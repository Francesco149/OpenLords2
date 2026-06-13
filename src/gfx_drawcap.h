/*
 * OpenLords2 — per-draw write-journal for Trace Studio v3 draw-stepping.
 *
 * Lords of the Realm II software-composites every sprite/glyph/line into ONE
 * 8bpp back-surface; the per-draw structure is invisible at the DDraw present
 * seam (one Blt).  To make the native viewer able to STEP individual draws, we
 * capture — only while a frame is armed for capture — each drawing primitive as
 * a sparse write-delta against the canvas:
 *
 *     base plane (the canvas at the first draw)
 *     + draw 0 (op_type, m0,m1,m2, [(offset,val)…])
 *     + draw 1 …
 *     == the final composited plane, exactly.
 *
 * Mechanism (mirrors the retail Frida before/after-snapshot path, so both
 * engines yield directly-comparable DRAW records):
 *   - gfx_drawcap_draw() at each dispatch entry records the op META and finalizes
 *     the PREVIOUS draw's delta by diffing the bound canvas against a running
 *     shadow.  The base is snapshotted lazily on the first draw.
 *   - gfx_drawcap_flush() (from gfx_unlock_canvas, canvas still bound) closes the
 *     last pending draw — so a frame composed of several lock/unlock cycles
 *     accumulates into one draw stream.
 *
 * KEY ROBUSTNESS: base + Σ(deltas) == final ALWAYS, because every delta is
 * shadow→canvas and the shadow tracks the canvas.  Any write that bypasses an
 * instrumented dispatch is not LOST — it just folds into the adjacent draw's
 * delta (coarser attribution, never a wrong pixel).  Empty deltas (clip-culled
 * draws, composite wrappers) are dropped.
 *
 * Zero cost when not armed: the dispatch hooks early-out on gfx_drawcap_armed()
 * (one load + branch).  Pure C — no DDraw/Win32 — so it links into the host test
 * binary alongside the gfx layer it instruments.
 *
 * The op_type values mirror OLV3_OP_* in
 * tools/trace_studio_v3/format/olv3_format.h (the viewer's per-type readout).
 */
#ifndef OPENLORDS2_GFX_DRAWCAP_H
#define OPENLORDS2_GFX_DRAWCAP_H

#include <stdint.h>

enum {
    GFX_DRAWCAP_OP_SPRITE = 0,   /* m0=sprite_index, m1=x, m2=y */
    GFX_DRAWCAP_OP_GLYPH  = 1,   /* m0=char_code,    m1=x, m2=y */
    GFX_DRAWCAP_OP_LINE   = 2,   /* m0=color,        m1=x, m2=y */
    GFX_DRAWCAP_OP_PANEL  = 3,   /* m0=kind/cols,    m1=x, m2=y */
    GFX_DRAWCAP_OP_FILL   = 4,   /* m0=color,        m1=x, m2=y */
    GFX_DRAWCAP_OP_IMAGE  = 5,   /* full-screen background image */
    GFX_DRAWCAP_OP_OTHER  = 6,
};

/* Arm draw capture for the frame about to render.  Resets the draw list; base is
 * taken lazily at the first draw.  Allocates scratch on first use.  Calling while
 * already armed restarts the frame. */
void gfx_drawcap_begin_frame(void);

/* Is capture armed for the current frame?  The dispatch hooks gate on this. */
int  gfx_drawcap_armed(void);

/* Record the start of one draw (a dispatch entry): finalize the previous draw's
 * write-delta, snapshot the base on first call, then stage this draw's meta.
 * No-op when not armed or no canvas is bound. */
void gfx_drawcap_draw(int op_type, int m0, int m1, int m2);

/* Finalize the last pending draw — call from gfx_unlock_canvas while the canvas
 * is still bound.  Idempotent; no-op when not armed / nothing pending. */
void gfx_drawcap_flush(void);

/* Number of draws recorded so far this frame (after empty-skip). */
int  gfx_drawcap_count(void);

/* Did this frame snapshot a base (i.e. record ≥1 draw)?  0 ⇒ plain frame, no
 * sidecar worth writing. */
int  gfx_drawcap_has_base(void);

/* Serialize base + draws to the <label>.draws sidecar (binary, native-endian):
 *   [u32 'OLDR'][u32 w][u32 h][u32 present][u32 n_draws]
 *   [base: w*h bytes]
 *   n_draws × { [u32 op_type][u32 m0][u32 m1][u32 m2][u32 n]
 *               [n × u32 offset][n × u8 val] }
 * Returns 1 on success, 0 on failure / nothing to write.  Call post-flush. */
int  gfx_drawcap_write(const char *path, unsigned present);

/* Disarm.  Scratch buffers are retained for the next frame's reuse. */
void gfx_drawcap_end_frame(void);

#endif /* OPENLORDS2_GFX_DRAWCAP_H */
