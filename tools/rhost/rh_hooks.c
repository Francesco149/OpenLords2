/* rh_hooks.c — port functions vs retail on identical state (the registry; core rows here,
 * subsystem rows in rh_hooks_<subsystem>.c — see rh_hooks.h).
 *
 *   --hook shadow:NAME[,…]   every retail call of NAME runs retail, then the PORT on the identical
 *                            pre-state (rh_shadow_ex), compares every state dword; retail's
 *                            result is kept. Divergences are logged with the first differing VAs.
 *   --hook replace:NAME      the port runs INSTEAD of retail (hybrid mode).
 *   --hook @FILE             the spec read from a file (rhost.py always passes one).
 * Add a port function = a retail/port wrapper pair + one table row (rhost.py derives VERIFY_HOOKS).
 * Rule: a port function never calls its own retail VA. */
#include "rh_hooks.h"
#include "engine.h"

void rh_hook_count(hook_t *h, int r, const char *why)
{
    h->calls++;
    if (r < 0) { h->skipped++; return; }
    if (r > 0) {
        if (!h->diverge++) snprintf(h->first, sizeof h->first, "f%u %s", g_frame, why);
        if (h->diverge <= 5) rh_log("DIVERGE %s f%u %s\n", h->name, g_frame, why);
    }
}

/* ---------------------------------------------------------------- rng_step: FUN_00404a46 (src/engine_rng.c) */
static hook_t *H_RNG;
static void rng_retail(void *c) { (void)c; ((void (__cdecl *)(void))0x404a46u)(); }
static void rng_port(void *c)
{
    (void)c;
    engine_rng_in_t in = { G32(0x58fe28u), G32(0x58fe08u) };
    engine_rng_out_t o;
    engine_rng_step(&in, &o);
    G32(0x58fe28u) = o.new_state_a; G32(0x58fe08u) = o.new_state_b;
    G32(0x5aea58u) = o.rng_15bit_a; G32(0x58fd7cu) = o.rng_7bit_a; G32(0x58fd70u) = o.rng_3bit_a;
    G32(0x5aea5cu) = o.rng_15bit_b; G32(0x58fd60u) = o.rng_7bit_b; G32(0x58fd6cu) = o.rng_3bit_b;
}
static void __cdecl w_rng(void)
{
    if (H_RNG->mode == H_REPLACE) { H_RNG->calls++; rng_port(NULL); return; }
    if (rh_in_shadow()) { rng_retail(NULL); return; }
    char why[400];
    rh_hook_count(H_RNG, rh_shadow_ex("rng_step", rng_retail, rng_port, NULL, why, sizeof why), why);
}

static hook_t CORE_HOOKS[] = {
    { "rng_step", 0x404a46u, (void *)w_rng, 0, 0, 0, 0, "" },
};

/* every table: core + one per subsystem file (rh_hooks.h RH_HOOK_TABLES) */
#define T(name) extern hook_t name[]; extern const int name##_N;
RH_HOOK_TABLES(T)
#undef T
static struct { hook_t *t; int n; } TABLES[] = {
    { CORE_HOOKS, (int)(sizeof CORE_HOOKS / sizeof *CORE_HOOKS) },
#define T(name) { name, 0 },
    RH_HOOK_TABLES(T)
#undef T
};
#define NTAB ((int)(sizeof TABLES / sizeof *TABLES))
static hook_t *find_hook(const char *name)
{
    for (int k = 0; k < NTAB; k++) for (int i = 0; i < TABLES[k].n; i++) if (!strcmp(TABLES[k].t[i].name, name)) return &TABLES[k].t[i];
    return NULL;
}

void rh_hooks_install(void)
{
    H_RNG = &CORE_HOOKS[0];
    {   int k = 1;
#define T(name) TABLES[k++].n = name##_N;
        RH_HOOK_TABLES(T)
#undef T
        (void)k;
    }
    if (!O.hooks || !*O.hooks) return;
    char spec[8192] = "";
    if (O.hooks[0] == '@') {
        FILE *f = fopen(O.hooks + 1, "r");
        if (!f) rh_fatal("cannot read hook spec %s", O.hooks + 1);
        size_t n = fread(spec, 1, sizeof spec - 1, f); spec[n] = 0; fclose(f);
    } else snprintf(spec, sizeof spec, "%s", O.hooks);
    for (char *t = strtok(spec, ", \r\n"); t; t = strtok(NULL, ", \r\n")) {
        char *c = strchr(t, ':');
        if (!c) rh_fatal("hook spec '%s': want shadow:NAME / replace:NAME", t);
        *c = 0;
        int mode = !strcmp(t, "shadow") ? H_SHADOW : !strcmp(t, "replace") ? H_REPLACE : !strcmp(t, "off") ? H_OFF : -1;
        if (mode < 0) rh_fatal("hook mode '%s'", t);
        hook_t *h = find_hook(c + 1);
        if (!h) rh_fatal("unknown hook '%s'", c + 1);
        h->mode = mode;
        if (mode == H_OFF) continue;
        int n = redirect_calls(h->va, h->wrapper);
        rh_log("hook %s:%s %08x <- %d call site(s)\n", t, h->name, h->va, n);
        if (!n) rh_fatal("hook %s: no call sites", h->name);
    }
}

void rh_hooks_report(void)
{
    rh_shadow_report();
    for (int k = 0; k < NTAB; k++) for (int i = 0; i < TABLES[k].n; i++) {
        hook_t *h = &TABLES[k].t[i];
        if (h->mode == H_OFF) continue;
        rh_log("hook-result %s mode=%s calls=%ld diverge=%ld skipped=%ld%s%s\n", h->name, h->mode == H_SHADOW ? "shadow" : "replace",
               h->calls, h->diverge, h->skipped, h->diverge ? " first: " : "", h->diverge ? h->first : "");
    }
}
