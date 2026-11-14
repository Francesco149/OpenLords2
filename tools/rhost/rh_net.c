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
static char g_gametype[16] = "Full";
static uint32_t g_poll, g_nsent, g_nrecv, g_send_hash = 2166136261u;

/* ================================================================ messages */
typedef struct { uint32_t n; int from; uint32_t len; uint8_t *d; } msg_t;
static msg_t *g_sched; static uint32_t g_nsched, g_isched;      /* replay: stamped deliveries from the trace */
static msg_t g_pend[256]; static uint32_t g_npend;                 /* visible, not yet received */
static msg_t g_arr[256]; static uint32_t g_narr;                   /* record: arrived from the broker, not stamped */

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
        if (!pn || !pp || !pd || strstr(line, "\"meta\"")) continue;
        msg_t m; m.n = (uint32_t)strtoul(pn + 4, NULL, 10); m.from = (int)strtol(pp + 4, NULL, 10);
        char *q = strchr(pd + 4, '"'); if (!q) continue; q++;
        char *e = strchr(q, '"'); if (!e) continue;
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
    int k = sprintf(b, "{\"n\": %u, \"p\": %d, \"d\": \"", m->n, m->from);
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
    else if (h.type == 'M') {
        if (g_narr >= 256) rh_fatal("net: arrival queue overflow");
        g_arr[g_narr++] = (msg_t){ 0, h.a, h.len, d };
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
int nw_any(void)
{
    g_poll++;
    while (g_isched < g_nsched && g_sched[g_isched].n <= g_poll) pend_push(g_sched[g_isched++]);
    if (g_live) {
        sock_drain();
        for (uint32_t i = 0; i < g_narr; i++) { g_arr[i].n = g_poll; log_delivery(&g_arr[i]); pend_push(g_arr[i]); free(g_arr[i].d); }
        g_narr = 0;
    }
    return g_npend ? 0 : 1;          /* 0: something is waiting (0x4b6b71) */
}
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
#define THUNK_PAGE 0x5e100000u
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
    uint32_t poll, isched, nrecv, nsent, send_hash, npend, cb_special, cb_left; int game_closed;
    uint32_t m_esp, m_fn, m_arg, m_wake, m_exit; int m_state; uint32_t stack_n;
    msg_t pend[256]; uint8_t *stack;
} net_meta_t;
void *rh_net_meta_save(void)
{
    if (!g_on) return NULL;
    if (m_in) rh_fatal("net: a rollback point inside the monitor coroutine");
    net_meta_t *m = malloc(sizeof *m);
    m->poll = g_poll; m->isched = g_isched; m->nrecv = g_nrecv; m->nsent = g_nsent; m->send_hash = g_send_hash;
    m->npend = g_npend; m->cb_special = g_cb_special; m->cb_left = g_cb_left; m->game_closed = g_game_closed;
    for (uint32_t i = 0; i < g_npend; i++) { m->pend[i] = g_pend[i]; m->pend[i].d = malloc(g_pend[i].len ? g_pend[i].len : 1); memcpy(m->pend[i].d, g_pend[i].d, g_pend[i].len); }
    m->m_esp = rhnet_m_esp; m->m_fn = m_fn; m->m_arg = m_arg; m->m_wake = m_wake; m->m_exit = m_exit; m->m_state = m_state;
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
    g_npend = m->npend; g_cb_special = m->cb_special; g_cb_left = m->cb_left; g_game_closed = m->game_closed;
    for (uint32_t i = 0; i < g_npend; i++) { g_pend[i] = m->pend[i]; g_pend[i].d = malloc(m->pend[i].len ? m->pend[i].len : 1); memcpy(g_pend[i].d, m->pend[i].d, m->pend[i].len); }
    rhnet_m_esp = m->m_esp; m_fn = m->m_fn; m_arg = m->m_arg; m_wake = m->m_wake; m_exit = m->m_exit; m_state = m->m_state;
    if (m->stack_n) memcpy((void *)(uintptr_t)rhnet_m_esp, m->stack, m->stack_n);
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
    rh_log("net: SIGS fake, me %d of %d, game type %s%s\n", g_me, g_np, g_gametype, g_live ? ", live" : "");
}
void rh_net_report(void)
{
    if (!g_on) return;
    rh_log("net: polls=%u recv=%u/%u sent=%u send_hash=%08x pending=%u monitor=%d\n", g_poll, g_nrecv,
           g_nsched + 0u, g_nsent, g_send_hash, g_npend, m_state);
    if (g_live) { sock_send('Q', 0, NULL, 0); closesocket(g_sock); g_live = 0; }
}
