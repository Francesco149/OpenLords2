/* OpenLords2 Trace Studio v3 — resident replay core (shared: replay CLI + viewer).
 *
 * Opens an olv3 container ONCE (read into memory, index every dedup'd palette +
 * index-plane resource, index each kept frame), then reconstructs any frame on
 * demand. Reconstruction is a palette LUT expansion (olv3_expand_bgra) — no GPU,
 * no replay device, sub-millisecond — so the viewer scrubs at memory speed.
 *
 * Pure C, no Windows/D3D dependency (cf. the openrecet sibling's core, which must
 * drive a real d3d8 device): the container already holds the composited result, so
 * there is nothing to re-issue. That also lets this core compile on the Linux host
 * for headless verification, not just under mingw for the viewer.
 *
 * The index-plane + palette accessors expose the raw 8bpp data so the viewer can
 * diff at the INDEX level (wrong blitter output) distinctly from the RGB level
 * (wrong palette colour) — a Lords-2-specific diagnostic the RGB-only openrecet
 * viewer can't offer.
 */
#ifndef OLV3_REPLAY_CORE_H
#define OLV3_REPLAY_CORE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Olv3Replay Olv3Replay;

/* Open a container: read it in, register every palette + index-plane resource,
 * index each kept frame. Returns NULL on failure (err, if non-NULL, gets a short
 * reason). */
Olv3Replay *olv3_open(const char *path, char *err, int errlen);

int olv3_count (const Olv3Replay *r);   /* kept-frame count          */
int olv3_width (const Olv3Replay *r);   /* frame width  (e.g. 640)   */
int olv3_height(const Olv3Replay *r);   /* frame height (e.g. 480)   */

/* Absolute present index (the engine frame counter at capture) for kept frame
 * `idx`, or -1 if out of range. Lets the viewer show real frame numbers. */
int olv3_present(const Olv3Replay *r, int idx);

/* Reconstruct kept frame `idx`: expand its index plane through its palette into an
 * internal tight w*h*4 BGRA buffer, valid until the next render/close, or NULL on
 * failure. The display primitive (the viewer uploads this to a D3D9 texture). */
const uint8_t *olv3_render(Olv3Replay *r, int idx);

/* The raw 8bpp index plane for kept frame `idx` (w*h bytes), valid for the life of
 * the replay, or NULL if out of range. For the INDEX-level diff. */
const uint8_t *olv3_indices(Olv3Replay *r, int idx);

/* The 256-entry palette (256*4 bytes, R,G,B,flags per entry) for kept frame `idx`,
 * or NULL if out of range. For the palette-strip panel + palette diff. */
const uint8_t *olv3_palette(Olv3Replay *r, int idx);

void olv3_close(Olv3Replay *r);

#ifdef __cplusplus
}
#endif
#endif /* OLV3_REPLAY_CORE_H */
