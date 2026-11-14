/* rh_net.c — multiplayer under rhost: a fake Sierra Internet Gaming System (SIGS) transport.
 *
 * Retail has two MP transports (docs/findings/multiplayer-rhost.md): DirectPlay (IDirectPlay2A, dialogs) and SIGS —
 * SNWValid.dll (SNWValidate) + SierraNW.dll, a flat cdecl TCP-message API the game loads at run time
 * (0x4b64d8 / 0x4b6754). With --net-sigs, rhost answers both DLLs itself:
 *   - DialogBoxParamA(0x81, DialogFunc 0x4b7f78) runs against a fake list box (rh_win.c): no DirectPlay provider is
 *     enumerated, IDOK on "Sierra Internet Gaming System" (item data 0) → EndDialog(1003) → retail takes SIGS;
 *   - the monitor thread (StartAddress 0x4b7052, CreateThread in 0x4b64d8) runs as a COROUTINE on its own fixed
 *     stack, scheduled in virtual time: it runs when the main thread sleeps / waits / ends a frame and the monitor's
 *     own Sleep(350) has elapsed — a pure function of the main thread's call sequence;
 *   - incoming messages are delivered at RECEIVE POLLS: the n-th CheckForAnyTCPMessage call makes every message
 *     stamped n visible. Replay: the stamps come from the trace ({"n": POLL, "p": FROM, "d": "hex"} lines). Record
 *     (--net-peer HOST:PORT, a broker: tools/rhost/rhnet.py): messages that arrived from the broker are stamped
 *     with the current poll and written to the out-trace — so each side of a live game replays ALONE, exactly.
 *   - record mode keeps the peers' virtual clocks within NET_WINDOW ms of each other (conservative sync: a process
 *     ahead blocks in real time until the others catch up; virtual time never sees the wait).
 * Sends are digested (count + FNV-1a) into the log; in record mode they go to the broker. */
#include "rhost.h"
#include <winsock2.h>

#define NET_WINDOW 200u
#define MON_LO 0x5e000000u
#define MON_HI 0x5e040000u
#define MON_THREAD ((HANDLE)(uintptr_t)0x7e5f0004u)
#define MOD_VALID  ((HMODULE)(uintptr_t)0x7e510000u)
#define MOD_NW     ((HMODULE)(uintptr_t)0x7e520000u)

void rh_vclock_set(uint32_t t);   /* rhost.c */
void rh_trace_write_raw(const char *line);   /* rh_win.c */
extern uint32_t g_cw0;

static int g_on, g_me, g_np = 2, g_live;
static int g_dp;                 /* "dp:ME/N": the DirectPlay transport (IDirectPlay2A) instead of SIGS */
static char g_gametype[16] = "Full";
static uint32_t g_poll, g_nsent, g_nrecv, g_send_hash = 2166136261u;

/* ================================================================ messages */
typedef struct { uint32_t n; int from; uint32_t len; uint8_t *d; int kind; } msg_t;   /* kind: NK_* */
enum { NK_DATA, NK_LEFT, NK_CHAT };   /* a message; a peer left the game; a lobby chat line (text in d) */
static msg_t *g_sched; static uint32_t g_nsched, g_isched;      /* replay: stamped deliveries from the trace */
static msg_t g_pend[256]; static uint32_t g_npend;                 /* visible, not yet received */
static msg_t g_arr[8192]; static uint32_t g_narr;                   /* record: arrived from the broker, not stamped */

static void pend_push(msg_t m)          /* the queue owns a copy: a receive frees it, a rollback revives the schedule's */
{
    if (g_npend >= 256) rh_fatal("net: pending queue overflow");
    uint8_t *d = malloc(m.len ? m.len : 1); memcpy(d, m.d, m.len); m.d = d;
    g_pend[g_npend++] = m;
}
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static void sched_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    static char line[8192]; uint32_t cap = 0;
    while (fgets(line, sizeof line, f)) {
        char *pn = strstr(line, "\"n\":"), *pp = strstr(line, "\"p\":"), *pd = strstr(line, "\"d\":");
        char *pc = strstr(line, "\"chat\":");
        if (pn && pp && !pd && !pc && strstr(line, "\"left\":")) {   /* {"n": N, "p": P, "left": 1} */
            if (g_nsched == cap) { cap = cap ? cap * 2 : 256; g_sched = realloc(g_sched, cap * sizeof *g_sched); }
            g_sched[g_nsched++] = (msg_t){ (uint32_t)strtoul(pn + 4, NULL, 10), (int)strtol(pp + 4, NULL, 10), 0, malloc(1), NK_LEFT };
            continue;
        }
        if (pc && !pd) pd = pc + 3;   /* {"n": N, "p": P, "chat": "HEX"}: the hex parse below finds the quotes after "chat": */
        if (!pn || !pp || !pd || strstr(line, "\"meta\"")) continue;
        msg_t m; m.n = (uint32_t)strtoul(pn + 4, NULL, 10); m.from = (int)strtol(pp + 4, NULL, 10); m.kind = NK_DATA;
        char *q = strchr(pd + 4, '"'); if (!q) continue; q++;
        char *e = strchr(q, '"'); if (!e) continue;
        if (pc) m.kind = NK_CHAT;
        m.len = (uint32_t)(e - q) / 2; m.d = malloc(m.len ? m.len : 1);
        for (uint32_t i = 0; i < m.len; i++) m.d[i] = (uint8_t)(hexv(q[2 * i]) << 4 | hexv(q[2 * i + 1]));
        if (g_nsched == cap) { cap = cap ? cap * 2 : 256; g_sched = realloc(g_sched, cap * sizeof *g_sched); }
        g_sched[g_nsched++] = m;
    }
    fclose(f);
    for (uint32_t i = 1; i < g_nsched; i++)
        for (uint32_t k = i; k && g_sched[k - 1].n > g_sched[k].n; k--) { msg_t t = g_sched[k]; g_sched[k] = g_sched[k - 1]; g_sched[k - 1] = t; }
    rh_log("net: %u scheduled deliveries from %s\n", g_nsched, path);
}
static void log_delivery(const msg_t *m)
{
    char *b = malloc(64 + 2 * m->len);
    if (m->kind == NK_LEFT) { sprintf(b, "{\"n\": %u, \"p\": %d, \"left\": 1}\n", m->n, m->from); rh_trace_write_raw(b); free(b); return; }
    int k = sprintf(b, "{\"n\": %u, \"p\": %d, \"%s\": \"", m->n, m->from, m->kind == NK_CHAT ? "chat" : "d");
    for (uint32_t i = 0; i < m->len; i++) k += sprintf(b + k, "%02x", m->d[i]);
    sprintf(b + k, "\"}\n");
    rh_trace_write_raw(b);
    free(b);
}

/* ================================================================ the broker link (record) */
#pragma pack(push, 1)
typedef struct { uint8_t type, a; uint16_t len; uint32_t clock; } nhdr_t;
#pragma pack(pop)
static SOCKET g_sock = INVALID_SOCKET;
static uint32_t g_peer_clock, g_sent_clock;
static void sock_send(uint8_t type, uint8_t a, const void *p, uint16_t len)
{
    nhdr_t h = { type, a, len, rh_vnow() };
    char buf[sizeof h + 65535];
    memcpy(buf, &h, sizeof h); if (len) memcpy(buf + sizeof h, p, len);
    int tot = (int)(sizeof h + len), off = 0;
    while (off < tot) { int r = send(g_sock, buf + off, tot - off, 0); if (r <= 0) rh_fatal("net: broker send failed (%d)", WSAGetLastError()); off += r; }
    if (type == 'C' || type == 'S') g_sent_clock = h.clock;
}
static void recv_all(void *p, int n)
{
    int off = 0;
    while (off < n) { int r = recv(g_sock, (char *)p + off, n - off, 0); if (r <= 0) rh_fatal("net: broker closed the link (%d)", WSAGetLastError()); off += r; }
}
static void sock_read_one(void)
{
    nhdr_t h; recv_all(&h, sizeof h);
    uint8_t *d = malloc(h.len ? h.len : 1);
    if (h.len) recv_all(d, h.len);
    if (h.type == 'C') { g_peer_clock = h.clock; free(d); }
    else if (h.type == 'L') {   /* a peer left the game (the broker saw it quit) */
        if (g_narr >= 8192) rh_fatal("net: arrival queue overflow");
        g_arr[g_narr++] = (msg_t){ 0, h.a, 0, d, NK_LEFT };
    }
    else if (h.type == 'M') {
        if (g_narr >= 8192) rh_fatal("net: arrival queue overflow");
        g_arr[g_narr++] = (msg_t){ 0, g_dp ? (h.a == 0xfe ? 0 : h.a + 1) : h.a, h.len, d, NK_DATA };   /* DP: the sender's DPID */
    } else { rh_log("net: broker frame type %c ignored\n", h.type); free(d); }
}
static int sock_readable(DWORD ms)
{
    fd_set s; FD_ZERO(&s); FD_SET(g_sock, &s);
    struct timeval tv = { (long)(ms / 1000), (long)(ms % 1000) * 1000 };
    return select(0, &s, NULL, NULL, &tv) > 0;
}
static void sock_drain(void) { while (sock_readable(0)) sock_read_one(); }
/* conservative clock sync: never run more than NET_WINDOW ms of virtual time ahead of the slowest peer */
void rh_net_sync(void)
{
    if (!g_live) return;
    uint32_t now = rh_vnow();
    if (now - g_sent_clock >= 50u) sock_send('C', 0, NULL, 0);
    #define AHEAD() (g_peer_clock < 0xf0000000u && (int32_t)(now - (g_peer_clock + NET_WINDOW)) > 0)   /* ≥ 0xf0000000: no live / running peer */
    if (!AHEAD()) return;
    if (g_sent_clock != now) sock_send('C', 0, NULL, 0);
    static uint32_t nblk; DWORD t0 = GetTickCount();
    if (++nblk <= 20 || !(nblk & 1023)) rh_log("net: f%u clock %u waits for the peers (%u) [%u]\n", g_frame, now, g_peer_clock, nblk);
    while (AHEAD()) sock_read_one();
    if (GetTickCount() - t0 > 2000) rh_log("net: f%u waited %lu ms wall\n", g_frame, (unsigned long)(GetTickCount() - t0));
}
/* a controller (rh_bot.c) holds this side between commands: its clock stops counting for the others' sync */
void rh_net_pause(int on) { if (g_live) sock_send(on ? 'P' : 'C', 0, NULL, 0); }
static void live_connect(const char *spec)
{
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    char host[128]; snprintf(host, sizeof host, "%s", spec);
    char *c = strrchr(host, ':'); if (!c) rh_fatal("net: --net-peer HOST:PORT");
    *c = 0; int port = atoi(c + 1);
    g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = inet_addr(host);
    if (connect(g_sock, (struct sockaddr *)&a, sizeof a) != 0) rh_fatal("net: cannot reach the broker %s (%d)", spec, WSAGetLastError());
    int one = 1; setsockopt(g_sock, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one);
    uint8_t np = (uint8_t)g_np;
    sock_send('H', (uint8_t)g_me, &np, 1);
    g_live = 1;
    rh_log("net: live, broker %s, me %d of %d\n", spec, g_me, g_np);
}

/* ================================================================ SNWValid.dll / SierraNW.dll (cdecl) */
int nw_validate(int game, char *ip, int *a, int *b, const char *path, const char *file, HWND w)
{
    (void)game; (void)path; (void)file; (void)w;
    strcpy(ip, "127.0.0.1"); *a = 0; *b = 0;
    /* the session (DAT_004e5a08, set by 0x4b62ea) keeps +28 / +32 = the addresses of 0x4b7a9a's stack buffers (the
     * exe's directory and path): dangling after the call, and their value depends on the caller's stack depth (a port
     * frame differs from retail's; retail on another OS differs too) — volatile, like the system-DLL pointer slots */
    uint32_t self = G32(0x4e5a08u);
    if (self) { rh_state_volatile_add(self + 28); rh_state_volatile_add(self + 32); }
    rh_log("net: SNWValidate → 0 (f%u)\n", g_frame);
    return 0;
}
int nw_init(const char *ip, int a, int b) { rh_log("net: InitializeDLL(%s, %d, %d) (f%u)\n", ip ? ip : "", a, b, g_frame); return 1; }
int nw_close(void) { rh_log("net: CloseDLL (f%u)\n", g_frame); return 0; }
/* 1: the lobby started a game. After GameClose (0x4b6ab6: leaving the MP game) the fake lobby starts the next one
 * 3 polls later (the monitor polls every 350 ms): the monitor logs GameEnd, then GameStart and signals the event a
 * re-entered 0x4b7a9a waits on (0x4b63d0) — before, the status stayed 0 and that wait never ended (mpbot deadlock:
 * File > New Game, Load from the MP setup page) */
static int g_game_closed;
int nw_check_conn(void) { if (g_game_closed) { g_game_closed--; return 0; } return 1; }
int nw_game_close(void) { rh_log("net: GameClose (f%u)\n", g_frame); g_game_closed = 3; return 0; }
static int net_poll(void);
int nw_any(void) { return net_poll() ? 0 : 1; }   /* 0: something is waiting (0x4b6b71) */
int nw_peek(void *buf, unsigned size, int player)
{
    (void)buf; (void)size;
    for (uint32_t i = 0; i < g_npend; i++) if (g_pend[i].from == player) return (int)g_pend[i].len;
    return 0;
}
int nw_recv(void *buf, int len, int player)
{
    for (uint32_t i = 0; i < g_npend; i++) if (g_pend[i].from == player) {
        msg_t m = g_pend[i];
        memmove(&g_pend[i], &g_pend[i + 1], (g_npend - i - 1) * sizeof *g_pend); g_npend--;
        uint32_t n = m.len < (uint32_t)len ? m.len : (uint32_t)len;
        memcpy(buf, m.d, n); free(m.d);
        g_nrecv++;
        return (int)n;
    }
    return 0;
}
static void digest(int to, const uint8_t *p, int len)
{
    g_nsent++;
    g_send_hash = (g_send_hash ^ (uint8_t)to) * 16777619u;
    for (int i = 0; i < len; i++) g_send_hash = (g_send_hash ^ p[i]) * 16777619u;
}
int nw_send(const void *buf, int len)
{
    digest(0xff, buf, len);
    if (g_live) sock_send('S', 0xff, buf, (uint16_t)len);
    return 1;
}
int nw_send_to(int player, const void *buf, int len)
{
    digest(player, buf, len);
    if (g_live) sock_send('S', (uint8_t)player, buf, (uint16_t)len);
    return 1;
}
int nw_nplayers(int x) { (void)x; return g_np; }
int nw_name(char *out, int player) { sprintf(out, "Player%d", player + 1); return 1; }
int nw_me(void) { return g_me; }
int nw_option(const char *key, char *out)
{
    if (key && !_stricmp(key, "GameType")) { strcpy(out, g_gametype); return 1; }
    rh_log("net: GetGameOption(%s): none\n", key ? key : "");
    return 0;
}
int nw_tell_game_msg(void) { return 0; }
int nw_free_game_msg(void) { return 0; }
int nw_ping(int a, int b) { (void)a; (void)b; return 40; }     /* < 100: not the slow-link flag 0x553264 */
static uint32_t g_cb_special, g_cb_left;
int nw_tell_special(int on, uint32_t cb, int a, int b) { (void)on; (void)a; (void)b; g_cb_special = cb; return 1; }
int nw_tell_left(int on, uint32_t cb, int a, int b) { (void)on; (void)a; (void)b; g_cb_left = cb; return 1; }


/* ================================================================ DirectPlay (IDirectPlay2A), "dp:ME/N"
 * Retail's other transport (docs/findings/multiplayer-rhost.md): DirectPlayEnumerateA lists one provider (a fixed
 * GUID), rh_win.c's dialogs pick it, Host (player 0) / Join, the default session name, the one session. DPIDs =
 * player index + 1 (as SIGS's); system messages come from DPID 0 (DPSYS_CREATEPLAYERORGROUP = 3 when a peer's
 * CreatePlayer reaches us through the broker). Every Receive call is a receive poll (deliveries stamped as SIGS's).
 * The object, its vtable and the method thunks live on the fixed export page: retail keeps the object pointer
 * (DAT_004e2a2c) and calls through the vtable. */
#define THUNK_PAGE 0x5e100000u
#define DP_THUNKS  (THUNK_PAGE + 0x400u)     /* 32 methods × 16 B */
#define DP_VTBL    (THUNK_PAGE + 0x800u)
#define DP_OBJ     (THUNK_PAGE + 0x900u)
#define DP_GUID    (THUNK_PAGE + 0x910u)     /* the provider */
#define DP_SESS    (THUNK_PAGE + 0x920u)     /* the session EnumSessions reports (DPSESSIONDESC2, 80 B) */
#define DP_SNAME   (THUNK_PAGE + 0x980u)
#define DPERR_NOMESSAGES 0x887700beu
#define DPERR_BUFFERTOOSMALL 0x8877001eu
static uint32_t g_dp_open;                   /* 1 created, 2 joined */
int rh_net_dp(void) { return g_dp; }
int rh_net_me(void) { return g_me; }
HRESULT WINAPI dp_qi(void *t, const void *iid, void **ppv) __asm__("_rhnet_dp_qi");
HRESULT WINAPI dp_qi(void *t, const void *iid, void **ppv) { (void)iid; *ppv = t; return 0; }
ULONG WINAPI dp_addref(void *t) __asm__("_rhnet_dp_addref");
ULONG WINAPI dp_addref(void *t) { (void)t; return 1; }
ULONG WINAPI dp_release(void *t) __asm__("_rhnet_dp_release");
ULONG WINAPI dp_release(void *t) { (void)t; return 0; }
HRESULT WINAPI dp_close(void *t) __asm__("_rhnet_dp_close");
HRESULT WINAPI dp_close(void *t) { (void)t; rh_log("net: DP Close (f%u)\n", g_frame); g_dp_open = 0; return 0; }
HRESULT WINAPI dp_create_player(void *t, uint32_t *id, void *name, HANDLE ev, void *data, DWORD size, DWORD fl) __asm__("_rhnet_dp_create_player");
HRESULT WINAPI dp_create_player(void *t, uint32_t *id, void *name, HANDLE ev, void *data, DWORD size, DWORD fl)
{
    (void)t; (void)name; (void)ev; (void)data; (void)size; (void)fl;
    *id = (uint32_t)g_me + 1;
    rh_log("net: DP CreatePlayer → DPID %u (f%u)\n", *id, g_frame);
    if (g_live) {   /* DPMSG_CREATEPLAYERORGROUP to the others: type 3, a player, its DPID (retail reads +0 / +8) */
        uint32_t m[12] = { 3, 1, *id, (uint32_t)g_np, 0, 0, 16, 0, 0, 0, 0, 0 };
        sock_send('Y', 0xfe, m, sizeof m);
    }
    return 0;
}
HRESULT WINAPI dp_destroy_player(void *t, uint32_t id) __asm__("_rhnet_dp_destroy_player");
HRESULT WINAPI dp_destroy_player(void *t, uint32_t id)
{
    (void)t; rh_log("net: DP DestroyPlayer %u (f%u)\n", id, g_frame);
    if (g_live) { uint32_t m[12] = { 5, 1, id, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; sock_send('Y', 0xfe, m, sizeof m); }
    return 0;
}
typedef BOOL (WINAPI *dp_enumsess_cb)(const void *desc, DWORD *timeout, DWORD flags, void *ctx);
HRESULT WINAPI dp_enum_sessions(void *t, void *desc, DWORD timeout, dp_enumsess_cb cb, void *ctx, DWORD fl) __asm__("_rhnet_dp_enum_sessions");
HRESULT WINAPI dp_enum_sessions(void *t, void *desc, DWORD timeout, dp_enumsess_cb cb, void *ctx, DWORD fl)
{
    (void)t; (void)desc; (void)fl;
    DWORD to = timeout;
    if (cb((const void *)(uintptr_t)DP_SESS, &to, 0, ctx)) cb(NULL, &to, 1 /* DPESC_TIMEDOUT */, ctx);
    return 0;
}
HRESULT WINAPI dp_get_caps(void *t, uint32_t *c, DWORD fl) __asm__("_rhnet_dp_get_caps");
HRESULT WINAPI dp_get_caps(void *t, uint32_t *c, DWORD fl)
{   /* DPCAPS: size, flags, max buffer, max queue, max players, hundred baud, latency, max local, header, timeout —
     * not the slow-link triple retail tests (max players 0x10000, latency 250, header 8 → DAT_00553264) */
    (void)t; (void)fl;
    c[1] = 0; c[2] = 1024; c[3] = 0; c[4] = 16; c[5] = 100000; c[6] = 50; c[7] = 1; c[8] = 20; c[9] = 2000;
    return 0;
}
HRESULT WINAPI dp_open(void *t, void *desc, DWORD fl) __asm__("_rhnet_dp_open");
HRESULT WINAPI dp_open(void *t, void *desc, DWORD fl)
{
    (void)t; (void)desc;
    g_dp_open = fl == 2 ? 1 : 2;
    rh_log("net: DP Open %s (f%u)\n", fl == 2 ? "create" : "join", g_frame);
    return 0;
}
/* a delivery becomes visible: a message joins the queue; a peer leaving calls SIGS's TellMePlayerLeftGame callback
 * (0x4b7a7c: DAT_004e5a90[player] = 1 → 0x4b8c3a → 0x43e6a2(2, player + 1)) or, under DirectPlay, arrives as
 * DPSYS_DESTROYPLAYERORGROUP (5) from DPID 0; a lobby chat line calls TellMeSpecialChatReceived's (0x4b79e3(text,
 * name): a type-20 popup) — the real DLLs call them from their own threads, rhost at the stamped poll */
static void deliver(msg_t m)
{
    if (m.kind == NK_DATA) { pend_push(m); return; }
    if (m.kind == NK_LEFT && g_dp) {
        uint32_t sys[12] = { 5, 1, (uint32_t)m.from + 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        pend_push((msg_t){ m.n, 0, sizeof sys, (uint8_t *)sys, NK_DATA });
        return;
    }
    rh_log("net: f%u poll %u: %s from player %d\n", g_frame, m.n, m.kind == NK_LEFT ? "left" : "lobby chat", m.from);
    if (m.kind == NK_LEFT && g_cb_left) ((int (__cdecl *)(int))(uintptr_t)g_cb_left)(m.from);
    if (m.kind == NK_CHAT && g_cb_special) {   /* 0x4b79e3 prints "%s: %s" with &arg_4: the sender's name is passed BY
                                                 * VALUE, inline on the stack after the text pointer (a char-array struct) */
        typedef struct { char s[32]; } name32_t;
        char text[256]; name32_t name;
        uint32_t n = m.len < sizeof text - 1 ? m.len : sizeof text - 1;
        memcpy(text, m.d, n); text[n] = 0;
        memset(&name, 0, sizeof name); sprintf(name.s, "Player%d", m.from + 1);
        ((void (__cdecl *)(const char *, name32_t))(uintptr_t)g_cb_special)(text, name);
    }
}
static int net_poll(void)   /* one receive poll: stamp + reveal deliveries (SIGS CheckForAnyTCPMessage, DP Receive) */
{
    g_poll++;
    while (g_isched < g_nsched && g_sched[g_isched].n <= g_poll) deliver(g_sched[g_isched++]);
    if (g_live) {
        sock_drain();
        for (uint32_t i = 0; i < g_narr; i++) { g_arr[i].n = g_poll; log_delivery(&g_arr[i]); deliver(g_arr[i]); free(g_arr[i].d); }
        g_narr = 0;
    }
    return (int)g_npend;
}
/* the controller injects a lobby chat line from player P (rh_bot.c "netchat": record / bot mode) */
void rh_net_inject_chat(int from, const char *text)
{
    if (!g_on) return;
    if (g_narr >= 8192) rh_fatal("net: arrival queue overflow");
    size_t n = strlen(text); uint8_t *d = malloc(n ? n : 1); memcpy(d, text, n);
    g_arr[g_narr++] = (msg_t){ 0, from, (uint32_t)n, d, NK_CHAT };
    if (!g_live) rh_log("net: a chat line queued without a broker: it is stamped at the next poll only in a live (record / bot) run\n");
}
HRESULT WINAPI dp_receive(void *t, uint32_t *from, uint32_t *to, DWORD fl, void *buf, DWORD *size) __asm__("_rhnet_dp_receive");
HRESULT WINAPI dp_receive(void *t, uint32_t *from, uint32_t *to, DWORD fl, void *buf, DWORD *size)
{
    (void)t; (void)fl;
    if (!net_poll()) return DPERR_NOMESSAGES;
    msg_t m = g_pend[0];
    if (m.len > *size) { *size = m.len; return DPERR_BUFFERTOOSMALL; }
    memmove(&g_pend[0], &g_pend[1], (g_npend - 1) * sizeof *g_pend); g_npend--;
    memcpy(buf, m.d, m.len); free(m.d);
    *from = (uint32_t)m.from; *to = (uint32_t)g_me + 1; *size = m.len;
    g_nrecv++;
    return 0;
}
static void digest(int to, const uint8_t *p, int len);
HRESULT WINAPI dp_send(void *t, uint32_t from, uint32_t to, DWORD fl, const void *buf, DWORD len) __asm__("_rhnet_dp_send");
HRESULT WINAPI dp_send(void *t, uint32_t from, uint32_t to, DWORD fl, const void *buf, DWORD len)
{
    (void)t; (void)from; (void)fl;
    digest(to ? (int)to - 1 : 0xff, buf, (int)len);
    if (g_live) sock_send('S', to ? (uint8_t)(to - 1) : 0xff, buf, (uint16_t)len);
    return 0;
}
HRESULT WINAPI dp_set_session_desc(void *t, void *desc, DWORD fl) __asm__("_rhnet_dp_set_session_desc");
HRESULT WINAPI dp_set_session_desc(void *t, void *desc, DWORD fl)   /* 0x4b7514 at game start: no new players */
{ (void)t; (void)desc; rh_log("net: DP SetSessionDesc(flags %lu) (f%u)\n", (unsigned long)fl, g_frame); return 0; }
void __attribute__((used)) dp_unused(uint32_t idx) { rh_fatal("net: retail called IDirectPlay2A method %u (not faked)", idx); }
/* DirectPlayCreate / DirectPlayEnumerateA (DPLAYX ordinals 1 / 2, rhost.c binds them): 1 = handled */
typedef BOOL (WINAPI *dp_enum_cb)(void *guid, char *name, DWORD maj, DWORD min, void *ctx);
int net_dp_enumerate_impl(void *cb, void *ctx)
{
    if (!g_dp) return 0;
    ((dp_enum_cb)cb)((void *)(uintptr_t)DP_GUID, "Internet TCP/IP Connection For DirectPlay", 5, 0, ctx);
    return 1;
}
int net_dp_create_impl(void *guid, void **out)
{
    if (!g_dp) return 0;
    /* 0x4b7585's join path keeps &its first argument (the application GUID, on its stack) in DAT_004e5b08 for the
     * session dialog: a dead-frame pointer whose value depends on the caller's depth — volatile, like the SIGS
     * session's +28 / +32 */
    rh_state_volatile_add(0x4e5b08u);
    rh_log("net: DirectPlayCreate(%s) (f%u)\n", guid == (void *)(uintptr_t)DP_GUID ? "the provider" : "?", g_frame);
    *out = (void *)(uintptr_t)DP_OBJ;
    return 1;
}

/* ================================================================ residue-neutral entries
 * Host code called from retail runs on retail's stack and leaves residue below retail's frame; a retail MP path reads an
 * uninitialized local (mp-start's state changed with an rh_net.c edit). Every entry from retail into this file goes
 * through a wrapper that, with --net-sigs, zeroes the 16 KB below the caller's stack after the call — the residue no
 * longer depends on rh_net's code. Without --net-sigs the wrapper tail-jumps: a plain call, every other scenario as
 * before. (Re-entry: the monitor coroutine only runs inside rh_net_tick / rh_net_wait and enters only nw_* — different
 * slots; its own Sleep goes through rh_net_sleep, not wrapped.) */
int rhnet_on;
__asm__(".text\n_rhnet_scrub:\n\t pushl %eax\n\t pushl %edx\n\t pushl %ecx\n\t pushl %edi\n"
        "\t leal -16384(%esp), %edi\n\t movl $4096, %ecx\n\t xorl %eax, %eax\n\t cld\n\t rep stosl\n"
        "\t popl %edi\n\t popl %ecx\n\t popl %edx\n\t popl %eax\n\t ret\n");
#define SCRUBBED(name, fn) \
    uint32_t rhnet_ret_##name; \
    __asm__(".text\n.globl _" #name "\n_" #name ":\n\t cmpl $0, _rhnet_on\n\t je _" #fn "\n\t popl _rhnet_ret_" #name \
            "\n\t call _" #fn "\n\t pushl _rhnet_ret_" #name "\n\t jmp _rhnet_scrub\n"); \
    void name##_sym(void) __asm__("_" #name);
SCRUBBED(w_nw_validate, nw_validate)
SCRUBBED(w_nw_init, nw_init)
SCRUBBED(w_nw_close, nw_close)
SCRUBBED(w_nw_check_conn, nw_check_conn)
SCRUBBED(w_nw_game_close, nw_game_close)
SCRUBBED(w_nw_any, nw_any)
SCRUBBED(w_nw_peek, nw_peek)
SCRUBBED(w_nw_recv, nw_recv)
SCRUBBED(w_nw_send, nw_send)
SCRUBBED(w_nw_send_to, nw_send_to)
SCRUBBED(w_nw_nplayers, nw_nplayers)
SCRUBBED(w_nw_name, nw_name)
SCRUBBED(w_nw_me, nw_me)
SCRUBBED(w_nw_option, nw_option)
SCRUBBED(w_nw_tell_game_msg, nw_tell_game_msg)
SCRUBBED(w_nw_free_game_msg, nw_free_game_msg)
SCRUBBED(w_nw_ping, nw_ping)
SCRUBBED(w_nw_tell_special, nw_tell_special)
SCRUBBED(w_nw_tell_left, nw_tell_left)
SCRUBBED(w_dp_qi, rhnet_dp_qi)
SCRUBBED(w_dp_addref, rhnet_dp_addref)
SCRUBBED(w_dp_release, rhnet_dp_release)
SCRUBBED(w_dp_close, rhnet_dp_close)
SCRUBBED(w_dp_create_player, rhnet_dp_create_player)
SCRUBBED(w_dp_destroy_player, rhnet_dp_destroy_player)
SCRUBBED(w_dp_enum_sessions, rhnet_dp_enum_sessions)
SCRUBBED(w_dp_get_caps, rhnet_dp_get_caps)
SCRUBBED(w_dp_open, rhnet_dp_open)
SCRUBBED(w_dp_receive, rhnet_dp_receive)
SCRUBBED(w_dp_send, rhnet_dp_send)
SCRUBBED(w_dp_set_session_desc, rhnet_dp_set_session_desc)
SCRUBBED(rh_net_dp_enumerate, net_dp_enumerate_impl)
SCRUBBED(rh_net_dp_create, net_dp_create_impl)
SCRUBBED(rh_net_tick, net_tick_impl)
SCRUBBED(rh_net_wait, net_wait_impl)
SCRUBBED(rh_net_load, net_load_impl)
SCRUBBED(rh_net_proc, net_proc_impl)
SCRUBBED(rh_net_free, net_free_impl)
SCRUBBED(rh_net_create_thread, net_create_thread_impl)

static const struct { const char *name; void *fn; } k_nw[] = {
    { "InitializeDLL", (void *)w_nw_init_sym }, { "CloseDLL", (void *)w_nw_close_sym }, { "CheckGameConnectStatus", (void *)w_nw_check_conn_sym },
    { "GameClose", (void *)w_nw_game_close_sym }, { "CheckForAnyTCPMessage", (void *)w_nw_any_sym }, { "PeekForTCPMessage", (void *)w_nw_peek_sym },
    { "GetNumberOfPlayers", (void *)w_nw_nplayers_sym }, { "SendTCPMessage", (void *)w_nw_send_sym }, { "SendTCPPointMessage", (void *)w_nw_send_to_sym },
    { "RecvTCPMessage", (void *)w_nw_recv_sym }, { "GetPlayerXName", (void *)w_nw_name_sym }, { "GetMyPlayerNumber", (void *)w_nw_me_sym },
    { "GetGameOption", (void *)w_nw_option_sym }, { "TellMeGameMessReceived", (void *)w_nw_tell_game_msg_sym }, { "FreeGameMessageBuffer", (void *)w_nw_free_game_msg_sym },
    { "PingTime", (void *)w_nw_ping_sym }, { "TellMeSpecialChatReceived", (void *)w_nw_tell_special_sym }, { "TellMePlayerLeftGame", (void *)w_nw_tell_left_sym },
};

/* retail keeps the export pointers in its session object (+44..+112): they must not move with rhost's code, so
 * GetProcAddress hands out jmp thunks on a page at a fixed address (one per k_nw row; SNWValidate last) */
static void thunks_init(void)
{
    if (VirtualAlloc((void *)(uintptr_t)THUNK_PAGE, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE) != (void *)(uintptr_t)THUNK_PAGE)
        rh_fatal("net: the export thunk page %08x is taken", THUNK_PAGE);
    size_t n = sizeof k_nw / sizeof *k_nw;
    for (size_t i = 0; i <= n; i++) {
        uint8_t *t = (uint8_t *)(uintptr_t)(THUNK_PAGE + 8 * i);
        uint32_t to = (uint32_t)(uintptr_t)(i < n ? k_nw[i].fn : (void *)w_nw_validate_sym);
        t[0] = 0xe9; *(uint32_t *)(t + 1) = to - (uint32_t)(uintptr_t)(t + 5);
    }
    /* DirectPlay: 32 method thunks (16 B: jmp to the wrapper, or push idx; call dp_unused), the vtable, the object */
    static void *const used[32] = { [0] = w_dp_qi_sym, [1] = w_dp_addref_sym, [2] = w_dp_release_sym, [4] = w_dp_close_sym,
        [6] = w_dp_create_player_sym, [9] = w_dp_destroy_player_sym, [13] = w_dp_enum_sessions_sym, [14] = w_dp_get_caps_sym,
        [24] = w_dp_open_sym, [25] = w_dp_receive_sym, [26] = w_dp_send_sym, [31] = w_dp_set_session_desc_sym };
    for (uint32_t m = 0; m < 32; m++) {
        uint8_t *t = (uint8_t *)(uintptr_t)(DP_THUNKS + 16 * m);
        if (used[m]) { t[0] = 0xe9; *(uint32_t *)(t + 1) = (uint32_t)(uintptr_t)used[m] - (uint32_t)(uintptr_t)(t + 5); }
        else { t[0] = 0x68; *(uint32_t *)(t + 1) = m; t[5] = 0xe8; *(uint32_t *)(t + 6) = (uint32_t)(uintptr_t)dp_unused - (uint32_t)(uintptr_t)(t + 10); }
        G32(DP_VTBL + 4 * m) = DP_THUNKS + 16 * m;
    }
    G32(DP_OBJ) = DP_VTBL;
    static const uint8_t guid[16] = { 0x3a, 0x82, 0x6e, 0x36, 0x7c, 0xb2, 0xcf, 0x11, 0x83, 0x46, 0x00, 0x20, 0xaf, 0x2a, 0x0d, 0x1b };
    memcpy((void *)(uintptr_t)DP_GUID, guid, 16);
    uint32_t *sd = (uint32_t *)(uintptr_t)DP_SESS;      /* DPSESSIONDESC2: size, flags, guidInstance, guidApplication, max / current players, name … */
    sd[0] = 80; sd[1] = 68; sd[2] = 0x4c32534c; sd[3] = 0x6f697373; sd[4] = 0x0000316e; sd[5] = 0x20262026;
    sd[10] = 5; sd[11] = 1; sd[12] = DP_SNAME;
    strcpy((char *)(uintptr_t)DP_SNAME, "Lords2 Session1");
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)THUNK_PAGE, 0x1000);
}
int rh_net_on(void) { return g_on; }
/* LoadLibraryA / GetProcAddress / FreeLibrary: 1 = handled */
int net_load_impl(const char *n, HMODULE *out)
{
    if (!g_on || !n) return 0;
    const char *b = strrchr(n, '\\'); b = b ? b + 1 : n;
    if (!_stricmp(b, "SNWValid.dll")) { *out = MOD_VALID; return 1; }
    if (!_stricmp(b, "SierraNW.dll")) { *out = MOD_NW; return 1; }
    return 0;
}
int net_proc_impl(HMODULE m, const char *name, FARPROC *out)
{
    if (m == MOD_VALID) { *out = (name && !strcmp(name, "SNWValidate")) ? (FARPROC)(uintptr_t)(THUNK_PAGE + 8 * (sizeof k_nw / sizeof *k_nw)) : NULL; return 1; }
    if (m != MOD_NW) return 0;
    *out = NULL;
    for (size_t i = 0; i < sizeof k_nw / sizeof *k_nw; i++) if (name && !strcmp(name, k_nw[i].name)) *out = (FARPROC)(uintptr_t)(THUNK_PAGE + 8 * i);
    if (!*out) rh_log("net: SierraNW has no %s\n", name ? name : "(ordinal)");
    return 1;
}
int net_free_impl(HMODULE m) { return m == MOD_VALID || m == MOD_NW; }

/* ================================================================ the monitor thread, as a coroutine */
uint32_t rhnet_m_esp, rhnet_m_hesp;   /* referenced from asm */
static uint32_t m_fn, m_arg, m_wake, m_exit;
static int m_state, m_in;            /* 0 none, 1 alive, 2 finished */
static void *m_tib_base, *m_tib_limit, *m_tib_seh; static uint16_t m_hcw;
__attribute__((naked)) static void m_swap_in(void)
{
    __asm__("pushl %ebx\n\t pushl %esi\n\t pushl %edi\n\t pushl %ebp\n\t"
            "movl %esp, _rhnet_m_hesp\n\t movl _rhnet_m_esp, %esp\n\t"
            "popl %ebp\n\t popl %edi\n\t popl %esi\n\t popl %ebx\n\t ret");
}
__attribute__((naked)) static void m_swap_out(void)
{
    __asm__("pushl %ebx\n\t pushl %esi\n\t pushl %edi\n\t pushl %ebp\n\t"
            "movl %esp, _rhnet_m_esp\n\t movl _rhnet_m_hesp, %esp\n\t"
            "popl %ebp\n\t popl %edi\n\t popl %esi\n\t popl %ebx\n\t ret");
}
static void __attribute__((used)) m_body(void)
{
    m_exit = ((DWORD (WINAPI *)(void *))(uintptr_t)m_fn)((void *)(uintptr_t)m_arg);
    m_state = 2;
    rh_log("net: monitor thread returned %u (f%u)\n", m_exit, g_frame);
    m_swap_out();
    for (;;) ;
}
__attribute__((naked, used)) static void m_tramp(void) { __asm__("fninit\n\t fldcw _g_cw0\n\t call _m_body\n\t hlt"); }
static void m_resume(void)
{
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    m_tib_base = tib->StackBase; m_tib_limit = tib->StackLimit; m_tib_seh = tib->ExceptionList;
    tib->StackBase = (void *)(uintptr_t)MON_HI; tib->StackLimit = (void *)(uintptr_t)(MON_LO + 0x1000u);
    tib->ExceptionList = (void *)(uintptr_t)0xffffffffu;
    __asm__ volatile("fnstcw %0" : "=m"(m_hcw));
    m_in = 1;
    m_swap_in();
    m_in = 0;
    __asm__ volatile("fldcw %0" : : "m"(m_hcw));
    tib->StackBase = m_tib_base; tib->StackLimit = m_tib_limit; tib->ExceptionList = m_tib_seh;
}
static void m_run_due(void)
{
    while (m_state == 1 && !m_in && (int32_t)(rh_vnow() - m_wake) >= 0) m_resume();
}
int net_create_thread_impl(LPTHREAD_START_ROUTINE fn, LPVOID arg, LPDWORD tid, HANDLE *out)
{
    if (!g_on) return 0;
    if (m_state == 1) rh_fatal("net: a second retail thread (%p) while the monitor lives", (void *)fn);
    static int mapped;
    if (!mapped) {
        if (VirtualAlloc((void *)(uintptr_t)MON_LO, MON_HI - MON_LO, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) != (void *)(uintptr_t)MON_LO)
            rh_fatal("net: the monitor stack range %08x is taken", MON_LO);
        mapped = 1;
    }
    memset((void *)(uintptr_t)MON_LO, 0, MON_HI - MON_LO);
    uint32_t sp = MON_HI - 64;
    sp -= 4; G32(sp) = (uint32_t)(uintptr_t)m_tramp;
    for (int i = 0; i < 4; i++) { sp -= 4; G32(sp) = 0; }
    rhnet_m_esp = sp; m_fn = (uint32_t)(uintptr_t)fn; m_arg = (uint32_t)(uintptr_t)arg;
    m_state = 1; m_wake = rh_vnow();
    if (tid) *tid = 0x5e1u;
    *out = MON_THREAD;
    rh_log("net: retail thread %p runs as a coroutine (f%u)\n", (void *)fn, g_frame);
    return 1;
}
/* the main thread's Sleep advanced the clock / a frame ended: run the monitor if its sleep is over */
void net_tick_impl(void) { if (g_on) { m_run_due(); rh_net_sync(); } }
/* Sleep from the monitor: yield until vclock + ms; 1 = handled */
int rh_net_sleep(DWORD ms)
{
    if (!m_in) return 0;
    m_wake = rh_vnow() + ms;
    m_swap_out();
    return 1;
}
int net_wait_impl(HANDLE h, DWORD ms, DWORD *ret)
{
    if (!g_on || m_in || (!m_state && h != MON_THREAD)) return 0;
    uint32_t deadline = ms == INFINITE ? 0xffffffffu : rh_vnow() + ms;
    for (;;) {
        m_run_due();
        DWORD r = h == MON_THREAD ? (m_state == 2 ? WAIT_OBJECT_0 : WAIT_TIMEOUT) : WaitForSingleObject(rh_handle_real(h), 0);
        if (r != WAIT_TIMEOUT) { *ret = r; return 1; }
        if ((int32_t)(rh_vnow() - deadline) >= 0) { *ret = WAIT_TIMEOUT; return 1; }
        if (m_state != 1 && ms == INFINITE) rh_fatal("net: an infinite wait nothing can signal (f%u)", g_frame);
        uint32_t next = m_state == 1 && (int32_t)(m_wake - deadline) < 0 ? m_wake : deadline;
        if ((int32_t)(next - rh_vnow()) <= 0) next = rh_vnow() + 1;
        rh_vclock_set(next);
        rh_net_sync();
    }
}
int rh_net_thread_handle(HANDLE h) { return g_on && h == MON_THREAD; }
DWORD rh_net_thread_exit(void) { return m_state == 2 ? m_exit : STILL_ACTIVE; }

/* ================================================================ rollback (shadow) / savestates
 * Everything a retail call can change here: counters, the visible queue (deep copies), the monitor coroutine (its
 * registers and the live part of its stack). The broker link is not rolled back (verify / rd never run live). */
typedef struct {
    uint32_t poll, isched, nrecv, nsent, send_hash, npend, cb_special, cb_left, dp_open; int game_closed;
    uint32_t ev_handle; int ev_set;   /* retail's hObject DAT_004e5a04 (the monitor's GameStart event): a kernel object */
    uint32_t m_esp, m_fn, m_arg, m_wake, m_exit; int m_state; uint32_t stack_n;
    msg_t pend[256]; uint8_t *stack;
} net_meta_t;
void *rh_net_meta_save(void)
{
    if (!g_on) return NULL;
    if (m_in) rh_fatal("net: a rollback point inside the monitor coroutine");
    net_meta_t *m = malloc(sizeof *m);
    m->poll = g_poll; m->isched = g_isched; m->nrecv = g_nrecv; m->nsent = g_nsent; m->send_hash = g_send_hash;
    m->npend = g_npend; m->cb_special = g_cb_special; m->cb_left = g_cb_left; m->game_closed = g_game_closed; m->dp_open = g_dp_open;
    for (uint32_t i = 0; i < g_npend; i++) { m->pend[i] = g_pend[i]; m->pend[i].d = malloc(g_pend[i].len ? g_pend[i].len : 1); memcpy(m->pend[i].d, g_pend[i].d, g_pend[i].len); }
    m->m_esp = rhnet_m_esp; m->m_fn = m_fn; m->m_arg = m_arg; m->m_wake = m_wake; m->m_exit = m_exit; m->m_state = m_state;
    /* the event the monitor signals and 0x4b63d0 waits on / resets is a real (manual-reset) kernel event: its state
     * is host state the shadow's memory rollback misses (a port run after retail's saw it reset: mp-skirmish-p2's
     * rejoin waited ~1 s longer, 4 UI rows diverged on clock stamps) */
    m->ev_handle = G32(0x4e5a04u); m->ev_set = 0;
    if (m->ev_handle) m->ev_set = WaitForSingleObject(rh_handle_real((HANDLE)(uintptr_t)m->ev_handle), 0) == WAIT_OBJECT_0;
    m->stack_n = m_state == 1 ? MON_HI - rhnet_m_esp : 0;
    m->stack = m->stack_n ? malloc(m->stack_n) : NULL;
    if (m->stack_n) memcpy(m->stack, (void *)(uintptr_t)rhnet_m_esp, m->stack_n);
    return m;
}
void rh_net_meta_restore(const void *p)
{
    const net_meta_t *m = p;
    if (!m) return;
    for (uint32_t i = 0; i < g_npend; i++) free(g_pend[i].d);
    g_poll = m->poll; g_isched = m->isched; g_nrecv = m->nrecv; g_nsent = m->nsent; g_send_hash = m->send_hash;
    g_npend = m->npend; g_cb_special = m->cb_special; g_cb_left = m->cb_left; g_game_closed = m->game_closed; g_dp_open = m->dp_open;
    for (uint32_t i = 0; i < g_npend; i++) { g_pend[i] = m->pend[i]; g_pend[i].d = malloc(m->pend[i].len ? m->pend[i].len : 1); memcpy(g_pend[i].d, m->pend[i].d, m->pend[i].len); }
    rhnet_m_esp = m->m_esp; m_fn = m->m_fn; m_arg = m->m_arg; m_wake = m->m_wake; m_exit = m->m_exit; m_state = m->m_state;
    if (m->stack_n) memcpy((void *)(uintptr_t)rhnet_m_esp, m->stack, m->stack_n);
    if (m->ev_handle && m->ev_handle == G32(0x4e5a04u)) {
        HANDLE ev = rh_handle_real((HANDLE)(uintptr_t)m->ev_handle);
        if (m->ev_set) SetEvent(ev); else ResetEvent(ev);
    }
}
void rh_net_meta_free(void *p)
{
    net_meta_t *m = p;
    if (!m) return;
    for (uint32_t i = 0; i < m->npend; i++) free(m->pend[i].d);
    free(m->stack); free(m);
}

/* ================================================================ setup / report */
/* --net-sigs ME/N[,full|skirmish] */
void rh_net_config(const char *spec)
{
    g_on = rhnet_on = 1;
    if (!strncmp(spec, "dp:", 3)) { g_dp = 1; spec += 3; }
    if (sscanf(spec, "%d/%d", &g_me, &g_np) != 2 || g_me < 0 || g_np < 2 || g_np > 5 || g_me >= g_np)
        rh_fatal("net: --net-sigs ME/N[,full|skirmish] (N 2..5): %s", spec);
    const char *c = strchr(spec, ',');
    if (c) snprintf(g_gametype, sizeof g_gametype, "%s", !_stricmp(c + 1, "skirmish") ? "Skirmish" : "Full");
}
void rh_net_install(const char *peer)
{
    if (!g_on) return;
    thunks_init();
    if (O.trace) sched_load(O.trace);
    if (peer) live_connect(peer);
    rh_log("net: %s fake, me %d of %d, game type %s%s\n", g_dp ? "DirectPlay" : "SIGS", g_me, g_np, g_gametype, g_live ? ", live" : "");
}
void rh_net_report(void)
{
    if (!g_on) return;
    rh_log("net: polls=%u recv=%u/%u sent=%u send_hash=%08x pending=%u monitor=%d\n", g_poll, g_nrecv,
           g_nsched + 0u, g_nsent, g_send_hash, g_npend, m_state);
    if (g_live) { sock_send('Q', 0, NULL, 0); closesocket(g_sock); g_live = 0; }
}
