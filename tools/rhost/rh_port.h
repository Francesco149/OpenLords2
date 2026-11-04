/* rh_port.h — plumbing shared by the port-hook tables (rh_hooks_<subsystem>.c; contract: rh_hooks.h).
 *
 * BINDING.  Linked port modules read and write retail's own memory: their save-block globals and runtime
 * globals are ABSOLUTE symbols at retail VAs (rh_hooks_ai.c: the save block; rh_hooks_combat.c: the battle /
 * movement runtime globals), and rh_port_bind_once() points their runtime pointer seams at retail's tables.
 * What remains are FILE-STATIC mirrors of retail globals: rh_port_sync_in() loads them from retail before every
 * port call, rh_port_sync_out() stores them back after it (store-if-changed: under the page-tracked shadow every
 * first write to a page faults). Port code that calls back into RETAIL code (a seam bound to an unported retail
 * function) brackets the call with rh_port_retail_enter/leave, so retail sees and changes the live values.
 * Definitions: rh_hooks_ai.c (bind / sync / EVQ / the generic wrapper body) — one copy for all of rhost.exe.
 *
 * WRAPPERS.  Every hooked function is __cdecl. A row's wrapper (w_<name>) is a naked entry stub: it notes the
 * caller's esp (e0: [e0] = return address, [e0+4..] = the caller's pushed arguments), moves 256 KB down the
 * stack (below any hook machinery already running) and calls the row's C body. One generic body (rh_port_run)
 * serves all rows. The retail side runs AT e0 again (rh_call_at: the original arguments in place, the same
 * frame addresses), so retail's reads of stack residue — locals it never initialized, which hold what earlier
 * calls left below the caller's esp — see exactly what they see in a pure run, and the residue it leaves is
 * retail's; the C machinery and the port run 256 KB below and leave that zone alone (retail's own frames below
 * e0 must fit in it). The port side calls a per-row adapter with a copy of the arguments. A non-void row's
 * return values are compared too (a mismatch is a divergence). Shadow mode returns retail's EAX (so even "void"
 * rows are transparent); replace mode returns the port's value. A table file defines RH_TABLE (its hook_t array)
 * and an enum X_<name> in row order before using HOOK_R / HOOK_V.
 *
 * HOST LAYERS.  rhost's media pool, DirectSound registry / serials / timers and fake-handle sequence are not
 * retail memory, so the shadow does not roll them back; rh_port_run rolls them like the heap meta (the port run
 * starts from the host state retail's run started from; retail's post host state is put back), so a port run
 * that calls back into retail code which loads a WAV cue and creates a buffer (FUN_00427990) matches retail's.
 *
 * STACK RESIDUE.  A retail function that reads an uninitialized local takes it from the residue: the port
 * models it as an explicit input / output, and its adapter moves it with rh_res_get / rh_res_put (offsets below
 * e0). Replace mode writes the port's final values where retail's frame would have left them; shadow mode
 * compares them with the residue retail's run left. */
#pragma once
#include "rh_hooks.h"
#include "engine_event_queue.h"

/* store only what changed (an unchanged store is no state change, and costs a page fault under the shadow) */
#define PUT(va, v)  do { uint32_t v_ = (uint32_t)(v); if (G32(va) != v_) G32(va) = v_; } while (0)
#define PUT8(va, v) do { uint8_t v_ = (uint8_t)(v); if (G8(va) != v_) G8(va) = v_; } while (0)

void rh_port_bind_once(void);
void rh_port_sync_in(void);
void rh_port_sync_out(void);
void rh_port_retail_enter(void);   /* port → retail: store the mirrors (and an open EVQ) */
void rh_port_retail_leave(void);   /* retail → port: reload them */

/* engine_event_queue: the port's queue is one struct, retail's six globals. A row whose port function posts
 * takes `q`: EVQ(call) loads it from retail, calls, stores it back. */
engine_event_queue_state_t *rh_evq_load(void);
void rh_evq_store(void);
#define EVQ(call) do { engine_event_queue_state_t *q = rh_evq_load(); call; rh_evq_store(); } while (0)
#define MP_ACTIVE ((int)G32(0x553030u))   /* DAT_00553030 */

typedef uint32_t (*rh_port_fn)(const uint32_t *a);
uint32_t rh_port_run(hook_t *h, void *self, rh_port_fn port, int has_ret, uint32_t e0);
uint32_t rh_call_at(uint32_t va, uint32_t e0, uint32_t ebp0);   /* call retail `va` with esp = e0 at its entry and the
                                                                  caller's ebp (rh_hooks_ai.c) */
extern uint32_t rh_entry_ebp;   /* the retail caller's ebp at a wrapper's entry (RH_ENTRY stores it; rh_port_run keeps it) */
uint32_t rh_res_get(int off);                    /* the dword at e0 - off when the hook was entered (off 4..0x40) */
void     rh_res_put(int off, uint32_t v);        /* the port's final value for it (replace: written; shadow: compared) */
void     rh_res_set(int off, uint32_t v);        /* … a modelled value retail's other frames may overwrite (replace: written;
                                                    shadow: not compared — the state dwords still are) */

/* one row = an index, a port adapter (a copy of the caller's arguments → port call) and a naked entry stub */
/* the stub: C machinery at min(e0 - 256 KB, the floor of a hook machinery already running - 1 KB) — a hook
 * entered from retail code that rh_call_at runs at an outer e0 must not land on the outer hook's C frames */
extern uint32_t rh_c_floor;
#define RH_ENTRY(W, B) \
    /* e0 → eax; edx = e0 - 256 KB, or the running machinery's floor - 1 KB if lower; save e0; b(e0); esp = e0 */ \
    static __attribute__((naked)) void W(void) \
    { __asm__("movl %ebp, _rh_entry_ebp\n\tmovl %esp, %eax\n\tleal -0x40000(%eax), %edx\n\tmovl _rh_c_floor, %ecx\n\t" \
              "testl %ecx, %ecx\n\tjz 1f\n\tsubl $0x400, %ecx\n\tcmpl %ecx, %edx\n\tjb 1f\n\tmovl %ecx, %edx\n" \
              "1:\tmovl %edx, %esp\n\tpushl %eax\n\tpushl %eax\n\t" \
              "call _" #B "\n\taddl $4, %esp\n\tpopl %esp\n\tret"); }
#define I(k) ((int)a[k])
/* HOOK_R: the port returns a value (compared); HOOK_V: void (retail's EAX passed through in shadow mode).
 * N (retail's arity) documents the row; the adapter sees ten argument slots (a[0..9]). */
#define HOOK_BODY(NAME, HAS_RET) \
    static void w_##NAME(void); \
    static __attribute__((used, noinline)) uint32_t __cdecl b_##NAME(uint32_t e0) \
    { return rh_port_run(&RH_TABLE[X_##NAME], (void *)w_##NAME, p_##NAME, HAS_RET, e0); } \
    RH_ENTRY(w_##NAME, b_##NAME)
#define HOOK_R(NAME, N, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) { (void)a; return (uint32_t)(CALL); } \
    HOOK_BODY(NAME, 1)
#define HOOK_V(NAME, N, CALL) \
    static uint32_t p_##NAME(const uint32_t *a) { (void)a; CALL; return 0; } \
    HOOK_BODY(NAME, 0)
