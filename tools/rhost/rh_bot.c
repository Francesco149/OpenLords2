/* rh_bot.c — the control channel: a controller (tools/rhost/rhbot.py — trace authoring, probes, the
 * autoplay bot) drives a live rhost run over TCP, one JSON line per command / reply.
 *
 *   rhost.exe ... --bot 127.0.0.1:PORT     (rhost connects; on Windows through `ssh -R`)
 *
 * The run stops at frame boundaries only: on connect (before retail's entry), then whenever the frames
 * a `run` asked for are done. While stopped it serves commands:
 *   {"cmd":"run","n":N}                   resume for N frames, then stop again
 *   {"cmd":"post","m":M,"w":W,"l":L}      queue an input message — dispatched before the next frame
 *                                         runs, and written to the output trace (the replayable record)
 *   {"cmd":"read","va":VA,"n":N}          → {"hex":"…"} (retail memory)
 *   {"cmd":"write","va":VA,"hex":"…"}     poke (exploration only: the run is no longer a pure replay —
 *                                         RESULT says poked=N)
 *   {"cmd":"shot","tag":"x"}              shot_x.png + frame_x.idx in the run dir
 *   {"cmd":"hash"}                        → state hashes
 *   {"cmd":"dump","tag":"x"}              state_x.rhs in the run dir
 *   {"cmd":"quit"}                        finish the run (RESULT reason=bot-quit)
 * Every reply carries the frame, the sim tick and the screen id. */
#include <winsock2.h>
#include "rhost.h"

static SOCKET g_s = INVALID_SOCKET;
static uint32_t g_run_left;
static uint32_t g_poked;
static char g_in[1 << 16]; static int g_inlen;

static void send_line(const char *s)
{
    size_t n = strlen(s);
    while (n) { int k = send(g_s, s, (int)n, 0); if (k <= 0) rh_fatal("bot: connection lost (send)"); s += k; n -= (size_t)k; }
}
static int recv_line(char *out, size_t cap)
{
    for (;;) {
        char *nl = memchr(g_in, '\n', (size_t)g_inlen);
        if (nl) {
            size_t l = (size_t)(nl - g_in);
            if (l >= cap) l = cap - 1;
            memcpy(out, g_in, l); out[l] = 0;
            int used = (int)(nl - g_in) + 1;
            memmove(g_in, g_in + used, (size_t)(g_inlen - used)); g_inlen -= used;
            return 1;
        }
        if (g_inlen == (int)sizeof g_in) rh_fatal("bot: command line too long");
        int k = recv(g_s, g_in + g_inlen, (int)sizeof g_in - g_inlen, 0);
        if (k <= 0) return 0;
        g_inlen += k;
    }
}
static int jnum(const char *s, const char *key, uint32_t *v)
{
    char k[32]; snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(s, k);
    if (!p) return 0;
    p += strlen(k); while (*p == ' ') p++;
    *v = (uint32_t)strtoul(p, NULL, 0);
    return 1;
}
static int jstr(const char *s, const char *key, char *out, size_t cap)
{
    char k[32]; snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(s, k);
    if (!p) return 0;
    p += strlen(k); while (*p == ' ') p++;
    if (*p++ != '"') return 0;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < cap) out[i++] = *p++;
    out[i] = 0;
    return 1;
}
static void reply(const char *extra)
{
    char b[512];
    snprintf(b, sizeof b, "{\"frame\": %u, \"tick\": %u, \"screen\": %u, \"state\": %u%s%s}\n", g_frame, G32(L2_SIM_TICK),
             G8(L2_SCREEN), G32(L2_ENGINE_STATE), extra && *extra ? ", " : "", extra ? extra : "");
    send_line(b);
}
static int readable(uint32_t va, uint32_t n)
{
    MEMORY_BASIC_INFORMATION mi;
    for (uint32_t a = va & ~0xfffu; a < va + n; a += 0x1000)
        if (!VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return 1;
}

/* serve commands until one resumes the run */
static void serve(void)
{
    static char line[1 << 16];
    rh_net_pause(1);                     /* MP: a side idle on its controller does not hold the others' clocks */
    reply("\"ev\": \"stop\"");
    for (;;) {
        if (!recv_line(line, sizeof line)) { rh_log("bot: controller closed the connection (frame %u)\n", g_frame); rh_finish("bot-closed"); }
        char cmd[32] = "";
        jstr(line, "cmd", cmd, sizeof cmd);
        uint32_t v = 0, n = 0, m = 0, w = 0, l = 0;
        if (!strcmp(cmd, "run")) {
            g_run_left = jnum(line, "n", &v) && v ? v : 1;
            rh_net_pause(0);
            return;
        } else if (!strcmp(cmd, "post")) {
            jnum(line, "m", &m); jnum(line, "w", &w); jnum(line, "l", &l);
            rh_win_inject(m, w, l);
            reply("\"ok\": 1");
        } else if (!strcmp(cmd, "read")) {
            jnum(line, "va", &v); jnum(line, "n", &n);
            if (n > 1u << 20 || !readable(v, n)) { reply("\"error\": \"unreadable\""); continue; }
            char *b = malloc(2 * (size_t)n + 32), *o = b;
            o += sprintf(o, "\"hex\": \"");
            static const char hx[] = "0123456789abcdef";
            for (uint32_t i = 0; i < n; i++) { uint8_t c = G8(v + i); *o++ = hx[c >> 4]; *o++ = hx[c & 15]; }
            *o++ = '"'; *o = 0;
            char *full = malloc(strlen(b) + 160);
            sprintf(full, "{\"frame\": %u, \"tick\": %u, \"screen\": %u, \"state\": %u, %s}\n", g_frame, G32(L2_SIM_TICK), G8(L2_SCREEN), G32(L2_ENGINE_STATE), b);
            send_line(full);
            free(full); free(b);
        } else if (!strcmp(cmd, "write")) {
            static char hex[1 << 15];
            jnum(line, "va", &v); jstr(line, "hex", hex, sizeof hex);
            size_t k = strlen(hex) / 2;
            if (!readable(v, (uint32_t)k)) { reply("\"error\": \"unwritable\""); continue; }
            for (size_t i = 0; i < k; i++) { unsigned c; sscanf(hex + 2 * i, "%2x", &c); G8(v + i) = (uint8_t)c; }
            g_poked++;
            rh_log("bot: POKE %u bytes at %08x (frame %u) — no longer a pure replay\n", (unsigned)k, v, g_frame);
            reply("\"ok\": 1");
        } else if (!strcmp(cmd, "shot")) {
            char tag[64] = "bot"; jstr(line, "tag", tag, sizeof tag);
            rh_shot(tag);
            reply("\"ok\": 1");
        } else if (!strcmp(cmd, "dump")) {
            char tag[64] = "bot", p[MAX_PATH]; jstr(line, "tag", tag, sizeof tag);
            snprintf(p, sizeof p, "%s\\state_%s.rhs", O.rundir, tag);
            rh_state_dump(p);
            reply("\"ok\": 1");
        } else if (!strcmp(cmd, "hash")) {
            rh_hash_t h; rh_state_hash(&h);
            char b[128]; snprintf(b, sizeof b, "\"full\": \"%016llx\", \"sem\": \"%016llx\"", (unsigned long long)h.full, (unsigned long long)h.semantic);
            reply(b);
        } else if (!strcmp(cmd, "quit")) {
            reply("\"ok\": 1");
            rh_finish("bot-quit");
        } else {
            reply("\"error\": \"unknown command\"");
        }
    }
}

uint32_t rh_bot_poked(void) { return g_poked; }

void rh_bot_install(void)
{
    if (!O.bot) return;
    char host[128]; snprintf(host, sizeof host, "%s", O.bot);
    char *c = strchr(host, ':');
    if (!c) rh_fatal("--bot HOST:PORT");
    *c = 0;
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((u_short)atoi(c + 1)); sa.sin_addr.s_addr = inet_addr(host);
    for (int tries = 0; tries < 100; tries++) {
        g_s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (connect(g_s, (struct sockaddr *)&sa, sizeof sa) == 0) break;
        closesocket(g_s); g_s = INVALID_SOCKET;
        Sleep(100);
    }
    if (g_s == INVALID_SOCKET) rh_fatal("bot: cannot connect to %s", O.bot);
    int one = 1; setsockopt(g_s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    rh_log("bot: connected to %s\n", O.bot);
    serve();
}

void rh_bot_frame(void)
{
    if (g_s == INVALID_SOCKET) return;
    if (g_run_left && --g_run_left) return;
    serve();
}
