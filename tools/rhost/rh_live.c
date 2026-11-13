/* rh_live.c — --live PORT: drive retail Lords2 interactively, at a frame boundary, over a socket.
 *
 * Ported from openrecet's rh_live.c (the Kirby v2 playbook on rhost: /opt/src/kirbys-adventure/tas/PLAYBOOK.md).
 * rhost becomes a server on 127.0.0.1:PORT (0 = any free port → <run>\live.port). One text line in, one line out
 * ("ok ..." / "err ..."). The client (tools/drive/drive.py, the `rd` CLI) is stateless; this process is its warm
 * cache — everything it holds can be rebuilt by replaying the movie. Retail runs as a coroutine on its own fixed
 * stack (rhost.c rh_step: one frame, boundary to boundary); this server runs on the host stack.
 *
 *   ping                                    frame / tick / screen / state / rng / epoch / poked
 *   run SEG,SEG.. [u=P] [a=P] [s=P,P..] [e=K]
 *                                           SEG = wN (run N frames) | eM:W:L (post a window message at this boundary:
 *                                           the next frame (= g_frame) is the first to see it; L = pX/pY: MAKELPARAM of programs
 *                                           X, Y evaluated now — input that reads state). Stop when program u holds
 *                                           ("until") or a holds ("avoid"), checked after every frame; sample the s
 *                                           programs every K frames (default: at the end)
 *   r VA LEN / w VA HEX                     read / poke memory (a poke is not in the movie: `poked` is set)
 *   prog ID tok tok ..                      define RPN program ID (0..255); eval ID,ID..
 *   snap | restore ID | free ID.. | freeall savestates (rh_snap.c)
 *   diff ID [max=N] [lo=VA] [hi=VA]         dwords that differ from savestate ID (mark → act → diff)
 *   shot TAG                                <run>\shot_TAG.png + frame_TAG.idx (the presented frame: software DirectDraw)
 *   hash | stats | movie | dump PATH        state hashes / counters / the input movie since boot / .rhs dump
 *   explore g=P c=P,P.. [a=P] acts=A|A|.. [t=MS] [refine=MS] [seed=N] [maxlen=N] [sticky=PCT] [apply=1]
 *           [score=P] [greed=G] [hold=N [hacts=A|A..] [hgoal=0]]
 *                                           exploration on savestates: an action is SEG+SEG.. (`+`-separated)
 *   quit                                    end the run (RESULT line, exit)
 *
 * Programs are postfix: numbers (dec / 0x hex / float), `frame`, memory reads that pop an address (r8 r16 r32 s8 s16
 * s32 f32 f64), + - * / // % & | ^ << >> ~ < <= > >= == != and or not neg abs min max sel(c a b) sqrt sin cos atan2
 * hypot pow. Values are doubles. The driver compiles infix + names to this.
 *
 * The movie: every input event retail has been given since boot (the --trace prefix's and this session's), as a
 * persistent list whose head is part of each savestate — a restore rewinds the movie with the state. `movie` returns
 * it; it is a flat rhost trace ({"f","m","w","l"}): replayed in a fresh rhost it reaches this state.
 *
 * explore (Kirby v2 search): keep one savestate per CELL (the tuple of the c programs); pick a cell (rarely picked
 * and fresh ones first; with score=, in proportion to e^(greed·normalised score)), restore it, run 2..maxlen random
 * actions (sticky), file every state passed through under its cell when the cell is new or was reached in fewer
 * frames (or a better score); stop when g holds, then keep expanding cells reached earlier than the best arrival for
 * `refine` ms. a prunes a branch. hold=N: an arrival counts only when some hold action survives N more frames with a
 * never true (and g still true unless hgoal=0). Cell checks happen at action boundaries, goal / avoid every frame. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "rhost.h"
#include <math.h>
#include <stddef.h>

/* ---------------- string builder ---------------- */
typedef struct { char *p; size_t n, cap; } sb_t;
static void sb_put(sb_t *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2 + 256; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void sb_f(sb_t *b, const char *fmt, ...)
{
    char t[1024]; va_list ap; va_start(ap, fmt); int n = vsnprintf(t, sizeof t, fmt, ap); va_end(ap);
    if (n > 0) sb_put(b, t, (size_t)(n < (int)sizeof t ? n : (int)sizeof t - 1));
}

/* ---------------- the movie ---------------- */
typedef struct mnode { struct mnode *parent; uint32_t frame, m, w, l; } mnode_t;
static mnode_t *g_mhead; static mnode_t *g_mchunk; static uint32_t g_mleft; static uint64_t g_mnodes;
static int g_poked; static uint32_t g_epoch = 1;
void rh_live_note_event(uint32_t frame, uint32_t m, uint32_t w, uint32_t l)   /* rh_win.c inject(): every input event */
{
    if (!g_mleft) { g_mchunk = malloc(65536 * sizeof(mnode_t)); g_mleft = 65536; if (!g_mchunk) rh_fatal("movie: out of memory"); }
    mnode_t *n = g_mchunk++; g_mleft--; g_mnodes++;
    n->parent = g_mhead; n->frame = frame; n->m = m; n->w = w; n->l = l;
    g_mhead = n;
}
static mnode_t **movie_list(mnode_t *head, mnode_t *stop, size_t *n)   /* the nodes after `stop` (exclusive), oldest first */
{
    size_t k = 0;
    for (mnode_t *m = head; m && m != stop; m = m->parent) k++;
    mnode_t **v = malloc((k ? k : 1) * sizeof *v);
    size_t i = k;
    for (mnode_t *m = head; m && m != stop; m = m->parent) v[--i] = m;
    *n = k;
    return v;
}

/* ---------------- what the last frame drew / tested: screen text and hotspots ----------------
 * Retail's text lines end in FUN_00402637 (buf, x, y, …) or FUN_004025d7 (centred: buf, x, y, width, …); its menu / HUD
 * input handlers test their hotspot tables with FUN_0040e3ee (click: ox, oy, table, count) and FUN_0040e5e7 (hover).
 * Their call sites are redirected to thunks that note the arguments and jump on; the C part runs on a private stack and
 * writes no retail memory (a live session's state stays a plain replay's: `rd verify`). Rows are per frame (cleared
 * when a frame starts); text a screen drew once stays in `g_text` until the screen id changes. */
typedef struct { uint8_t kind; int32_t a[4], b[2]; uint16_t len; char s[100]; } lrec_t;   /* b: localized (group, string) */
#define MAX_HOT 96
#define MAX_TXT 192
static lrec_t g_hot[MAX_HOT], g_txt[MAX_TXT]; static int g_nhot, g_ntxt, g_txt_screen = -1, g_rec_stale;
uint32_t g_rec_esp, g_rec_stack;
__attribute__((used)) void __cdecl rec_record(uint32_t kind)
{
    const int32_t *a = (const int32_t *)(uintptr_t)g_rec_esp;      /* [0] ret, [1..] args */
    if (kind == 2 || kind == 3) {                                  /* hotspot table: (ox, oy, table, count) */
        for (int i = 0; i < g_nhot; i++) if (g_hot[i].a[2] == a[3] && g_hot[i].a[0] == a[1] && g_hot[i].a[1] == a[2]) return;
        if (g_nhot >= MAX_HOT) return;
        lrec_t *r = &g_hot[g_nhot++]; r->kind = (uint8_t)kind; r->a[0] = a[1]; r->a[1] = a[2]; r->a[2] = a[3]; r->a[3] = a[4]; r->b[0] = r->b[1] = 0; r->len = 0;
        return;
    }
    /* kinds 0 / 1: (buf, x, y, ...); 4 / 5 / 6: localized (group, string, x, y, ...); 7: wrapped buffer (buf, x, y, ...) */
    int loc = kind >= 4 && kind <= 6;
    const char *str = loc ? "" : (const char *)(uintptr_t)a[1];
    int32_t x = loc ? a[3] : a[2], y = loc ? a[4] : a[3];
    if (!loc && (uint32_t)a[1] < 0x400000u) return;
    if ((int)G8(L2_SCREEN) != g_txt_screen) { g_ntxt = 0; g_txt_screen = G8(L2_SCREEN); }   /* a new screen: its own rows (a panel draws in the frame it opens) */
    int slot = -1;                                                 /* one row per position: a redraw replaces it */
    for (int i = 0; i < g_ntxt; i++) if (g_txt[i].a[0] == x && g_txt[i].a[1] == y) { slot = i; break; }
    if (slot < 0) { if (g_ntxt >= MAX_TXT) return; slot = g_ntxt++; }
    lrec_t *r = &g_txt[slot]; r->kind = (uint8_t)kind; r->a[0] = x; r->a[1] = y; r->a[2] = kind == 1 ? a[4] : 0; r->a[3] = (int32_t)g_frame;
    r->b[0] = loc ? a[1] : 0; r->b[1] = loc ? a[2] : 0;
    uint16_t n = 0; while (n < sizeof r->s && str[n]) n++;
    memcpy(r->s, str, n); r->len = n;
}
#define REC_THUNK(name, k, target) \
    __attribute__((naked)) static void name(void) { \
        __asm__("movl %esp, _g_rec_esp\n\t movl _g_rec_stack, %esp\n\t pushl $" #k "\n\t call _rec_record\n\t" \
                "movl _g_rec_esp, %esp\n\t movl $" #target ", %eax\n\t jmp *%eax"); }
REC_THUNK(rec_text, 0, 0x402637)
REC_THUNK(rec_text_c, 1, 0x4025d7)
REC_THUNK(rec_hot_click, 2, 0x40e3ee)
REC_THUNK(rec_hot_hover, 3, 0x40e5e7)
REC_THUNK(rec_loc_line, 4, 0x402d37)
REC_THUNK(rec_loc_centred, 5, 0x402c5e)
REC_THUNK(rec_loc_wrap, 6, 0x40328e)
REC_THUNK(rec_buf_wrap, 7, 0x40352f)
void rh_live_frame_begin(void)                                     /* rhost.c rh_step: a frame starts */
{
    g_nhot = 0; g_rec_stale = 0;
}
static void rec_install(void)
{
    uint8_t *st = VirtualAlloc(NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    g_rec_stack = (uint32_t)(uintptr_t)st + 0x10000 - 64;
    int a = redirect_calls(0x402637u, rec_text), b = redirect_calls(0x4025d7u, rec_text_c);
    int c = redirect_calls(0x40e3eeu, rec_hot_click), d = redirect_calls(0x40e5e7u, rec_hot_hover);
    int e = redirect_calls(0x402d37u, rec_loc_line), f = redirect_calls(0x402c5eu, rec_loc_centred);
    int g = redirect_calls(0x40328eu, rec_loc_wrap), h = redirect_calls(0x40352fu, rec_buf_wrap);
    rh_log("live: screen text from %d + %d call sites (localized %d + %d + %d, wrapped %d), hotspots from %d + %d\n", a, b, e, f, g, h, c, d);
}

/* ---------------- programs (postfix) ---------------- */
enum { OP_NUM, OP_R8, OP_R16, OP_R32, OP_S8, OP_S16, OP_S32, OP_F32, OP_F64, OP_FRAME, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_FDIV,
       OP_MOD, OP_AND, OP_OR, OP_XOR, OP_SHL, OP_SHR, OP_INV, OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ, OP_NE, OP_LAND, OP_LOR, OP_LNOT,
       OP_NEG, OP_ABS, OP_MIN, OP_MAX, OP_SEL, OP_SQRT, OP_SIN, OP_COS, OP_ATAN2, OP_HYPOT, OP_POW, OP_SLOT, OP_SLOTI, OP_CNT };
static const char *const OPN[] = { "", "r8", "r16", "r32", "s8", "s16", "s32", "f32", "f64", "frame", "+", "-", "*", "/", "//", "%",
                                   "&", "|", "^", "<<", ">>", "~", "<", "<=", ">", ">=", "==", "!=", "and", "or", "not", "neg", "abs",
                                   "min", "max", "sel", "sqrt", "sin", "cos", "atan2", "hypot", "pow", "slot", "sloti", "cnt" };
typedef struct { uint8_t op; double v; } ins_t;
typedef struct { int n; ins_t *c; } prog_t;
#define NPROG 256
static prog_t g_prog[NPROG];
static long g_bad_reads;
static int mem_ok(uint32_t a, uint32_t n)
{
    if (a >= RT_IMG_LO && a + n <= RT_IMG_HI && a + n >= a) return 1;
    MEMORY_BASIC_INFORMATION mi;
    for (uint32_t p = a & ~0xfffu; p < a + n; ) {
        if (!VirtualQuery((void *)(uintptr_t)p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
        uint32_t hi = (uint32_t)(uintptr_t)mi.BaseAddress + (uint32_t)mi.RegionSize;
        if (hi <= p) return 0;
        p = hi;
    }
    return 1;
}
/* records: `BASE STRIDE LO HI cnt:N <N body tokens>` = how many slots LO..HI make the body non-zero; inside the body
 * `slot` pushes the slot's address (BASE + i * STRIDE) and `sloti` its index i (a count over 150 pieces is ~20 tokens) */
static double g_slot, g_sloti;
static double vm_run(const ins_t *c, int n);
static double vm_eval(const prog_t *p) { return vm_run(p->c, p->n); }
static double vm_run(const ins_t *c, int n)
{
    double st[64]; int sp = 0;
    for (int i = 0; i < n; i++) {
        const ins_t *x = &c[i];
        #define A st[sp - 2]
        #define B st[sp - 1]
        #define BIN(expr) do { if (sp < 2) return 0; double r_ = (expr); sp--; st[sp - 1] = r_; } while (0)
        #define I64(v) ((int64_t)(v))
        switch (x->op) {
        case OP_NUM: if (sp < 64) st[sp++] = x->v; break;
        case OP_FRAME: if (sp < 64) st[sp++] = g_frame; break;
        case OP_R8: case OP_R16: case OP_R32: case OP_S8: case OP_S16: case OP_S32: case OP_F32: case OP_F64: {
            if (sp < 1) return 0;
            uint32_t a = (uint32_t)(int64_t)st[sp - 1], n = x->op == OP_F64 ? 8 : (x->op == OP_R8 || x->op == OP_S8) ? 1 : (x->op == OP_R16 || x->op == OP_S16) ? 2 : 4;
            if (!mem_ok(a, n)) { g_bad_reads++; st[sp - 1] = 0; break; }
            const void *q = (const void *)(uintptr_t)a;
            st[sp - 1] = x->op == OP_R8 ? *(const uint8_t *)q : x->op == OP_S8 ? *(const int8_t *)q : x->op == OP_R16 ? *(const uint16_t *)q :
                         x->op == OP_S16 ? *(const int16_t *)q : x->op == OP_R32 ? *(const uint32_t *)q : x->op == OP_S32 ? *(const int32_t *)q :
                         x->op == OP_F32 ? (double)*(const float *)q : *(const double *)q;
            break;
        }
        case OP_ADD: BIN(A + B); break;
        case OP_SUB: BIN(A - B); break;
        case OP_MUL: BIN(A * B); break;
        case OP_DIV: BIN(B != 0 ? A / B : 0); break;
        case OP_FDIV: BIN(B != 0 ? floor(A / B) : 0); break;
        case OP_MOD: BIN(B != 0 ? A - floor(A / B) * B : 0); break;
        case OP_AND: BIN((double)(I64(A) & I64(B))); break;
        case OP_OR: BIN((double)(I64(A) | I64(B))); break;
        case OP_XOR: BIN((double)(I64(A) ^ I64(B))); break;
        case OP_SHL: BIN((double)(I64(A) << (I64(B) & 63))); break;
        case OP_SHR: BIN((double)(I64(A) >> (I64(B) & 63))); break;
        case OP_LT: BIN(A < B); break;
        case OP_LE: BIN(A <= B); break;
        case OP_GT: BIN(A > B); break;
        case OP_GE: BIN(A >= B); break;
        case OP_EQ: BIN(A == B); break;
        case OP_NE: BIN(A != B); break;
        case OP_LAND: BIN(A != 0 && B != 0); break;
        case OP_LOR: BIN(A != 0 || B != 0); break;
        case OP_MIN: BIN(A < B ? A : B); break;
        case OP_MAX: BIN(A > B ? A : B); break;
        case OP_INV: if (sp < 1) return 0; st[sp - 1] = (double)(uint32_t)~(uint32_t)I64(st[sp - 1]); break;
        case OP_LNOT: if (sp < 1) return 0; st[sp - 1] = st[sp - 1] == 0; break;
        case OP_NEG: if (sp < 1) return 0; st[sp - 1] = -st[sp - 1]; break;
        case OP_ABS: if (sp < 1) return 0; st[sp - 1] = fabs(st[sp - 1]); break;
        case OP_SQRT: if (sp < 1) return 0; st[sp - 1] = st[sp - 1] > 0 ? sqrt(st[sp - 1]) : 0; break;
        case OP_SIN: if (sp < 1) return 0; st[sp - 1] = sin(st[sp - 1]); break;
        case OP_COS: if (sp < 1) return 0; st[sp - 1] = cos(st[sp - 1]); break;
        case OP_ATAN2: BIN(atan2(A, B)); break;
        case OP_HYPOT: BIN(sqrt(A * A + B * B)); break;
        case OP_POW: BIN((A < 0 && B != floor(B)) || (A == 0 && B < 0) ? 0 : pow(A, B)); break;
        case OP_SEL: if (sp < 3) return 0; { double r = st[sp - 3] != 0 ? st[sp - 2] : st[sp - 1]; sp -= 2; st[sp - 1] = r; } break;
        case OP_SLOT: if (sp < 64) st[sp++] = g_slot; break;
        case OP_SLOTI: if (sp < 64) st[sp++] = g_sloti; break;
        case OP_CNT: {
            int body = (int)x->v;
            if (sp < 4 || i + body >= n + 0 || body < 0 || i + 1 + body > n) return 0;
            double base = st[sp - 4], stride = st[sp - 3]; int64_t lo = (int64_t)st[sp - 2], hi = (int64_t)st[sp - 1]; sp -= 4;
            double os = g_slot, oi = g_sloti, cnt = 0;
            for (int64_t k = lo; k <= hi && k - lo < 4096; k++) { g_slot = base + (double)k * stride; g_sloti = (double)k; if (vm_run(c + i + 1, body) != 0) cnt++; }
            g_slot = os; g_sloti = oi;
            st[sp++] = cnt; i += body;
            break;
        }
        }
        #undef A
        #undef B
        #undef BIN
        #undef I64
    }
    return sp ? st[sp - 1] : 0;
}
static int prog_define(int id, char **tok, int ntok, sb_t *err)
{
    ins_t *c = malloc((ntok ? ntok : 1) * sizeof *c);
    for (int i = 0; i < ntok; i++) {
        int op = -1;
        for (int k = 1; k < (int)(sizeof OPN / sizeof OPN[0]); k++) if (!strcmp(tok[i], OPN[k])) { op = k; break; }
        if (op < 0 && !strncmp(tok[i], "cnt:", 4)) { c[i].op = OP_CNT; c[i].v = atof(tok[i] + 4); continue; }
        if (op < 0) {
            char *e; double v = (tok[i][0] == '0' && (tok[i][1] == 'x' || tok[i][1] == 'X')) ? (double)strtoull(tok[i], &e, 16) : strtod(tok[i], &e);
            if (*e || e == tok[i]) { sb_f(err, "bad token '%s'", tok[i]); free(c); return -1; }
            c[i].op = OP_NUM; c[i].v = v;
        } else { c[i].op = (uint8_t)op; c[i].v = 0; }
    }
    free(g_prog[id].c); g_prog[id].c = c; g_prog[id].n = ntok;
    return 0;
}
static int pid_of(const char *s) { char *e; long v = strtol(s, &e, 10); return (*e || e == s || v < 0 || v >= NPROG || !g_prog[v].c) ? -1 : (int)v; }
static int pid_list(const char *s, int *out, int max)
{
    int n = 0; char b[512]; snprintf(b, sizeof b, "%s", s);
    for (char *t = strtok(b, ","); t; t = strtok(NULL, ",")) { if (n == max) return -1; int p = pid_of(t); if (p < 0) return -1; out[n++] = p; }
    return n;
}

/* ---------------- segments: waits and events ---------------- */
typedef struct { uint8_t ev; uint32_t n; uint32_t m, w, l; int16_t px, py; } seg_t;   /* ev=0: wait n frames; ev=1: post (m, w, l | MAKELPARAM(px, py)) */
static int parse_segs(const char *s, seg_t **out, char sep)
{
    int n = 0, cap = 16; seg_t *v = malloc(cap * sizeof *v);
    while (*s) {
        char *e; seg_t g; memset(&g, 0, sizeof g); g.px = g.py = -1;
        if (*s == 'w') { g.n = (uint32_t)strtoul(s + 1, &e, 10); if (e == s + 1) goto bad; }
        else if (*s == 'e') {
            g.ev = 1;
            g.m = (uint32_t)strtoul(s + 1, &e, 10); if (e == s + 1 || *e != ':') goto bad;
            g.w = (uint32_t)strtoul(e + 1, &e, 10); if (*e != ':') goto bad;
            if (e[1] == 'p') {
                long x = strtol(e + 2, &e, 10); if (*e != '/' || e[1] != 'p') goto bad;
                long y = strtol(e + 2, &e, 10);
                if (x < 0 || x >= NPROG || !g_prog[x].c || y < 0 || y >= NPROG || !g_prog[y].c) goto bad;
                g.px = (int16_t)x; g.py = (int16_t)y;
            } else g.l = (uint32_t)strtoul(e + 1, &e, 10);
        } else goto bad;
        if (*e && *e != sep) goto bad;
        if (n == cap) { cap *= 2; v = realloc(v, cap * sizeof *v); }
        v[n++] = g;
        s = *e ? e + 1 : e;
    }
    *out = v;
    return n;
bad:
    free(v);
    return -1;
}
static uint32_t seg_l(const seg_t *g)
{
    if (g->px < 0) return g->l;
    int x = (int)vm_eval(&g_prog[g->px]), y = (int)vm_eval(&g_prog[g->py]);
    if (x < 0) x = 0; if (x > 639) x = 639; if (y < 0) y = 0; if (y > 479) y = 479;
    return ((uint32_t)y << 16) | (uint32_t)x;
}
static void post(const seg_t *g) { rh_win_inject(g->m, g->w, seg_l(g)); }

static double now_ms(void)
{
    static LARGE_INTEGER f; LARGE_INTEGER t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}
static void status(sb_t *o)
{
    sb_f(o, "frame=%u tick=%u screen=%u state=%u rng=%08x/%08x epoch=%u poked=%d", g_frame, G32(L2_SIM_TICK), G8(L2_SCREEN),
         G32(L2_ENGINE_STATE), G32(L2_RNG_A), G32(L2_RNG_B), g_epoch, g_poked);
}

/* ---------------- explore ---------------- */
typedef struct { uint64_t key; int snap; uint32_t frames, chosen, born; mnode_t *head; double v[6], score; } cell_t;
static uint64_t g_xs;
static uint32_t xrand(void) { g_xs ^= g_xs << 13; g_xs ^= g_xs >> 7; g_xs ^= g_xs << 17; return (uint32_t)(g_xs >> 16); }
static double xunit(void) { return xrand() / 4294967296.0; }
static uint64_t cell_key(const int *cp, int nc, double *vals)
{
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < nc; i++) {
        double v = vm_eval(&g_prog[cp[i]]); if (i < 6) vals[i] = v;
        uint64_t u; memcpy(&u, &v, 8); if (v == 0) u = 0;
        h ^= u; h *= 1099511628211ull; h ^= h >> 29;
    }
    return h ? h : 1;
}
/* the path from the root to HEAD as segments: the events (frame-relative) and the waits between, `frames` long */
static void path_segs(mnode_t *head, mnode_t *root_head, uint32_t root_frame, uint32_t frames, sb_t *out)
{
    size_t n; mnode_t **v = movie_list(head, root_head, &n);
    uint32_t at = root_frame, end = root_frame + frames; int first = 1;   /* an event of frame F was posted at the boundary where g_frame == F */
    for (size_t i = 0; i <= n; i++) {
        uint32_t f = i < n ? v[i]->frame : end;
        if (f > end) f = end;
        if (f > at) { sb_f(out, "%sw%u", first ? "" : ",", f - at); first = 0; at = f; }
        if (i < n) { sb_f(out, "%se%u:%u:%u", first ? "" : ",", v[i]->m, v[i]->w, v[i]->l); first = 0; }
    }
    free(v);
}
static long evict_cells(cell_t *cells, int n, const cell_t *keep)
{
    uint64_t sum = 0; int live = 0; long ev = 0;
    for (int i = 1; i < n; i++) if (cells[i].snap) { sum += cells[i].chosen; live++; }
    uint32_t thr = live ? (uint32_t)(sum / (uint64_t)live) : 0;
    for (int i = 1; i < n; i++) if (cells[i].snap && cells[i].chosen >= thr && &cells[i] != keep) { rh_snap_free(cells[i].snap); cells[i].snap = 0; ev++; }
    return ev;
}
typedef struct { int n, goal, avoid, need_goal, nact; seg_t **acts; const int *alen; } hold_t;
static int hold_try(const hold_t *h, int which, uint64_t *ticks)   /* 1 = the tail survives N frames */
{
    int k = 0;
    while (k < h->n) {
        int progressed = 0;
        for (int s = 0; s < h->alen[which] && k < h->n; s++) {
            const seg_t *g = &h->acts[which][s];
            if (g->ev) { post(g); continue; }
            for (uint32_t i = 0; i < g->n && k < h->n; i++) {
                rh_step(); k++; (*ticks)++; progressed = 1;
                if (h->avoid >= 0 && vm_eval(&g_prog[h->avoid]) != 0) return 0;
            }
        }
        if (!progressed) break;
    }
    return !h->need_goal || vm_eval(&g_prog[h->goal]) != 0;
}
static void do_explore(int goal, int avoid, const int *cp, int nc, seg_t **acts, const int *alen, int nact, double tlimit, double refine,
                       int maxlen, double sticky, int apply, int score, double greed, const hold_t *hold, sb_t *out)
{
    int root = rh_snap_take();
    if (!root) { sb_f(out, "err no savestate (pool exhausted?)"); return; }
    mnode_t *root_head = g_mhead; uint32_t root_frame = g_frame;
    int cap = 4096, n = 0, mapcap = 16384; cell_t *cells = malloc(cap * sizeof *cells); int *map = calloc(mapcap, sizeof *map);
    #define FIND(k, slot) do { slot = (uint32_t)((k) * 11400714819323198485ull >> 40) % (uint32_t)mapcap; \
                               while (map[slot] && cells[map[slot] - 1].key != (k)) slot = (slot + 1) % (uint32_t)mapcap; } while (0)
    double vals[6] = { 0 }; uint64_t k0 = cell_key(cp, nc, vals); uint32_t s0; FIND(k0, s0);
    double sc0 = score >= 0 ? vm_eval(&g_prog[score]) : 0, smin = sc0, smax = sc0;
    cells[0] = (cell_t){ k0, root, 0, 0, 0, root_head, { vals[0], vals[1], vals[2], vals[3], vals[4], vals[5] }, sc0 }; map[s0] = 1; n = 1;
    #define WEIGHT(i) ((1.0 / sqrt(1.0 + cells[i].chosen)) * (it - cells[i].born <= 2 ? 3.0 : 1.0) * \
                       (score >= 0 && smax > smin ? exp(greed * (cells[i].score - smin) / (smax - smin)) : 1.0))
    double t0 = now_ms(), tbest = 0; uint32_t best = 0, it = 0; uint64_t ticks = 0; mnode_t *best_head = NULL; long pruned = 0, evicted = 0, rejected = 0, held = 0; int best_tail = -1;
    uint32_t hn = hold && hold->n > 0 ? (uint32_t)hold->n : 0;
    while (1) {
        double now = now_ms();
        if (!best && now - t0 > tlimit) break;
        if (best && now - tbest > refine) break;
        it++;
        double tw = 0;
        for (int i = 0; i < n; i++) { if (!cells[i].snap || (best && cells[i].frames + 1 >= best)) continue; tw += WEIGHT(i); }
        if (tw <= 0) break;
        double pick = xunit() * tw; int ei = -1;
        for (int i = 0; i < n; i++) {
            if (!cells[i].snap || (best && cells[i].frames + 1 >= best)) continue;
            ei = i; pick -= WEIGHT(i); if (pick <= 0) break;
        }
        cells[ei].chosen++;
        rh_snap_restore(cells[ei].snap);
        uint32_t f = cells[ei].frames; int a = (int)(xrand() % (uint32_t)nact), len = 1 + (int)(xrand() % (uint32_t)maxlen), stop = 0;
        for (int k = 0; k < len && !stop; k++) {
            if (k && xunit() > sticky) a = (int)(xrand() % (uint32_t)nact);
            for (int s = 0; s < alen[a] && !stop; s++) {
                if (acts[a][s].ev) { post(&acts[a][s]); continue; }
                for (uint32_t i = 0; i < acts[a][s].n && !stop; i++) {
                    rh_step(); f++; ticks++;
                    if (vm_eval(&g_prog[goal]) != 0) {
                        stop = 1;
                        if (best && f >= best) break;
                        int tail = -1;
                        if (hn) {
                            int hs = rh_snap_take();
                            if (!hs) { evicted += evict_cells(cells, n, &cells[ei]); hs = rh_snap_take(); }
                            if (!hs) { rejected++; break; }
                            for (int t = 0; t < hold->nact && tail < 0; t++) {
                                if (t) rh_snap_restore(hs);
                                if (hold_try(hold, t, &ticks)) tail = t;
                            }
                            rh_snap_free(hs);
                            if (tail < 0) { rejected++; break; }
                            held++;
                        }
                        best = f; best_head = g_mhead; best_tail = tail; tbest = now_ms();
                    }
                    else if (avoid >= 0 && vm_eval(&g_prog[avoid]) != 0) { pruned++; stop = 1; }
                    else if (best && f + 1 >= best) stop = 1;
                }
            }
            if (stop) break;
            uint64_t key = cell_key(cp, nc, vals); uint32_t slot; FIND(key, slot);
            cell_t *c = map[slot] ? &cells[map[slot] - 1] : NULL;
            double sc = score >= 0 ? vm_eval(&g_prog[score]) : 0;
            if (c && c->snap && !(sc > c->score || (sc == c->score && f < c->frames))) continue;
            int id = rh_snap_take();
            if (!id) { evicted += evict_cells(cells, n, c); id = rh_snap_take(); if (!id) continue; }
            if (c) { if (c->snap && c->snap != root) rh_snap_free(c->snap); c->chosen /= 2; }
            else {
                if (n == cap) { cap *= 2; cells = realloc(cells, cap * sizeof *cells); }
                if ((n + 1) * 2 > mapcap) {
                    mapcap *= 2; free(map); map = calloc(mapcap, sizeof *map);
                    for (int i = 0; i < n; i++) { uint32_t s2; FIND(cells[i].key, s2); map[s2] = i + 1; }
                    FIND(key, slot);
                }
                c = &cells[n++]; map[slot] = n; c->key = key; c->chosen = 0;
                for (int i = 0; i < 6; i++) c->v[i] = vals[i];
            }
            c->snap = id; c->frames = f; c->born = it; c->head = g_mhead; c->score = sc;
            if (sc < smin) smin = sc;
            if (sc > smax) smax = sc;
        }
    }
    double ms = now_ms() - t0;
    rh_snap_restore(root);
    sb_f(out, "ok found=%d frames=%u cells=%d iters=%u ticks=%llu ms=%.0f pruned=%ld evicted=%ld epoch=%u", best != 0, best ? best + hn : 0, n, it,
         (unsigned long long)ticks, ms, pruned, evicted, g_epoch);
    if (hn) sb_f(out, " hold=%u held=%ld rejected=%ld", hn, held, rejected);
    if (best && hn) sb_f(out, " arrive=%u tail=%d", best, best_tail);
    mnode_t *apply_head = NULL; uint32_t apply_frames = 0;
    if (best) {
        sb_t path = { 0 }; path_segs(best_head, root_head, root_frame, best + hn, &path);
        sb_f(out, " path="); sb_put(out, path.p ? path.p : "", path.n);
        free(path.p);
        if (apply) { apply_head = best_head; apply_frames = best + hn; }
    } else {
        sb_f(out, " newest=");
        int shown = 0;
        for (int i = n - 1; i >= 0 && shown < 8; i--, shown++) {
            sb_f(out, "%s%u@", shown ? ";" : "", cells[i].frames);
            for (int k = 0; k < nc && k < 6; k++) sb_f(out, "%s%.6g", k ? "/" : "", cells[i].v[k]);
        }
        if (score >= 0) {
            int bi = 0;
            for (int i = 1; i < n; i++)
                if (cells[i].score > cells[bi].score || (cells[i].score == cells[bi].score && cells[i].frames < cells[bi].frames)) bi = i;
            sb_f(out, " score0=%.9g bscore=%.9g bframes=%u", sc0, cells[bi].score, cells[bi].frames);
            if (cells[bi].frames) {
                sb_t path = { 0 }; path_segs(cells[bi].head, root_head, root_frame, cells[bi].frames, &path);
                sb_f(out, " bpath="); sb_put(out, path.p ? path.p : "", path.n); free(path.p);
                if (apply == 2) { apply_head = cells[bi].head; apply_frames = cells[bi].frames; }
            }
        }
    }
    if (apply_head || apply_frames) {                    /* replay the path's events at their frames (the movie records them again) */
        size_t m; mnode_t **v = movie_list(apply_head, root_head, &m);
        uint32_t end = root_frame + apply_frames; size_t i = 0;
        for (;;) {
            while (i < m && v[i]->frame <= g_frame) { rh_win_inject(v[i]->m, v[i]->w, v[i]->l); i++; }
            if (g_frame >= end) break;
            rh_step();
        }
        free(v);
        sb_f(out, " applied=1 holds=%d avoid=%d frame=%u", vm_eval(&g_prog[goal]) != 0, avoid >= 0 && vm_eval(&g_prog[avoid]) != 0, g_frame);
    }
    for (int i = 0; i < n; i++) if (cells[i].snap && cells[i].snap != root) rh_snap_free(cells[i].snap);
    rh_snap_free(root);
    free(cells); free(map);
    #undef FIND
    #undef WEIGHT
}

/* ---------------- the server ---------------- */
static SOCKET g_ls = INVALID_SOCKET, g_cs = INVALID_SOCKET;
static char g_in[1 << 16]; static int g_in_n;
void rh_live_install(void)
{
    WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) rh_fatal("live: WSAStartup failed");
    g_ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((u_short)O.live_port);
    if (g_ls == INVALID_SOCKET || bind(g_ls, (struct sockaddr *)&a, sizeof a) || listen(g_ls, 4))
        rh_fatal("live: cannot listen on 127.0.0.1:%d (err %d)", O.live_port, WSAGetLastError());
    int al = sizeof a; getsockname(g_ls, (struct sockaddr *)&a, &al);
    O.live_port = ntohs(a.sin_port);
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\live.port.tmp", O.rundir);
    char q[MAX_PATH]; snprintf(q, sizeof q, "%s\\live.port", O.rundir);
    FILE *f = fopen(p, "w"); if (f) { fprintf(f, "%d %lu\n", O.live_port, (unsigned long)GetCurrentProcessId()); fclose(f); MoveFileExA(p, q, MOVEFILE_REPLACE_EXISTING); }
    g_xs = 0x9E3779B97F4A7C15ull;
    rec_install();
    rh_log("live: serving on 127.0.0.1:%d\n", O.live_port);
}
void rh_live_register(void) { rh_snap_register("movie.head", &g_mhead, sizeof g_mhead); }
static int wait_readable(SOCKET s)
{
    fd_set f; FD_ZERO(&f); FD_SET(s, &f);
    struct timeval tv = { (long)(O.live_idle ? O.live_idle : 1800), 0 };
    return select(0, &f, NULL, NULL, &tv) > 0;
}
static int read_line(char *out, int cap)
{
    for (;;) {
        for (int i = 0; i < g_in_n; i++)
            if (g_in[i] == '\n') {
                int n = i < cap - 1 ? i : cap - 1;
                memcpy(out, g_in, (size_t)n); out[n] = 0;
                if (n && out[n - 1] == '\r') out[n - 1] = 0;
                memmove(g_in, g_in + i + 1, (size_t)(g_in_n - i - 1)); g_in_n -= i + 1;
                return n;
            }
        if (g_in_n == (int)sizeof g_in) g_in_n = 0;
        if (!wait_readable(g_cs)) rh_finish("live-idle");
        int r = recv(g_cs, g_in + g_in_n, (int)sizeof g_in - g_in_n, 0);
        if (r <= 0) return -1;
        g_in_n += r;
    }
}
static void send_all(const char *p, size_t n)
{
    while (n) { int r = send(g_cs, p, (int)(n > (1 << 20) ? (1 << 20) : n), 0); if (r <= 0) return; p += r; n -= (size_t)r; }
}
static const char *opt(char **tok, int ntok, const char *key)
{
    size_t kl = strlen(key);
    for (int i = 1; i < ntok; i++) if (!strncmp(tok[i], key, kl) && tok[i][kl] == '=') return tok[i] + kl + 1;
    return NULL;
}

static void command(char *line, sb_t *o)
{
    static char *tok[700]; int nt = 0;
    for (char *t = strtok(line, " \t"); t && nt < 700; t = strtok(NULL, " \t")) tok[nt++] = t;
    if (!nt) { sb_f(o, "err empty"); return; }
    const char *c = tok[0];
    if (!strcmp(c, "ping")) { sb_f(o, "ok "); status(o); }
    else if (!strcmp(c, "run")) {
        if (nt < 2) { sb_f(o, "err run SEG,SEG.."); return; }
        seg_t *sg; int ns = parse_segs(tok[1], &sg, ',');
        if (ns < 0) { sb_f(o, "err bad segments"); return; }
        const char *su = opt(tok, nt, "u"), *sa = opt(tok, nt, "a"), *ss = opt(tok, nt, "s"), *se = opt(tok, nt, "e");
        int u = su ? pid_of(su) : -1, a = sa ? pid_of(sa) : -1, sp[32], nsp = ss ? pid_list(ss, sp, 32) : 0;
        if ((su && u < 0) || (sa && a < 0) || nsp < 0) { free(sg); sb_f(o, "err undefined program"); return; }
        uint32_t every = se ? (uint32_t)strtoul(se, 0, 10) : 0, ran = 0;
        const char *why = "end"; sb_t smp = { 0 };
        for (int s = 0; s < ns && why[0] == 'e'; s++) {
            if (sg[s].ev) { post(&sg[s]); continue; }
            for (uint32_t i = 0; i < sg[s].n; i++) {
                rh_step(); ran++;
                if (every && ran % every == 0) {
                    sb_f(&smp, "%s%u", smp.n ? ";" : "", g_frame);
                    for (int k = 0; k < nsp; k++) sb_f(&smp, ":%.9g", vm_eval(&g_prog[sp[k]]));
                }
                if (u >= 0 && vm_eval(&g_prog[u]) != 0) { why = "until"; break; }
                if (a >= 0 && vm_eval(&g_prog[a]) != 0) { why = "avoid"; break; }
            }
        }
        free(sg);
        sb_f(o, "ok ran=%u stop=%s ", ran, why); status(o);
        if (nsp) {
            sb_f(o, " now=");
            for (int k = 0; k < nsp; k++) sb_f(o, "%s%.9g", k ? ":" : "", vm_eval(&g_prog[sp[k]]));
            if (every) { sb_f(o, " samples="); sb_put(o, smp.p ? smp.p : "", smp.n); }
        }
        free(smp.p);
    }
    else if (!strcmp(c, "prog")) {
        int id = nt >= 2 ? atoi(tok[1]) : -1;
        if (id < 0 || id >= NPROG) { sb_f(o, "err prog ID(0..%d) tokens", NPROG - 1); return; }
        sb_t e = { 0 };
        if (prog_define(id, tok + 2, nt - 2, &e)) { sb_f(o, "err %s", e.p); free(e.p); return; }
        sb_f(o, "ok id=%d n=%d", id, nt - 2);
    }
    else if (!strcmp(c, "eval")) {
        int ids[64], n = nt >= 2 ? pid_list(tok[1], ids, 64) : -1;
        if (n < 0) { sb_f(o, "err undefined program"); return; }
        long bad0 = g_bad_reads;
        sb_f(o, "ok v=");
        for (int i = 0; i < n; i++) sb_f(o, "%s%.17g", i ? ":" : "", vm_eval(&g_prog[ids[i]]));
        sb_f(o, " badreads=%ld frame=%u", g_bad_reads - bad0, g_frame);
    }
    else if (!strcmp(c, "r")) {
        uint32_t a = nt >= 3 ? (uint32_t)strtoul(tok[1], 0, 16) : 0, n = nt >= 3 ? (uint32_t)strtoul(tok[2], 0, 0) : 0;
        if (!n || n > (4u << 20) || !mem_ok(a, n)) { sb_f(o, "err unreadable %08x+%u", a, n); return; }
        sb_f(o, "ok ");
        char *h = malloc(n * 2 + 1); static const char X[] = "0123456789abcdef";
        for (uint32_t i = 0; i < n; i++) { uint8_t b = *(const uint8_t *)(uintptr_t)(a + i); h[i * 2] = X[b >> 4]; h[i * 2 + 1] = X[b & 15]; }
        sb_put(o, h, n * 2); free(h);
    }
    else if (!strcmp(c, "w")) {
        uint32_t a = nt >= 3 ? (uint32_t)strtoul(tok[1], 0, 16) : 0; size_t hl = nt >= 3 ? strlen(tok[2]) : 0;
        if (!hl || (hl & 1) || !mem_ok(a, (uint32_t)(hl / 2))) { sb_f(o, "err w VA HEX"); return; }
        for (size_t i = 0; i < hl / 2; i++) { unsigned b; sscanf(tok[2] + i * 2, "%2x", &b); *(volatile uint8_t *)(uintptr_t)(a + i) = (uint8_t)b; }
        g_poked = 1;
        sb_f(o, "ok poked=%u", (unsigned)(hl / 2));
    }
    else if (!strcmp(c, "snap")) {
        int id = rh_snap_take();
        if (!id) sb_f(o, "err savestate pool exhausted"); else sb_f(o, "ok id=%d frame=%u epoch=%u", id, g_frame, g_epoch);
    }
    else if (!strcmp(c, "restore")) {
        int id = nt >= 2 ? atoi(tok[1]) : 0;
        if (rh_snap_restore(id)) sb_f(o, "err no savestate %d (epoch %u)", id, g_epoch); else { g_rec_stale = 1; sb_f(o, "ok "); status(o); }
    }
    else if (!strcmp(c, "free")) { for (int i = 1; i < nt; i++) rh_snap_free(atoi(tok[i])); sb_f(o, "ok"); }
    else if (!strcmp(c, "freeall")) { rh_snap_free_all(); g_epoch++; sb_f(o, "ok epoch=%u", g_epoch); }
    else if (!strcmp(c, "diff")) {
        int id = nt >= 2 ? atoi(tok[1]) : 0;
        const char *sm = opt(tok, nt, "max"), *slo = opt(tok, nt, "lo"), *shi = opt(tok, nt, "hi");
        struct dctx { sb_t *o; long left; } dc = { o, sm ? atol(sm) : 400 };
        void cb(uint32_t va, uint32_t was, uint32_t now, void *u) { struct dctx *d = u; if (d->left-- > 0) sb_f(d->o, "%s%x:%x:%x", d->o->p[d->o->n - 1] == '=' ? "" : ",", va, was, now); }
        sb_f(o, "ok d=");
        long n = rh_snap_diff(id, slo ? (uint32_t)strtoul(slo, 0, 16) : 0, shi ? (uint32_t)strtoul(shi, 0, 16) : 0xffffffffu, cb, &dc);
        if (n < 0) { o->n = 0; sb_f(o, "err no savestate %d", id); return; }
        sb_f(o, " n=%ld frame=%u", n, g_frame);
    }
    else if (!strcmp(c, "shot")) {
        char tag[64]; snprintf(tag, sizeof tag, "%s", nt >= 2 ? tok[1] : "live");
        rh_shot(tag);
        sb_f(o, "ok %s\\shot_%s.png frame=%u", O.rundir, tag, g_frame);
    }
    else if (!strcmp(c, "text") || !strcmp(c, "hot")) {
        int t = c[0] == 't'; const lrec_t *v = t ? g_txt : g_hot; int n = t ? g_ntxt : g_nhot;
        static const char X[] = "0123456789abcdef";
        sb_f(o, "ok frame=%u stale=%d n=%d rows=", g_frame, g_rec_stale, n);
        for (int i = 0; i < n; i++) {
            sb_f(o, "%s%d:%d:%d:%d:%d:%d:%d:", i ? ";" : "", v[i].kind, v[i].a[0], v[i].a[1], v[i].a[2], v[i].a[3], v[i].b[0], v[i].b[1]);
            char h[sizeof v[i].s * 2 + 1];
            for (int k = 0; k < v[i].len; k++) { uint8_t b = (uint8_t)v[i].s[k]; h[k * 2] = X[b >> 4]; h[k * 2 + 1] = X[b & 15]; }
            h[v[i].len * 2] = 0; sb_put(o, h, (size_t)v[i].len * 2);
        }
    }
    else if (!strcmp(c, "hash")) {
        rh_hash_t h; rh_state_hash(&h);
        sb_f(o, "ok full=%016llx sem=%016llx bytes=%u ", (unsigned long long)h.full, (unsigned long long)h.semantic, h.bytes); status(o);
    }
    else if (!strcmp(c, "stats")) { char b[512]; rh_snap_stats(b, sizeof b); sb_f(o, "ok %s movie_nodes=%llu badreads=%ld handles=%ld", b, (unsigned long long)g_mnodes, g_bad_reads, rh_handles_live()); }
    else if (!strcmp(c, "movie")) {
        size_t n; mnode_t **v = movie_list(g_mhead, NULL, &n);
        sb_f(o, "ok frame=%u n=%u poked=%d ev=", g_frame, (unsigned)n, g_poked);
        for (size_t i = 0; i < n; i++) sb_f(o, "%s%u:%u:%u:%u", i ? "," : "", v[i]->frame, v[i]->m, v[i]->w, v[i]->l);
        free(v);
    }
    else if (!strcmp(c, "dump")) { if (nt < 2 || rh_state_dump(tok[1])) sb_f(o, "err dump PATH"); else sb_f(o, "ok"); }
    else if (!strcmp(c, "explore")) {
        const char *sg = opt(tok, nt, "g"), *sc = opt(tok, nt, "c"), *sa = opt(tok, nt, "a"), *sacts = opt(tok, nt, "acts");
        int g = sg ? pid_of(sg) : -1, a = sa ? pid_of(sa) : -1, cp[16], nc = sc ? pid_list(sc, cp, 16) : -1;
        if (g < 0 || nc <= 0 || (sa && a < 0) || !sacts) { sb_f(o, "err explore g=P c=P,.. acts=A|A.. (undefined program?)"); return; }
        seg_t *acts[64]; int alen[64], nact = 0; static char b[1 << 15]; snprintf(b, sizeof b, "%s", sacts);
        for (char *t = strtok(b, "|"); t && nact < 64; t = strtok(NULL, "|")) {
            alen[nact] = parse_segs(t, &acts[nact], '+');
            int frames = 0; for (int s = 0; alen[nact] > 0 && s < alen[nact]; s++) frames += acts[nact][s].ev ? 0 : (int)acts[nact][s].n;
            if (alen[nact] <= 0 || !frames) { sb_f(o, "err bad action '%s' (needs a wait)", t); if (alen[nact] > 0) free(acts[nact]); for (int i = 0; i < nact; i++) free(acts[i]); return; }
            nact++;
        }
        if (!nact) { sb_f(o, "err no actions"); return; }
        const char *shn = opt(tok, nt, "hold"), *sha = opt(tok, nt, "hacts"), *shg = opt(tok, nt, "hgoal");
        seg_t *hacts[64]; int hlen[64], nh = 0;
        hold_t hold = { shn ? atoi(shn) : 0, g, a, shg ? atoi(shg) != 0 : 1, 0, hacts, hlen };
        if (hold.n > 0) {
            static char hb[1 << 14]; snprintf(hb, sizeof hb, "%s", sha ? sha : "w1");
            for (char *t = strtok(hb, "|"); t && nh < 64; t = strtok(NULL, "|")) {
                hlen[nh] = parse_segs(t, &hacts[nh], '+');
                if (hlen[nh] <= 0) { sb_f(o, "err bad hold action '%s'", t); for (int i = 0; i < nh; i++) free(hacts[i]); for (int i = 0; i < nact; i++) free(acts[i]); return; }
                nh++;
            }
            hold.nact = nh;
        }
        const char *st = opt(tok, nt, "t"), *sr = opt(tok, nt, "refine"), *ssd = opt(tok, nt, "seed"), *sm = opt(tok, nt, "maxlen"), *sk = opt(tok, nt, "sticky"), *sap = opt(tok, nt, "apply");
        if (ssd) { g_xs = 0x9E3779B97F4A7C15ull ^ ((uint64_t)strtoull(ssd, 0, 10) * 0xD1B54A32D192ED03ull); if (!g_xs) g_xs = 1; }
        int maxlen = sm ? atoi(sm) : 4; if (maxlen < 1) maxlen = 1;
        const char *ssc = opt(tok, nt, "score"), *sgr = opt(tok, nt, "greed");
        int scp = ssc ? pid_of(ssc) : -1;
        if (ssc && scp < 0) { sb_f(o, "err explore score=P (undefined program)"); for (int i = 0; i < nact; i++) free(acts[i]); return; }
        static lrec_t sh[MAX_HOT], stx[MAX_TXT]; int snh = g_nhot, snt = g_ntxt, sts = g_txt_screen, sst = g_rec_stale;
        memcpy(sh, g_hot, sizeof sh); memcpy(stx, g_txt, sizeof stx);
        do_explore(g, a, cp, nc, acts, alen, nact, st ? atof(st) : 60000, sr ? atof(sr) : 5000, maxlen, sk ? atof(sk) / 100.0 : 0.5, sap ? atoi(sap) : 0,
                   scp, sgr ? atof(sgr) : 3.0, &hold, o);
        if (!strstr(o->p ? o->p : "", " applied=1")) {     /* nothing applied: the rows of the frame the search started at */
            memcpy(g_hot, sh, sizeof sh); memcpy(g_txt, stx, sizeof stx); g_nhot = snh; g_ntxt = snt; g_txt_screen = sts; g_rec_stale = sst;
        }
        for (int i = 0; i < nact; i++) free(acts[i]);
        for (int i = 0; i < nh; i++) free(hacts[i]);
    }
    else if (!strcmp(c, "quit")) { sb_f(o, "ok bye"); send_all(o->p, o->n); send_all("\n", 1); rh_finish("live-quit"); }
    else sb_f(o, "err unknown command '%s'", c);
}

void rh_live_main(void)
{
    /* with --trace: replay it first (its events go into the movie), to its end or --live-until */
    if (O.trace) {
        uint32_t until = O.live_until ? O.live_until : rh_trace_last_frame();
        while (g_frame < until) rh_step();
        rh_log("live: replayed to frame %u (%s)\n", g_frame, rh_trace_status());
    }
    static char line[1 << 16];
    for (;;) {
        if (g_cs == INVALID_SOCKET) {
            if (!wait_readable(g_ls)) rh_finish("live-idle");
            g_cs = accept(g_ls, NULL, NULL);
            if (g_cs == INVALID_SOCKET) rh_fatal("live: accept failed (err %d)", WSAGetLastError());
            int one = 1; setsockopt(g_cs, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
            g_in_n = 0;
        }
        if (read_line(line, sizeof line) < 0) { closesocket(g_cs); g_cs = INVALID_SOCKET; continue; }
        sb_t o = { 0 };
        command(line, &o);
        sb_put(&o, "\n", 1);
        send_all(o.p, o.n);
        free(o.p);
    }
}

/* ---------------- --snap-test A:B[:K] (a plain replay through the coroutine) ----------------
 * At boundary A: savestate. At boundary B: hash; restore; the replay runs A..B again (the trace cursor is in the
 * win meta); hash again — K times. Every pass must give the first pass's hashes; the run's end state must be the
 * plain replay's (`rhost.py snap-check`). No write tracking here, so no residue class: any difference is a miss. */
static uint32_t g_st_a, g_st_b; static int g_st_k = 2, g_st_pass, g_st_id, g_st_fail, g_st_on;
static uint64_t g_st_sem, g_st_fb; static uint32_t g_st_rng;
static char g_st_status[160];
void rh_snap_selftest(void)
{
    if (!O.snap_test) return;
    if (!g_st_on) {
        unsigned a = 0, b = 0; int k = 2;
        if (sscanf(O.snap_test, "%u:%u:%d", &a, &b, &k) < 2 || b <= a) rh_fatal("--snap-test A:B[:K]");
        g_st_a = a; g_st_b = b; g_st_k = k; g_st_on = 1;
    }
    if (g_frame == g_st_a && !g_st_id) {
        if (!(g_st_id = rh_snap_take())) rh_fatal("snap-test: no savestate at f%u", g_frame);
        rh_log("snap-test: savestate %d at f%u\n", g_st_id, g_frame);
    } else if (g_frame == g_st_b && g_st_id && g_st_pass <= g_st_k) {
        rh_hash_t h; rh_state_hash(&h);
        uint64_t fb = 0; rh_fb_hash(&fb);
        if (!g_st_pass) { g_st_sem = h.semantic; g_st_rng = G32(L2_RNG_A); g_st_fb = fb; }
        else if (h.semantic != g_st_sem || G32(L2_RNG_A) != g_st_rng || fb != g_st_fb) {
            g_st_fail++;
            rh_log("snap-test: pass %d DIFFERS at f%u: sem %016llx/%016llx rng %08x/%08x fb %016llx/%016llx\n", g_st_pass, g_frame,
                   (unsigned long long)h.semantic, (unsigned long long)g_st_sem, G32(L2_RNG_A), g_st_rng, (unsigned long long)fb, (unsigned long long)g_st_fb);
        }
        if (g_st_pass < g_st_k) {
            g_st_pass++;
            rh_snap_take();                                  /* a second savestate in between: exercises parent sharing */
            if (rh_snap_restore(g_st_id)) rh_fatal("snap-test: restore failed");
        } else g_st_pass++;
    }
}
const char *rh_snap_test_status(void)
{
    if (!O.snap_test) return "";
    snprintf(g_st_status, sizeof g_st_status, " snap_test=%s passes=%d", g_st_fail ? "FAIL" : g_st_pass <= g_st_k ? "INCOMPLETE" : "PASS",
             g_st_pass > g_st_k ? g_st_k : g_st_pass);
    return g_st_status;
}
