// OpenLords2 Trace Studio v3 - native viewer.
//
// Dear ImGui + Win32 + Direct3D9, cross-built 32-bit with mingw (runs on the
// Windows host via WSLInterop). The d3d9 device hosts the UI; two olv3 replay
// cores (port + retail) reconstruct kept frames on demand by palette-expanding an
// 8bpp index plane → BGRA (sub-millisecond, pure CPU - no replay device) into
// resident d3d9 textures that ImGui shows as the port|retail|diff panels. NO PNG
// bake: the two .olv3 containers (named by view.json) are the only artifacts;
// frames are reconstructed live and synced by the STORED identity join (view.json's
// per-column port/retail idx).
//
// Lords 2 is 8bpp, so the viewer diffs at BOTH levels the openrecet sibling can't:
//   • RGB diff   - the two palette-expanded frames (what the eye sees).
//   • INDEX diff - the raw 8bpp planes (what the BLITTER wrote): an index mismatch
//                  means the compositor diverged; a palette mismatch with matching
//                  indices means the palette builder diverged. Separating them is
//                  the core port-gap diagnostic here.
// A palette-strip panel + a per-pixel index/RGB hover readout complete the picture.
//
//   viewer.exe <view.json>                interactive 3-panel scrub
//   viewer.exe --shot out.bmp <view.json> headless one-frame render (self-verify/feed)
//
// Build: nix develop --command make -C tools/trace_studio_v3/viewer
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include "replay_core.h"
#include <nlohmann/json.hpp>
#include <d3d9.h>
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
using json = nlohmann::json;

static LPDIRECT3D9       g_d3d = nullptr;
static LPDIRECT3DDEVICE9 g_dev = nullptr;
static D3DPRESENT_PARAMETERS g_pp;

// ── the loaded view ──
struct Col {
    int offset = 0, port_idx = -1, retail_idx = -1;
    std::string label;                 // identity key (anchor+offset / trace label)
    int port_present = -1, retail_present = -1;
    std::string gap;                   // "", "port", or "retail"
    // computed diff metrics (sentinels < 0 = not yet computed)
    int gt8 = -1, maxd = 0; double meanabs = 0;   // RGB diff
    int idxdiff = -1;                  // # pixels whose 8bpp index differs
    int paldiff = -1;                  // # palette entries differing (RGB)
    // engine state (the save-schema slot snapshot), port vs retail, identity-keyed.
    bool has_state = false;
    json sport, sretail;
};
static std::vector<Col> g_cols;
static Olv3Replay *g_port = nullptr, *g_retail = nullptr;
static LPDIRECT3DTEXTURE9 g_tport = nullptr, g_tretail = nullptr, g_tdiff = nullptr;
static uint8_t *g_diffbuf = nullptr;
static int g_w = 0, g_h = 0, g_cur = 0;
static std::string g_scenario, g_anchor, g_verdict, g_note;
static bool g_show[3] = {true, true, true};   // port, retail, diff
static int  g_diff_mode = 1;          // 0 = RGB amplified, 1 = INDEX (default: the blitter-true signal)
static int  g_heat_metric = 1;        // ribbon heat: 0 = RGB meanabs, 1 = index-diff count
static const int AMP = 6;
// game-state panel
static bool g_has_state = false;
static bool g_show_state = true;
static bool g_state_diff_only = false;
static char g_state_filter[64] = {0};
// per-pixel hover readout (engine-appropriate replacement for openrecet's pixel→draw pick)
static int g_hover_x = -1, g_hover_y = -1;

// ── draw-stepping (v2 containers: per-draw write-deltas on the PORT side) ──
// The port emits a base plane + ordered op-deltas per captured frame; the replay
// core reconstructs any prefix (build-up), one draw in isolation (solo), and the
// draw that last painted a pixel (pick). These drive the panel below.
static bool g_drawstep   = false;   // step a frame's draws (the stepped panel shows the build-up)
static int  g_ds_side    = 0;       // which engine to step: 0 = port, 1 = retail
static int  g_draw_k     = -1;      // # draws applied in the build-up (slider; clamped to [0,n])
static int  g_draw_n     = 0;       // draw count of the current frame on the stepped side
static bool g_draw_solo  = false;   // show ONLY the selected draw over its base
static int  g_draw_sel   = 0;       // selected draw (solo target + list highlight)
static int  g_ds_col     = -1;      // (col<<1|side) the draw-step bounds were last refreshed for
// cross-engine pixel pick: the owning draw on EACH engine at (g_pick_x,g_pick_y)
static int  g_pick_x = -1, g_pick_y = -1;
static int  g_pick_port = -2, g_pick_retail = -2;   // -2 none, -1 base shows through, >=0 draw idx
static bool g_do_pick    = false;   // a panel click is pending pick resolution
static bool g_draw_scroll = false;  // scroll the draw list to g_draw_sel next frame

static const char *OPN[8] = {"sprite","glyph","line","panel","fill","image","other","?"};
static ImU32 op_color(int op)
{
    switch (op & 7) {
        case 0: return IM_COL32(120, 200, 255, 255);  // sprite  — blue
        case 1: return IM_COL32(170, 255, 150, 255);  // glyph   — green
        case 2: return IM_COL32(255, 210, 120, 255);  // line    — amber
        case 3: return IM_COL32(220, 160, 255, 255);  // panel   — violet
        case 4: return IM_COL32(255, 150, 150, 255);  // fill    — red
        case 5: return IM_COL32(150, 224, 224, 255);  // image   — cyan
        default:return IM_COL32(200, 200, 200, 255);  // other
    }
}

// One engine's pixel-pick result: the owning draw + its op/meta, op-coloured.
// owner: -2 = side has no draws, -1 = base shows through, >=0 = draw index.
static void pick_readout(const char *name, Olv3Replay *rep, int idx, int owner)
{
    if (owner == -2) { ImGui::TextDisabled("%s: (no draws)", name); return; }
    if (owner < 0)   { ImGui::TextDisabled("%s: base", name); return; }
    int op = 6, m0 = 0, m1 = 0, m2 = 0;
    olv3_draw_meta(rep, idx, owner, &op, &m0, &m1, &m2);
    ImGui::TextColored(ImColor(op_color(op)), "%s: #%d %s m0=%d (%d,%d)",
                       name, owner, OPN[op & 7], m0, m1, m2);
}

// ── notes / crop regions (the web studio's note loop, native) ──
struct Note { int id = 0; std::string label, side, text; int col = -1; bool hasbox = false; float box[4] = {0,0,0,0}; };
static std::vector<Note> g_notes;
static std::string g_notes_path;
static int  g_note_next_id = 1;
static bool g_note_mode = false;
static bool g_show_notes = true;
struct Draft {
    bool placing = false, editing = false, hasbox = false, focus = false;
    std::string side, label; int col = -1;
    ImVec2 r0, r1, rmin, rmax;
    float box[4] = {0,0,0,0};
    char text[512] = {0};
};
static Draft g_draft;

static void destroy_device();
static void show_column(int i);

static bool create_device(HWND hwnd, UINT W, UINT H)
{
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) return false;
    ZeroMemory(&g_pp, sizeof(g_pp));
    g_pp.Windowed = TRUE;
    g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    g_pp.BackBufferWidth = W; g_pp.BackBufferHeight = H;
    g_pp.EnableAutoDepthStencil = TRUE;
    g_pp.AutoDepthStencilFormat = D3DFMT_D16;
    g_pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    return SUCCEEDED(g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_pp, &g_dev));
}

static LPDIRECT3DTEXTURE9 make_tex()
{
    LPDIRECT3DTEXTURE9 t = nullptr;   // X8 ⇒ ImGui draws the alpha-less frame opaque
    g_dev->CreateTexture(g_w, g_h, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &t, nullptr);
    return t;
}

static void upload(LPDIRECT3DTEXTURE9 tex, const uint8_t *bgra)
{
    if (!tex || !bgra) return;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
        for (int r = 0; r < g_h; r++)
            memcpy((uint8_t *)lr.pBits + (size_t)r * lr.Pitch, bgra + (size_t)r * g_w * 4, (size_t)g_w * 4);
        tex->UnlockRect(0);
    }
}

// amplified-white RGB diff into `out` + the gt8/meanabs/maxd metric (the web studio's
// amplified_diff law, so a v3 RGB metric matches the old one).
static void diff_rgb(const uint8_t *p, const uint8_t *r, uint8_t *out, Col &c)
{
    long gt8 = 0; int maxd = 0; double sum = 0;
    const int N = g_w * g_h;
    for (int i = 0; i < N; i++) {
        int db = abs(p[i*4]-r[i*4]), dg = abs(p[i*4+1]-r[i*4+1]), dr = abs(p[i*4+2]-r[i*4+2]);
        int mx = db > dg ? (db > dr ? db : dr) : (dg > dr ? dg : dr);
        int s = db + dg + dr;
        if (mx > 8) gt8++;
        if (mx > maxd) maxd = mx;
        sum += s;
        if (out) { int v = s * AMP; if (v > 255) v = 255; out[i*4] = out[i*4+1] = out[i*4+2] = (uint8_t)v; out[i*4+3] = 255; }
    }
    c.gt8 = (int)gt8; c.maxd = maxd; c.meanabs = sum / (3.0 * N);
}

// INDEX diff: red where the raw 8bpp indices differ, the matching frame dimmed
// underneath (so context is visible). Sets c.idxdiff = # differing pixels.
static void diff_index(const uint8_t *pi, const uint8_t *ri, const uint8_t *p_bgra,
                       uint8_t *out, Col &c)
{
    long nd = 0; const int N = g_w * g_h;
    for (int i = 0; i < N; i++) {
        if (pi[i] != ri[i]) {
            nd++;
            if (out) { out[i*4]=0; out[i*4+1]=0; out[i*4+2]=255; out[i*4+3]=255; }   // red
        } else if (out) {
            uint8_t g = (uint8_t)((p_bgra[i*4]*30 + p_bgra[i*4+1]*59 + p_bgra[i*4+2]*11) / 100 / 3);
            out[i*4]=g; out[i*4+1]=g; out[i*4+2]=g; out[i*4+3]=255;                  // dim grey context
        }
    }
    c.idxdiff = (int)nd;
}

static int palette_diff_count(const uint8_t *pp, const uint8_t *rp)
{
    if (!pp || !rp) return -1;
    int n = 0;
    for (int i = 0; i < 256; i++)
        if (pp[i*4] != rp[i*4] || pp[i*4+1] != rp[i*4+1] || pp[i*4+2] != rp[i*4+2]) n++;
    return n;
}

// ── background diff-metric fill (responsive open; the web studio computed these
// synchronously). Each both-sides column needs 2 renders + a px loop. ──
static int g_metric_cursor = 0;

static bool compute_col_metric(Col &c)
{
    if (c.port_idx < 0 || c.retail_idx < 0 || c.gt8 >= 0) return false;
    const uint8_t *p = olv3_render(g_port, c.port_idx);
    if (!p) return false;
    std::vector<uint8_t> pcopy(p, p + (size_t)g_w * g_h * 4);   // render() reuses one buffer per core
    const uint8_t *r = olv3_render(g_retail, c.retail_idx);
    if (!r) return false;
    diff_rgb(pcopy.data(), r, nullptr, c);
    const uint8_t *pi = olv3_indices(g_port, c.port_idx);
    const uint8_t *ri = olv3_indices(g_retail, c.retail_idx);
    if (pi && ri) diff_index(pi, ri, pcopy.data(), nullptr, c);
    c.paldiff = palette_diff_count(olv3_palette(g_port, c.port_idx), olv3_palette(g_retail, c.retail_idx));
    return true;
}

static void precompute_metrics() { for (auto &c : g_cols) compute_col_metric(c); }

static int metrics_pending()
{
    int n = 0;
    for (auto &c : g_cols) if (c.port_idx >= 0 && c.retail_idx >= 0 && c.gt8 < 0) n++;
    return n;
}

static void pump_metrics(double budget_ms)
{
    if (g_metric_cursor >= (int)g_cols.size()) return;
    LARGE_INTEGER freq, t0, now; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);
    while (g_metric_cursor < (int)g_cols.size()) {
        compute_col_metric(g_cols[g_metric_cursor++]);
        QueryPerformanceCounter(&now);
        if ((double)(now.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart >= budget_ms) break;
    }
}

// the engine being draw-stepped (port or retail) + its kept-frame index for col.
static Olv3Replay *ds_replay() { return g_ds_side ? g_retail : g_port; }
static int ds_idx(const Col &c) { return g_ds_side ? c.retail_idx : c.port_idx; }

// render column `i` into the three panel textures (+ refresh its diff image/metrics).
// Refresh the draw-step bounds for the column/side being shown. On a genuine
// (col,side) change the build-up resets to "all draws applied" (== the final plane).
static void refresh_drawstep(int col, const Col &c)
{
    int idx = ds_idx(c);
    int n = idx >= 0 ? olv3_draws(ds_replay(), idx) : 0;
    if (n < 0) n = 0;
    g_draw_n = n;
    int key = (col << 1) | g_ds_side;
    if (key != g_ds_col) {
        g_draw_k = n; g_draw_sel = 0; g_ds_col = key;
    } else {
        if (g_draw_k < 0 || g_draw_k > n) g_draw_k = n;
        if (g_draw_sel >= n) g_draw_sel = n ? n - 1 : 0;
    }
}

// Upload `tex` from engine `rep`/`idx`: the stepped side shows the build-up
// (render_upto / solo), every other panel the full final plane. `full` is the
// already-copied full plane (render_upto/solo share the replay's one buffer, so
// the caller copies the full render out FIRST).
static void upload_panel(LPDIRECT3DTEXTURE9 tex, Olv3Replay *rep, int idx,
                         int side, const std::vector<uint8_t> &full)
{
    if (idx < 0 || full.empty()) return;
    if (g_drawstep && g_ds_side == side && g_draw_n > 0) {
        const uint8_t *dp = g_draw_solo ? olv3_render_solo(rep, idx, g_draw_sel)
                                        : olv3_render_upto(rep, idx, g_draw_k);
        upload(tex, dp ? dp : full.data());
    } else {
        upload(tex, full.data());
    }
}

static void show_column(int i)
{
    if (i < 0 || i >= (int)g_cols.size()) return;
    g_cur = i;
    Col &c = g_cols[i];
    refresh_drawstep(i, c);
    // Full renders FIRST (build-up reuses each replay's single buffer).
    std::vector<uint8_t> pcopy, rcopy;
    const uint8_t *p = c.port_idx   >= 0 ? olv3_render(g_port,   c.port_idx)   : nullptr;
    if (p) pcopy.assign(p, p + (size_t)g_w * g_h * 4);
    const uint8_t *r = c.retail_idx >= 0 ? olv3_render(g_retail, c.retail_idx) : nullptr;
    if (r) rcopy.assign(r, r + (size_t)g_w * g_h * 4);
    upload_panel(g_tport,   g_port,   c.port_idx,   0, pcopy);
    upload_panel(g_tretail, g_retail, c.retail_idx, 1, rcopy);
    // Diff is always full port vs full retail (the stepped panel may show a prefix).
    if (!pcopy.empty() && !rcopy.empty()) {
        if (g_diff_mode == 0) {
            diff_rgb(pcopy.data(), rcopy.data(), g_diffbuf, c);
        } else {
            const uint8_t *pi = olv3_indices(g_port, c.port_idx);
            const uint8_t *ri = olv3_indices(g_retail, c.retail_idx);
            diff_rgb(pcopy.data(), rcopy.data(), nullptr, c);   // keep RGB metric current too
            if (pi && ri) diff_index(pi, ri, pcopy.data(), g_diffbuf, c);
        }
        c.paldiff = palette_diff_count(olv3_palette(g_port, c.port_idx), olv3_palette(g_retail, c.retail_idx));
        upload(g_tdiff, g_diffbuf);
    }
}

// ── notes persistence (json array at g_notes_path, the Windows-local file) ──
static void load_notes()
{
    g_notes.clear(); g_note_next_id = 1;
    if (g_notes_path.empty()) return;
    std::ifstream f(g_notes_path.c_str());
    if (!f) return;
    json arr; try { f >> arr; } catch (const std::exception &) { return; }
    if (!arr.is_array()) return;
    for (auto &jn : arr) {
        Note n;
        n.id = jn.value("id", 0);
        n.label = jn.value("label", "");
        n.side = jn.value("side", "");
        n.text = jn.value("text", "");
        n.col = jn.value("col", -1);
        if (jn.contains("box") && jn["box"].is_array() && jn["box"].size() == 4) {
            n.hasbox = true;
            for (int k = 0; k < 4; k++) n.box[k] = jn["box"][k].get<float>();
        }
        if (n.id >= g_note_next_id) g_note_next_id = n.id + 1;
        g_notes.push_back(n);
    }
}

static void save_notes()
{
    if (g_notes_path.empty()) return;
    json arr = json::array();
    for (auto &n : g_notes) {
        json jn = { {"id", n.id}, {"label", n.label}, {"side", n.side},
                    {"text", n.text}, {"col", n.col} };
        if (n.hasbox) jn["box"] = { (int)n.box[0], (int)n.box[1], (int)n.box[2], (int)n.box[3] };
        else jn["box"] = nullptr;
        arr.push_back(jn);
    }
    std::ofstream f(g_notes_path.c_str(), std::ios::trunc);
    if (!f) { fprintf(stderr, "notes: cannot write %s\n", g_notes_path.c_str()); return; }
    f << arr.dump(1);
}

static int col_of_label(const std::string &label)
{
    if (label.empty()) return -1;
    for (size_t i = 0; i < g_cols.size(); i++) if (g_cols[i].label == label) return (int)i;
    return -1;
}

static void finalize_draft_box()
{
    float W = g_draft.rmax.x - g_draft.rmin.x, H = g_draft.rmax.y - g_draft.rmin.y;
    auto cx = [&](float x){ float v = (x - g_draft.rmin.x) / W * g_w; return v < 0 ? 0 : (v > g_w ? g_w : v); };
    auto cy = [&](float y){ float v = (y - g_draft.rmin.y) / H * g_h; return v < 0 ? 0 : (v > g_h ? g_h : v); };
    float x0 = cx(g_draft.r0.x), x1 = cx(g_draft.r1.x), y0 = cy(g_draft.r0.y), y1 = cy(g_draft.r1.y);
    g_draft.box[0] = x0 < x1 ? x0 : x1; g_draft.box[1] = y0 < y1 ? y0 : y1;
    g_draft.box[2] = x0 < x1 ? x1 : x0; g_draft.box[3] = y0 < y1 ? y1 : y0;
    g_draft.hasbox = (g_draft.box[2] - g_draft.box[0] >= 3) || (g_draft.box[3] - g_draft.box[1] >= 3);
    g_draft.placing = false; g_draft.editing = true; g_draft.focus = true; g_draft.text[0] = 0;
}

static void commit_draft()
{
    Note n;
    n.id = g_note_next_id++; n.label = g_draft.label;
    n.side = g_draft.side == "frame" ? "" : g_draft.side;
    n.text = g_draft.text; n.col = g_draft.col; n.hasbox = g_draft.hasbox;
    for (int k = 0; k < 4; k++) n.box[k] = g_draft.box[k];
    g_notes.push_back(n); save_notes();
    g_draft = Draft{};
}

static bool load_view(const char *path)
{
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return false; }
    json m; try { f >> m; } catch (const std::exception &e) { fprintf(stderr, "json: %s\n", e.what()); return false; }

    g_scenario = m.value("scenario", "");
    g_anchor = m.value("anchor", "");
    g_verdict = m.value("verdict", "");
    g_note = m.value("note", "");
    g_has_state = m.value("has_state", false);
    g_notes_path = m.value("notes_path", "");
    std::string pc = m.value("port_container", ""), rc = m.value("retail_container", "");
    char err[128] = {0};
    g_port = olv3_open(pc.c_str(), err, sizeof err);
    if (!g_port) { fprintf(stderr, "port container: %s\n", err); return false; }
    // Retail is OPTIONAL — a port-only capture (e.g. --only port, or draw-step
    // verification before Phase 2b retail hooks) leaves an empty retail.olv3.
    // The diff/retail panels just stay blank; everything else (incl. draw-step)
    // works against the port alone.
    g_retail = olv3_open(rc.c_str(), err, sizeof err);
    g_w = olv3_width(g_port); g_h = olv3_height(g_port);
    if (g_retail && (olv3_width(g_retail) != g_w || olv3_height(g_retail) != g_h)) {
        fprintf(stderr, "port/retail dims differ\n"); return false;
    }
    if (!g_retail) fprintf(stderr, "retail container: %s (port-only view)\n", err);

    for (auto &jf : m["frames"]) {
        Col c;
        c.offset = jf.value("offset", 0);
        c.label = jf.value("label", "");
        c.gap = jf.value("gap", json()).is_string() ? jf.value("gap", "") : "";
        c.port_idx = jf.value("port_idx", json()).is_number() ? jf["port_idx"].get<int>() : -1;
        c.retail_idx = jf.value("retail_idx", json()).is_number() ? jf["retail_idx"].get<int>() : -1;
        c.port_present = jf.value("port_present", json()).is_number() ? jf["port_present"].get<int>() : -1;
        c.retail_present = jf.value("retail_present", json()).is_number() ? jf["retail_present"].get<int>() : -1;
        if (jf.contains("state") && jf["state"].is_object()) {
            c.has_state = true;
            c.sport   = jf["state"].value("port", json::object());
            c.sretail = jf["state"].value("retail", json::object());
        }
        g_cols.push_back(c);
    }
    if (g_cols.empty()) { fprintf(stderr, "view has no frames\n"); return false; }

    g_tport = make_tex(); g_tretail = make_tex(); g_tdiff = make_tex();
    g_diffbuf = (uint8_t *)malloc((size_t)g_w * g_h * 4);
    g_metric_cursor = 0;
    load_notes();
    show_column(0);
    return true;
}

// metric → green→yellow→red heat (absolute scale). `t` is a 0..1 normalised metric.
static ImU32 heat01(double t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int r = t < 0.5 ? (int)(510 * t) : 255;
    int g = t < 0.5 ? 200 : (int)(200 * (1 - (t - 0.5) * 2));
    return IM_COL32(r, g, 78, 255);
}
static double col_heat_t(const Col &c)
{
    if (g_heat_metric == 1) { if (c.idxdiff < 0) return -1; return c.idxdiff / (double)(g_w * g_h) * 12.0; }
    if (c.gt8 < 0) return -1;
    return c.meanabs / 6.0;
}
static int col_signal(const Col &c) { return g_heat_metric == 1 ? c.idxdiff : c.gt8; }

static void seek(int i) { if (i < 0) i = 0; if (i >= (int)g_cols.size()) i = (int)g_cols.size() - 1;
    if (i != g_cur) { g_pick_port = g_pick_retail = -2; show_column(i); } }   // pick is column-specific

// one panel: a labelled image (or a gap placeholder), scaled to `panel_w`. Tracks the
// hovered capture-pixel (for the index/RGB readout) and hosts note-mode crop boxes.
static void panel(const char *label, LPDIRECT3DTEXTURE9 tex, bool present, float panel_w,
                  const char *sidename = "")
{
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.55f, 0.72f, 0.92f, 1.0f), "%s", label);
    float h = panel_w * g_h / g_w;
    if (present) {
        ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(panel_w, h));
        ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        if (ImGui::IsItemHovered()) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            g_hover_x = (int)((mp.x - mn.x) / (mx.x - mn.x) * g_w);
            g_hover_y = (int)((mp.y - mn.y) / (mx.y - mn.y) * g_h);
            // cross-engine pixel→draw pick: left-click the port OR retail panel
            // (outside note mode) → owning draw on EACH engine (resolved after the
            // panel row). Available whenever a side carries draws.
            if (sidename && (std::string(sidename) == "port" || std::string(sidename) == "retail")
                && !g_note_mode && ImGui::IsMouseClicked(0)) {
                g_do_pick = true;
            }
        }
        // note mode: drag a crop box on THIS side
        if (g_note_mode && !g_notes_path.empty()) {
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0) && !g_draft.placing && !g_draft.editing) {
                g_draft.placing = true; g_draft.side = sidename; g_draft.col = g_cur;
                g_draft.label = g_cols[g_cur].label;
                g_draft.r0 = g_draft.r1 = ImGui::GetIO().MousePos; g_draft.rmin = mn; g_draft.rmax = mx;
            }
            if (g_draft.placing && g_draft.side == sidename) {
                g_draft.r1 = ImGui::GetIO().MousePos;
                dl->AddRect(g_draft.r0, g_draft.r1, IM_COL32(255, 210, 80, 255), 0, 0, 2.0f);
                if (ImGui::IsMouseReleased(0)) finalize_draft_box();
            }
        }
        if (g_show_notes) {
            const std::string &lbl = g_cols[g_cur].label;
            for (auto &n : g_notes) {
                bool here = n.hasbox && n.side == sidename &&
                            (lbl.empty() ? n.col == g_cur : n.label == lbl);
                if (!here) continue;
                ImVec2 a(mn.x + n.box[0] / g_w * (mx.x - mn.x), mn.y + n.box[1] / g_h * (mx.y - mn.y));
                ImVec2 b(mn.x + n.box[2] / g_w * (mx.x - mn.x), mn.y + n.box[3] / g_h * (mx.y - mn.y));
                dl->AddRect(a, b, IM_COL32(120, 224, 120, 255), 0, 0, 2.0f);
                char tag[16]; snprintf(tag, sizeof tag, "#%d", n.id);
                dl->AddText(ImVec2(a.x + 3, a.y + 2), IM_COL32(140, 240, 140, 255), tag);
            }
        }
        // hovered-pixel crosshair
        if (g_hover_x >= 0 && g_hover_y >= 0) {
            float hx = mn.x + (g_hover_x + 0.5f) / g_w * (mx.x - mn.x);
            float hy = mn.y + (g_hover_y + 0.5f) / g_h * (mx.y - mn.y);
            dl->AddLine(ImVec2(mn.x, hy), ImVec2(mx.x, hy), IM_COL32(255, 220, 80, 90));
            dl->AddLine(ImVec2(hx, mn.y), ImVec2(hx, mx.y), IM_COL32(255, 220, 80, 90));
        }
    } else {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + panel_w, p.y + h), IM_COL32(28, 24, 24, 255));
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + panel_w/2 - 30, p.y + h/2), IM_COL32(255, 180, 84, 255), "(gap)");
        ImGui::Dummy(ImVec2(panel_w, h));
    }
    ImGui::EndGroup();
}

static std::string fmt_state_val(const json &v)
{
    char b[48];
    if (v.is_null())           return "-";
    if (v.is_number_float())   { snprintf(b, sizeof b, "%.9g", v.get<double>()); return b; }
    if (v.is_number_integer()) { snprintf(b, sizeof b, "%lld", (long long)v.get<int64_t>()); return b; }
    if (v.is_boolean())        return v.get<bool>() ? "true" : "false";
    if (v.is_string())         return v.get<std::string>();
    return v.dump();
}

// the GAME-STATE panel - the save-schema slot snapshot, retail vs port, port≠retail
// rows highlighted, with a name filter + diffs-only toggle. Populated by --state.
static void game_state_panel(const Col &c)
{
    ImGui::Separator();
    if (!g_has_state) {
        ImGui::TextDisabled("game state: (re-capture with --state to carry the save-schema slot snapshot)");
        return;
    }
    std::vector<std::string> keys;
    for (auto it = c.sretail.begin(); it != c.sretail.end(); ++it) keys.push_back(it.key());
    for (auto it = c.sport.begin();   it != c.sport.end();   ++it)
        if (!c.sretail.contains(it.key())) keys.push_back(it.key());
    std::sort(keys.begin(), keys.end());
    int ndiff = 0;
    for (auto &k : keys)
        if (c.sport.contains(k) && c.sretail.contains(k) && c.sport[k] != c.sretail[k]) ndiff++;
    char hdr[96];
    snprintf(hdr, sizeof hdr, "game state - %d fields, %d differ###gamestate", (int)keys.size(), ndiff);
    ImGui::SetNextItemOpen(g_show_state, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader(hdr)) return;
    if (!c.has_state) { ImGui::TextDisabled("(no engine state at this column - a gap, or outside the state window)"); return; }
    ImGui::SetNextItemWidth(160); ImGui::InputText("filter##state", g_state_filter, sizeof g_state_filter);
    ImGui::SameLine(); ImGui::Checkbox("diffs only", &g_state_diff_only);
    ImGui::SameLine(); ImGui::TextDisabled("(red = port != retail)");
    if (ImGui::BeginTable("gstate", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame |
                          ImGuiTableFlags_ScrollY, ImVec2(0, 210))) {
        ImGui::TableSetupColumn("field"); ImGui::TableSetupColumn("retail"); ImGui::TableSetupColumn("port");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (auto &k : keys) {
            if (g_state_filter[0] && k.find(g_state_filter) == std::string::npos) continue;
            bool hp = c.sport.contains(k), hr = c.sretail.contains(k);
            bool differ = hp && hr && c.sport[k] != c.sretail[k];
            if (g_state_diff_only && !differ) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k.c_str());
            ImVec4 col = differ ? ImVec4(1, 0.42f, 0.42f, 1) : ImVec4(0.84f, 0.86f, 0.88f, 1);
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%s", hr ? fmt_state_val(c.sretail[k]).c_str() : "-");
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%s", hp ? fmt_state_val(c.sport[k]).c_str() : "-");
        }
        ImGui::EndTable();
    }
}

// the PALETTE panel - port + retail 256-colour palettes as 16×16 swatch grids,
// differing entries ringed red. The palette-builder divergence story the index
// diff complements (right index + wrong palette colour = a palette-builder gap).
static void palette_panel(const Col &c)
{
    const uint8_t *pp = c.port_idx   >= 0 ? olv3_palette(g_port,   c.port_idx)   : nullptr;
    const uint8_t *rp = c.retail_idx >= 0 ? olv3_palette(g_retail, c.retail_idx) : nullptr;
    char hdr[80]; snprintf(hdr, sizeof hdr, "palette - %d / 256 entries differ###pal",
                           c.paldiff < 0 ? 0 : c.paldiff);
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);   // palette divergence is a known bug class here
    if (!ImGui::CollapsingHeader(hdr)) return;
    const float sw = 11.0f;   // swatch size
    auto grid = [&](const char *title, const uint8_t *pal, const uint8_t *other) {
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", title);
        ImVec2 o = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        if (pal) for (int i = 0; i < 256; i++) {
            int gx = i & 15, gy = i >> 4;
            ImVec2 a(o.x + gx * sw, o.y + gy * sw), b(a.x + sw - 1, a.y + sw - 1);
            dl->AddRectFilled(a, b, IM_COL32(pal[i*4], pal[i*4+1], pal[i*4+2], 255));
            bool diff = other && (pal[i*4]!=other[i*4] || pal[i*4+1]!=other[i*4+1] || pal[i*4+2]!=other[i*4+2]);
            if (diff) dl->AddRect(ImVec2(a.x-0.5f,a.y-0.5f), ImVec2(b.x+1,b.y+1), IM_COL32(255,40,40,255), 0,0,1.5f);
        }
        ImGui::Dummy(ImVec2(16 * sw, 16 * sw));
        ImGui::EndGroup();
    };
    grid("retail", rp, pp); ImGui::SameLine(0, 24); grid("port", pp, rp);
    // hovered-pixel index → palette entry, both sides
    if (g_hover_x >= 0 && g_hover_y >= 0) {
        const uint8_t *pi = c.port_idx   >= 0 ? olv3_indices(g_port,   c.port_idx)   : nullptr;
        const uint8_t *ri = c.retail_idx >= 0 ? olv3_indices(g_retail, c.retail_idx) : nullptr;
        size_t off = (size_t)g_hover_y * g_w + g_hover_x;
        int piv = pi ? pi[off] : -1, riv = ri ? ri[off] : -1;
        ImGui::Text("pixel (%d,%d):", g_hover_x, g_hover_y); ImGui::SameLine();
        if (riv >= 0) ImGui::TextColored(ImVec4(0.84f,0.86f,0.88f,1), "retail idx=%d rgb=(%d,%d,%d)",
            riv, rp[riv*4], rp[riv*4+1], rp[riv*4+2]); ImGui::SameLine();
        ImVec4 pc = (piv != riv) ? ImVec4(1,0.42f,0.42f,1) : ImVec4(0.84f,0.86f,0.88f,1);
        if (piv >= 0) ImGui::TextColored(pc, " · port idx=%d rgb=(%d,%d,%d)",
            piv, pp[piv*4], pp[piv*4+1], pp[piv*4+2]);
    }
}

static void draw_ui()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGui::Begin("##v3", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (g_cols.empty()) { ImGui::Text("no view loaded - usage: viewer.exe <view.json>"); ImGui::End(); return; }
    Col &c = g_cols[g_cur];

    // header
    bool aligned = g_verdict.rfind("ALIGNED", 0) == 0;
    ImGui::TextColored(ImVec4(0.42f, 0.71f, 1.0f, 1.0f), "trace studio v3");
    ImGui::SameLine(); ImGui::Text("· %s · %s ·", g_scenario.c_str(), g_anchor.c_str());
    ImGui::SameLine(); ImGui::TextColored(aligned ? ImVec4(0.41f,0.82f,0.51f,1) : ImVec4(1,0.71f,0.33f,1), "%s", g_verdict.c_str());
    ImGui::SameLine(); ImGui::Text("· %dx%d · %d cols · %.0f fps", g_w, g_h, (int)g_cols.size(), ImGui::GetIO().Framerate);
    if (int mp = metrics_pending()) {
        ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "· filling diff metrics (%d left)", mp);
    }
    if (!g_note.empty()) ImGui::TextDisabled("%s", g_note.c_str());

    // toolbar: panel toggles + diff mode + scrub
    const char *names[3] = {"port", "retail", "diff"};
    for (int k = 0; k < 3; k++) { if (k) ImGui::SameLine(); ImGui::Checkbox(names[k], &g_show[k]); }
    ImGui::SameLine(); ImGui::TextDisabled("| diff:");
    ImGui::SameLine(); if (ImGui::RadioButton("index", &g_diff_mode, 1)) show_column(g_cur);
    ImGui::SameLine(); if (ImGui::RadioButton("rgb", &g_diff_mode, 0)) show_column(g_cur);
    ImGui::SameLine(); ImGui::TextDisabled("| heat:");
    ImGui::SameLine(); ImGui::RadioButton("idx##h", &g_heat_metric, 1);
    ImGui::SameLine(); ImGui::RadioButton("rgb##h", &g_heat_metric, 0);

    ImGui::SameLine(); ImGui::TextDisabled("|");
    ImGui::SameLine(); if (ImGui::Button("|<")) seek(0);
    ImGui::SameLine(); if (ImGui::Button("<")) seek(g_cur - 1);
    int sel = g_cur; ImGui::SameLine(); ImGui::SetNextItemWidth(-220);
    if (ImGui::SliderInt("##scrub", &sel, 0, (int)g_cols.size() - 1)) seek(sel);
    ImGui::SameLine(); if (ImGui::Button(">")) seek(g_cur + 1);
    ImGui::SameLine(); if (ImGui::Button(">|")) seek((int)g_cols.size() - 1);
    ImGui::SameLine(); ImGui::Text("col %d/%d · %s", g_cur, (int)g_cols.size() - 1,
                     c.label.size() ? c.label.c_str() : std::to_string(c.offset).c_str());

    // notes toolbar + inline draft editor
    ImGui::BeginDisabled(g_notes_path.empty());
    ImGui::Checkbox("note mode (m)", &g_note_mode);
    ImGui::SameLine(); ImGui::Checkbox("show notes", &g_show_notes);
    ImGui::SameLine(); if (ImGui::Button("note frame")) {
        g_draft = Draft{}; g_draft.editing = true; g_draft.focus = true;
        g_draft.side = "frame"; g_draft.col = g_cur; g_draft.label = g_cols[g_cur].label;
    }
    ImGui::EndDisabled();
    if (g_notes_path.empty()) {
        ImGui::SameLine(); ImGui::TextDisabled("(notes need a view.json with notes_path)");
    } else if (g_note_mode && !g_draft.editing) {
        ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "drag a box on a panel to flag a region");
    }
    if (g_draft.editing) {
        ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "NEW NOTE");
        ImGui::SameLine(); ImGui::Text("@ %s [%s]%s:",
            g_draft.label.empty() ? ("col " + std::to_string(g_draft.col)).c_str() : g_draft.label.c_str(),
            g_draft.side == "frame" ? "frame" : g_draft.side.c_str(),
            g_draft.hasbox ? "" : " whole frame");
        if (g_draft.focus) { ImGui::SetKeyboardFocusHere(); g_draft.focus = false; }
        ImGui::SetNextItemWidth(-220);
        bool enter = ImGui::InputText("##ntext", g_draft.text, sizeof g_draft.text, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine(); bool save = ImGui::Button("save");
        ImGui::SameLine(); bool cancel = ImGui::Button("cancel");
        if (cancel) g_draft = Draft{};
        else if ((enter || save) && g_draft.text[0]) commit_draft();
    }

    // draw-step controls (v2 per-draw write-deltas) — when EITHER engine has draws
    int port_nd   = c.port_idx   >= 0 ? olv3_draws(g_port,   c.port_idx)   : 0; if (port_nd   < 0) port_nd   = 0;
    int retail_nd = c.retail_idx >= 0 ? olv3_draws(g_retail, c.retail_idx) : 0; if (retail_nd < 0) retail_nd = 0;
    if (port_nd > 0 || retail_nd > 0) {
        ImGui::Separator();
        if (ImGui::Checkbox("draw-step (d)", &g_drawstep)) show_column(g_cur);
        ImGui::SameLine(); ImGui::TextDisabled("| step:");
        ImGui::SameLine(); if (ImGui::RadioButton("port##ds", &g_ds_side, 0)) show_column(g_cur);
        ImGui::SameLine(); if (ImGui::RadioButton("retail##ds", &g_ds_side, 1)) show_column(g_cur);
        ImGui::SameLine(); ImGui::TextDisabled("| %d draws (port %d / retail %d)", g_draw_n, port_nd, retail_nd);
        Olv3Replay *rep = ds_replay();
        int didx = ds_idx(c);
        if (g_drawstep && g_draw_n > 0) {
            ImGui::SameLine(); if (ImGui::Checkbox("solo (s)", &g_draw_solo)) show_column(g_cur);
            if (!g_draw_solo) {
                ImGui::SameLine(); if (ImGui::Button("|<##d")) { g_draw_k = 0; show_column(g_cur); }
                ImGui::SameLine(); if (ImGui::Button("<##d"))  { if (g_draw_k > 0) { g_draw_k--; show_column(g_cur); } }
                int k = g_draw_k; ImGui::SameLine(); ImGui::SetNextItemWidth(-360);
                if (ImGui::SliderInt("##dk", &k, 0, g_draw_n)) { g_draw_k = k; show_column(g_cur); }
                ImGui::SameLine(); if (ImGui::Button(">##d"))  { if (g_draw_k < g_draw_n) { g_draw_k++; show_column(g_cur); } }
                ImGui::SameLine(); if (ImGui::Button(">|##d")) { g_draw_k = g_draw_n; show_column(g_cur); }
                ImGui::SameLine(); ImGui::Text("k=%d", g_draw_k);
            } else {
                int s = g_draw_sel; ImGui::SameLine(); ImGui::SetNextItemWidth(-200);
                if (ImGui::SliderInt("##dsel", &s, 0, g_draw_n - 1)) { g_draw_sel = s; g_draw_scroll = true; show_column(g_cur); }
            }
        }
        // cross-engine pixel pick — the owning draw on EACH engine (click any panel)
        if (g_pick_port != -2 || g_pick_retail != -2) {
            ImGui::Text("pick (%d,%d):", g_pick_x, g_pick_y);
            ImGui::SameLine(); pick_readout("port", g_port, c.port_idx, g_pick_port);
            ImGui::SameLine(); ImGui::TextDisabled("|");
            ImGui::SameLine(); pick_readout("retail", g_retail, c.retail_idx, g_pick_retail);
        }
        // the draw list for the stepped side — click a row to build up to (+select) it
        if (g_drawstep && g_draw_n > 0 && didx >= 0 && ImGui::BeginChild("##drawlist", ImVec2(0, 120), true)) {
            ImGuiListClipper clip; clip.Begin(g_draw_n);
            while (clip.Step()) {
                for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
                    int op = 6, m0 = 0, m1 = 0, m2 = 0;
                    olv3_draw_meta(rep, didx, k, &op, &m0, &m1, &m2);
                    bool active = g_draw_solo ? (k == g_draw_sel) : (k < g_draw_k);
                    bool isSel  = (k == g_draw_sel);
                    ImGui::PushStyleColor(ImGuiCol_Text, active ? op_color(op) : IM_COL32(105, 105, 112, 255));
                    char buf[112];
                    snprintf(buf, sizeof buf, "#%-4d %-6s  m0=%-5d (%4d,%4d)", k, OPN[op & 7], m0, m1, m2);
                    if (ImGui::Selectable(buf, isSel)) {
                        g_draw_sel = k;
                        if (!g_draw_solo) g_draw_k = k + 1;   // build up to and including this draw
                        show_column(g_cur);
                    }
                    ImGui::PopStyleColor();
                    if (isSel && g_draw_scroll) { ImGui::SetScrollHereY(0.5f); }
                }
            }
            g_draw_scroll = false;
            ImGui::EndChild();
        }
    }

    // 3-panel row
    g_hover_x = g_hover_y = -1;
    int nshow = (g_show[0] ? 1 : 0) + (g_show[1] ? 1 : 0) + (g_show[2] ? 1 : 0);
    if (nshow < 1) nshow = 1;
    float avail = ImGui::GetContentRegionAvail().x;
    float pw = (avail - (nshow - 1) * 8.0f) / nshow;
    bool first = true;
    if (g_show[0]) { panel("port", g_tport, c.port_idx >= 0, pw, "port"); first = false; }
    if (g_show[1]) { if (!first) ImGui::SameLine(); panel("retail", g_tretail, c.retail_idx >= 0, pw, "retail"); first = false; }
    if (g_show[2]) { if (!first) ImGui::SameLine(); panel(g_diff_mode ? "diff (index: red=differ)" : "diff (rgb x6)",
                     g_tdiff, c.port_idx >= 0 && c.retail_idx >= 0, pw, "diff"); }

    // resolve a pending pixel pick (panel() set g_do_pick + g_hover on click): the
    // draw that last painted (x,y) on EACH engine — "which draw produced this pixel,
    // and does it differ port↔retail?". Uses all draws (k=-1) = final-frame owner.
    if (g_do_pick) {
        g_do_pick = false;
        if (g_hover_x >= 0 && g_hover_y >= 0) {
            g_pick_x = g_hover_x; g_pick_y = g_hover_y;
            g_pick_port   = (c.port_idx   >= 0 && olv3_draws(g_port,   c.port_idx)   > 0)
                            ? olv3_pick(g_port,   c.port_idx,   -1, g_pick_x, g_pick_y) : -2;
            g_pick_retail = (c.retail_idx >= 0 && olv3_draws(g_retail, c.retail_idx) > 0)
                            ? olv3_pick(g_retail, c.retail_idx, -1, g_pick_x, g_pick_y) : -2;
            int own = g_ds_side ? g_pick_retail : g_pick_port;   // sync stepped side's selection
            if (own >= 0) { g_draw_sel = own; g_draw_scroll = true; if (g_draw_solo) show_column(g_cur); }
        }
    }

    // diff ribbon (one cell per column, heat = selected metric, click to seek)
    ImGui::Spacing();
    ImVec2 rp = ImGui::GetCursorScreenPos();
    float rw = ImGui::GetContentRegionAvail().x, rh = 16, cw = rw / g_cols.size();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < g_cols.size(); i++) {
        double t = col_heat_t(g_cols[i]);
        ImU32 col = g_cols[i].gap.size() ? IM_COL32(60, 60, 70, 255)
                  : t < 0 ? IM_COL32(17, 22, 28, 255) : heat01(t);
        dl->AddRectFilled(ImVec2(rp.x + i * cw, rp.y), ImVec2(rp.x + (i + 1) * cw, rp.y + rh), col);
    }
    dl->AddRect(ImVec2(rp.x + g_cur * cw - 1, rp.y - 1), ImVec2(rp.x + (g_cur + 1) * cw + 1, rp.y + rh + 1), IM_COL32(108, 182, 255, 255), 0, 0, 2);
    ImGui::InvisibleButton("##ribbon", ImVec2(rw, rh));
    if (ImGui::IsItemActive() || ImGui::IsItemClicked())
        seek((int)((ImGui::GetIO().MousePos.x - rp.x) / cw));

    // worst / next
    int worst = 0; for (size_t i = 0; i < g_cols.size(); i++) if (col_signal(g_cols[i]) > col_signal(g_cols[worst])) worst = (int)i;
    if (ImGui::Button("worst (w)")) seek(worst);
    ImGui::SameLine(); if (ImGui::Button("next>0 (n)")) { for (size_t i = g_cur + 1; i < g_cols.size(); i++) if (col_signal(g_cols[i]) > 0) { seek((int)i); break; } }
    ImGui::SameLine(); ImGui::TextDisabled("worst: %s %s=%dpx",
        g_cols[worst].label.size() ? g_cols[worst].label.c_str() : std::to_string(g_cols[worst].offset).c_str(),
        g_heat_metric ? "idxdiff" : "gt8", col_signal(g_cols[worst]));

    ImGui::Separator();
    ImGui::Text("identity: %s   |   index-diff %d px · palette-diff %d · RGB gt8 %d px meanabs %.4f max|d| %d%s",
                c.label.size() ? c.label.c_str() : (g_anchor + "+" + std::to_string(c.offset)).c_str(),
                c.idxdiff, c.paldiff, c.gt8, c.meanabs, c.maxd, c.gap.size() ? "  (GAP)" : "");
    if (ImGui::BeginTable("pstate", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("retail present"); ImGui::TableSetupColumn("port present");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("%d", c.retail_present);
        ImGui::TableNextColumn(); ImGui::Text("%d", c.port_present);
        ImGui::EndTable();
    }

    palette_panel(c);
    game_state_panel(c);

    // notes list
    if (!g_notes_path.empty()) {
        ImGui::Separator();
        char hdr[80]; snprintf(hdr, sizeof hdr, "notes (%d)###notes", (int)g_notes.size());
        if (ImGui::CollapsingHeader(hdr)) {
            if (g_notes.empty())
                ImGui::TextDisabled("none yet - toggle 'note mode' + drag a box on a panel, or 'note frame'.");
            int del = -1;
            for (size_t i = 0; i < g_notes.size(); i++) {
                Note &n = g_notes[i];
                ImGui::PushID((int)i);
                if (ImGui::SmallButton("seek")) { int ci = col_of_label(n.label); seek(ci >= 0 ? ci : n.col); }
                ImGui::SameLine(); if (ImGui::SmallButton("del")) del = (int)i;
                ImGui::SameLine();
                bool oncur = n.label.empty() ? n.col == g_cur : n.label == g_cols[g_cur].label;
                ImGui::TextColored(oncur ? ImVec4(0.55f, 0.9f, 0.55f, 1) : ImVec4(0.8f, 0.82f, 0.84f, 1),
                    "#%d %s%s%s - %s", n.id, n.side.empty() ? "frame" : n.side.c_str(),
                    n.label.empty() ? "" : (" @" + n.label).c_str(),
                    n.hasbox ? "" : " (whole)", n.text.c_str());
                ImGui::PopID();
            }
            if (del >= 0) { g_notes.erase(g_notes.begin() + del); save_notes(); }
        }
    }

    ImGui::TextDisabled("keys: ,/. ±1 · arrows ±10 · Home/End · 1/2/3 panels · i index/rgb diff · w worst · n next · m note mode");
    ImGui::End();
}

static bool save_bmp(const char *path, const uint8_t *bgra, int W, int H, int pitch)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const uint32_t img = (uint32_t)W * H * 4, off = 14 + 40;
    uint8_t hdr[54] = {0};
    hdr[0]='B'; hdr[1]='M'; *(uint32_t*)&hdr[2]=off+img; *(uint32_t*)&hdr[10]=off;
    *(uint32_t*)&hdr[14]=40; *(int32_t*)&hdr[18]=W; *(int32_t*)&hdr[22]=H;
    *(uint16_t*)&hdr[26]=1; *(uint16_t*)&hdr[28]=32; *(uint32_t*)&hdr[34]=img;
    fwrite(hdr, 1, 54, f);
    for (int r = H - 1; r >= 0; r--) fwrite(bgra + (size_t)r * pitch, 1, (size_t)W * 4, f);
    fclose(f);
    return true;
}

static void imgui_init(HWND hwnd)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_dev);
}

static void begin_frame() { ImGui_ImplDX9_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame(); }
static void end_frame()
{
    ImGui::EndFrame(); ImGui::Render();
    g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(16, 18, 22), 1.0f, 0);
    if (SUCCEEDED(g_dev->BeginScene())) { ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData()); g_dev->EndScene(); }
}

static void shutdown_all()
{
    if (g_port) olv3_close(g_port);
    if (g_retail) olv3_close(g_retail);
    free(g_diffbuf);
    if (g_tport) g_tport->Release();
    if (g_tretail) g_tretail->Release();
    if (g_tdiff) g_tdiff->Release();
    ImGui_ImplDX9_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    destroy_device();
}
static void destroy_device()
{
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
    if (g_d3d) { g_d3d->Release(); g_d3d = nullptr; }
}

static int do_shot(const char *out, const char *view, int W, int H, int col)
{
    WNDCLASSEXA wc = {sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "olv3viewer_shot"; RegisterClassExA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "olv3", WS_OVERLAPPEDWINDOW, 0, 0, W, H, nullptr, nullptr, wc.hInstance, nullptr);
    if (!create_device(hwnd, W, H)) { fprintf(stderr, "CreateDevice failed\n"); return 2; }
    imgui_init(hwnd);
    if (view && !load_view(view)) return 2;
    precompute_metrics();
    if (col > 0) seek(col);
    begin_frame(); draw_ui(); end_frame();

    IDirect3DSurface9 *bb = nullptr, *sys = nullptr; int rc = 2;
    if (SUCCEEDED(g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb
     && SUCCEEDED(g_dev->CreateOffscreenPlainSurface(W, H, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr)) && sys
     && SUCCEEDED(g_dev->GetRenderTargetData(bb, sys))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            rc = save_bmp(out, (const uint8_t *)lr.pBits, W, H, lr.Pitch) ? 0 : 2;
            sys->UnlockRect();
        }
    }
    if (sys) sys->Release();
    if (bb) bb->Release();
    shutdown_all(); DestroyWindow(hwnd);
    fprintf(stderr, rc == 0 ? "shot -> %s (%dx%d)\n" : "shot FAILED\n", out, W, H);
    return rc;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static LRESULT WINAPI WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp)) return true;
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (msg == WM_SIZE && g_dev && wp != SIZE_MINIMIZED) {
        g_pp.BackBufferWidth = LOWORD(lp); g_pp.BackBufferHeight = HIWORD(lp);
        ImGui_ImplDX9_InvalidateDeviceObjects();
        g_dev->Reset(&g_pp);
        ImGui_ImplDX9_CreateDeviceObjects();
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void handle_keys()
{
    if (g_cols.empty()) return;
    if (ImGui::IsAnyItemActive()) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Comma)) seek(g_cur - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_Period)) seek(g_cur + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) seek(g_cur - 10);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) seek(g_cur + 10);
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) seek(0);
    if (ImGui::IsKeyPressed(ImGuiKey_End)) seek((int)g_cols.size() - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_1)) g_show[0] = !g_show[0];
    if (ImGui::IsKeyPressed(ImGuiKey_2)) g_show[1] = !g_show[1];
    if (ImGui::IsKeyPressed(ImGuiKey_3)) g_show[2] = !g_show[2];
    if (ImGui::IsKeyPressed(ImGuiKey_I)) { g_diff_mode ^= 1; show_column(g_cur); }
    if (ImGui::IsKeyPressed(ImGuiKey_M) && !g_notes_path.empty()) g_note_mode = !g_note_mode;
    if (ImGui::IsKeyPressed(ImGuiKey_W)) { int wb = 0; for (size_t i = 0; i < g_cols.size(); i++) if (col_signal(g_cols[i]) > col_signal(g_cols[wb])) wb = (int)i; seek(wb); }
    if (ImGui::IsKeyPressed(ImGuiKey_N)) { for (size_t i = g_cur + 1; i < g_cols.size(); i++) if (col_signal(g_cols[i]) > 0) { seek((int)i); break; } }
    // draw-stepping: d = toggle, s = solo, [ / ] = step the build-up
    if (g_draw_n > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_D)) { g_drawstep = !g_drawstep; show_column(g_cur); }
        if (g_drawstep && ImGui::IsKeyPressed(ImGuiKey_S)) { g_draw_solo = !g_draw_solo; show_column(g_cur); }
        if (g_drawstep && !g_draw_solo) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)  && g_draw_k > 0)         { g_draw_k--; show_column(g_cur); }
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket) && g_draw_k < g_draw_n)  { g_draw_k++; show_column(g_cur); }
        }
        if (g_drawstep && g_draw_solo) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)  && g_draw_sel > 0)            { g_draw_sel--; g_draw_scroll = true; show_column(g_cur); }
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket) && g_draw_sel < g_draw_n - 1) { g_draw_sel++; g_draw_scroll = true; show_column(g_cur); }
        }
    }
}

static int do_interactive(const char *view)
{
    WNDCLASSEXA wc = {sizeof(wc)};
    wc.style = CS_CLASSDC; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "olv3viewer"; RegisterClassExA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "trace studio v3", WS_OVERLAPPEDWINDOW,
                              60, 40, 1400, 940, nullptr, nullptr, wc.hInstance, nullptr);
    RECT crc; GetClientRect(hwnd, &crc);
    UINT cw = crc.right - crc.left, ch = crc.bottom - crc.top;
    if (!create_device(hwnd, cw ? cw : 1400, ch ? ch : 940)) { fprintf(stderr, "CreateDevice failed\n"); return 2; }
    imgui_init(hwnd);
    if (view && !load_view(view)) { fprintf(stderr, "load_view failed\n"); return 2; }
    ShowWindow(hwnd, SW_SHOWDEFAULT); UpdateWindow(hwnd);

    bool running = true;
    while (running) {
        MSG m;
        while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m); DispatchMessageA(&m);
            if (m.message == WM_QUIT) running = false;
        }
        if (!running) break;
        begin_frame(); handle_keys(); draw_ui(); end_frame();
        pump_metrics(8.0);
        g_dev->Present(nullptr, nullptr, nullptr, nullptr);
    }
    shutdown_all(); DestroyWindow(hwnd);
    return 0;
}

int main(int argc, char **argv)
{
    const char *shot = nullptr, *view = nullptr;
    int col = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = argv[++i];
        else if (strcmp(argv[i], "--col") == 0 && i + 1 < argc) col = atoi(argv[++i]);
        else if (strcmp(argv[i], "--rgb") == 0) g_diff_mode = 0;
        else view = argv[i];
    }
    if (shot) return do_shot(shot, view, 1400, 920, col);
    return do_interactive(view);
}
