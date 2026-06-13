/* OpenLords2 Trace Studio v3 — headless replay CLI.
 *
 *   replay <cap.olv3>                  print a structured summary (count/dims/presents)
 *   replay <cap.olv3> <idx> <out.bmp>  reconstruct kept frame <idx> → a 32bpp .bmp
 *
 * Pure C, no Windows/D3D — builds host-native (gcc) for tests AND under mingw. The
 * .bmp it writes is the exact BGRA the viewer uploads, so a host render here is the
 * ground truth a pytest cross-checks against the Python expander.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "replay_core.h"

static int save_bmp(const char *path, const uint8_t *bgra, int W, int H)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    const uint32_t img = (uint32_t)W * H * 4, off = 14 + 40;
    uint8_t hdr[54] = {0};
    hdr[0]='B'; hdr[1]='M';
    *(uint32_t*)&hdr[2]=off+img; *(uint32_t*)&hdr[10]=off;
    *(uint32_t*)&hdr[14]=40; *(int32_t*)&hdr[18]=W; *(int32_t*)&hdr[22]=H;
    *(uint16_t*)&hdr[26]=1; *(uint16_t*)&hdr[28]=32; *(uint32_t*)&hdr[34]=img;
    fwrite(hdr, 1, 54, f);
    for (int r = H - 1; r >= 0; r--) fwrite(bgra + (size_t)r * W * 4, 1, (size_t)W * 4, f);
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <cap.olv3> [idx out.bmp]\n", argv[0]); return 2; }
    char err[128] = {0};
    Olv3Replay *r = olv3_open(argv[1], err, sizeof err);
    if (!r) { fprintf(stderr, "open failed: %s\n", err); return 2; }
    int n = olv3_count(r), W = olv3_width(r), H = olv3_height(r);

    if (argc >= 4) {
        int idx = atoi(argv[2]);
        const uint8_t *bgra = olv3_render(r, idx);
        if (!bgra) { fprintf(stderr, "render frame %d failed\n", idx); olv3_close(r); return 2; }
        int ok = save_bmp(argv[3], bgra, W, H);
        fprintf(stderr, "frame %d/%d (present %d) -> %s %s\n", idx, n - 1, olv3_present(r, idx),
                argv[3], ok ? "OK" : "WRITE-FAILED");
        olv3_close(r);
        return ok ? 0 : 2;
    }

    printf("{\"n_frames\": %d, \"dims\": \"%dx%d\", \"present_first\": %d, \"present_last\": %d}\n",
           n, W, H, olv3_present(r, 0), olv3_present(r, n - 1));
    olv3_close(r);
    return 0;
}
