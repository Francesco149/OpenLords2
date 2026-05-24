// tools/frida/openlords2-agent.js
//
// Frida agent for the harness against the retail Lords2.exe.
//
// Same conventions as the sibling projects (OpenMare, openrecet): one
// module-level `init({...})` RPC sets every behavior, and the agent emits
// `kind`-tagged JSON events over send() that the Python driver consumes.
//
// Phase A — non-engine-specific hooks (no Ghidra-derived addresses yet).
// Everything here is keyed on imported DLL exports (user32, kernel32,
// winmm, ddraw, dsound), which is robust across Lords2.exe rebuilds and
// patch versions:
//
//   user32!CreateWindowEx{A,W}        — track owned HWNDs
//   user32!ShowWindow                 — rewrite SHOW→HIDE on owned HWNDs
//   user32!ShowWindowAsync            — same
//   user32!SetWindowPos               — strip SWP_SHOWWINDOW on owned HWNDs
//   user32!SetWindowLong{A,W}         — strip WS_VISIBLE on GWL_STYLE writes
//   user32!ChangeDisplaySettings*     — return DISP_CHANGE_SUCCESSFUL no-op
//   user32!MessageBox{A,W}            — capture body, return IDOK (always on)
//   kernel32!Sleep                    — no-op (turbo v1) when enabled
//   ddraw!DirectDrawCreate            — wrap IDirectDraw::SetCooperativeLevel
//   ddraw!DirectDrawCreateEx          — wrap IDirectDraw7::SetCooperativeLevel
//   dsound!DirectSoundCreate          — wrap IDirectSound::SetCooperativeLevel
//                                        + IDirectSoundBuffer::SetVolume clamp
//   winmm!waveOutSetVolume            — clamp to 0 (silent_audio)
//
// As Ghidra resolves the engine addresses (main-loop body, frame counter,
// audio init wrappers), we extend with engine-internal hooks behind init
// flags.  This file matches the patterns OpenMare's openmare-agent.js
// uses; refer there for the full Phase B/C extensions.
//
// Message protocol (stringified JSON via send()):
//   {kind:"ready",  module, base, fridaVersion, ...flags}
//   {kind:"log",    msg}
//   {kind:"error",  where, msg}
//   {kind:"hwnd_owned",         hwnd, source}
//   {kind:"hwnd_owned_detail",  hwnd, api, cls, title}
//   {kind:"hide_force",         api, hwnd, original}
//   {kind:"display_settings_blocked", api, flags}
//   {kind:"messagebox_intercepted",  api, hwnd, caption, body, type}
//   {kind:"ddraw_cooplevel_rewrite", iface, hwnd, original, forced}
//   {kind:"dsound_cooplevel_rewrite", iface, hwnd, original, forced}
//   {kind:"audio_mute",         api, original}

'use strict';

// ─── module identity ────────────────────────────────────────────────────

const MODULE_NAME = 'Lords2.exe';
const IMAGE_BASE  = ptr('0x00400000');
let   g_target_module_name = MODULE_NAME;

// ─── retail address table (Lords2.exe, build verified 2026-05-24) ──────
// VA → resolved against the actual module base via rva() at hook time.
// Cross-referenced in docs/findings/main-loop-tick.md.
const ADDR = {
    fn_engine_tick:    0x004b99c0,  // FUN_004b99c0 — per-tick driver (sim + render)
    // FPS-counter globals (read once per engine second-rollover to publish
    // an `fps_sample` event for turbo verification).
    var_fps_current:   0x004ea174,  // DAT_004ea174 — current FPS (refreshed once / sec)
    var_fps_cum_sum:   0x004ea0f4,  // DAT_004ea0f4 — cumulative FPS sum (for avg)
    var_fps_cum_secs:  0x004ea83c,  // DAT_004ea83c — cumulative seconds counter
};

// ─── runtime state ──────────────────────────────────────────────────────

let g_base = null;
let g_start_real_ms = Date.now();

let g_hide_window         = false;
let g_owned_hwnds         = {};   // key = hwnd hex, val = {source}

let g_silent_audio_enabled = false;
let g_force_windowed       = false;
let g_turbo_enabled        = false;

// Turbo v2 — virtualised winmm!timeGetTime, gated to the engine main thread.
// `g_virtual_now_ms` is bumped by `g_turbo_step_ms` on every per-tick driver
// entry; main-thread timeGetTime calls have their retval overwritten with
// it. Side threads (DPlay, Smacker, DSound mixer) keep wall-clock. See the
// rationale block above installVirtualClockHook(). Mirrors OpenMare's
// pattern (openmare-agent.js installFrameCounterHook / installVirtualClockHook).
let g_turbo_step_ms       = 17;
let g_virtual_now_ms      = 0;
let g_clock_virtualised   = false;
let g_main_thread_id      = -1;   // captured on first per-tick driver entry

// FPS sample emission. Once the engine's cumulative_seconds counter ticks,
// we publish the engine's FPS readout so the harness can plot the trajectory
// without having to spelunk memory itself.
let g_last_fps_cum_secs   = 0;
let g_fps_sample_count    = 0;

// ─── helpers ────────────────────────────────────────────────────────────

function rva(va) { return g_base.add(va - IMAGE_BASE.toUInt32()); }
function nowMs()  { return (Date.now() - g_start_real_ms) | 0; }
function log(msg) { send({kind: 'log',   msg: String(msg)}); }
function err(where, msg) { send({kind: 'error', where: String(where), msg: String(msg)}); }

function vtableSlot(thisPtr, idx) {
    return thisPtr.readPointer().add(idx * Process.pointerSize).readPointer();
}

// ─── owned-HWND tracking + window-hide multi-API hooks ──────────────────
// SW_HIDE = 0, SW_SHOW = 5 (WinUser.h, fixed since 9x).
// SWP_SHOWWINDOW = 0x40, SWP_HIDEWINDOW = 0x80.
// GWL_STYLE = -16, WS_VISIBLE = 0x10000000.
//
// "Process-owned" HWNDs = those returned from a user32!CreateWindowEx{A,W}
// we observed.  This excludes MessageBox / system error popups (kernel-
// internal create path) so they still surface for the human running the
// harness.  We rewrite all four show-path APIs (ShowWindow, ShowWindowAsync,
// SetWindowPos+SWP_SHOWWINDOW, SetWindowLong+WS_VISIBLE) when targeting
// owned HWNDs.

const SW_HIDE          = 0;
const SW_SHOW          = 5;
const SWP_SHOWWINDOW   = 0x40;
const SWP_HIDEWINDOW   = 0x80;
const GWL_STYLE        = -16;
const WS_VISIBLE       = 0x10000000;

function hwndKey(h) { return h.toString(); }
function isOwnedHwnd(h) {
    return h && !h.isNull() && hwndKey(h) in g_owned_hwnds;
}

function markOwnedHwnd(h, source) {
    if (!h || h.isNull()) return;
    const k = hwndKey(h);
    if (k in g_owned_hwnds) return;
    g_owned_hwnds[k] = {source: source};
    send({kind: 'hwnd_owned', hwnd: k, source: source});
}

// CreateWindowEx argv (x86 stdcall, args[0..11]):
//   0  dwExStyle
//   1  lpClassName
//   2  lpWindowName
//   3  dwStyle       ← we strip WS_VISIBLE here if g_hide_window
//   4  x
//   5  y
//   6  nWidth
//   7  nHeight
//   8  hWndParent
//   9  hMenu
//   10 hInstance
//   11 lpParam
//
// Why strip WS_VISIBLE at create time rather than only hook ShowWindow:
// Lords2.exe (and many 1996-era engines) pass WS_VISIBLE to CreateWindowEx
// directly, so the window is shown *during* the create call — by the
// time our onLeave fires to register the hwnd in g_owned_hwnds, the
// window has already been displayed on screen.  ShowWindow never gets
// called separately, so a hide_window hook on ShowWindow alone is
// insufficient.  Verified 2026-05-24 against retail Lords2.exe: a
// borderless black window appeared during init despite all four
// show-path hooks being armed.

function installCreateWindowExHooks() {
    const u32 = Process.findModuleByName('user32.dll');
    if (!u32) { err('installCreateWindowExHooks', 'user32.dll not loaded'); return; }
    let count = 0;
    ['CreateWindowExA', 'CreateWindowExW'].forEach(function (name) {
        const fn = u32.findExportByName(name);
        if (!fn) { err('installCreateWindowExHooks', name + ' not found'); return; }
        Interceptor.attach(fn, {
            onEnter: function (args) {
                this.cls = null;
                this.wn  = null;
                this.stripped_visible = false;
                try {
                    const clsArg = args[1];
                    if (clsArg && !clsArg.isNull()) {
                        const lo = clsArg.toUInt32() & 0xffff;
                        const hi = (clsArg.toUInt32() >>> 16) & 0xffff;
                        if (hi === 0 && lo !== 0) {
                            this.cls = '#atom_' + lo.toString(16);
                        } else {
                            this.cls = (name === 'CreateWindowExA')
                                ? clsArg.readCString()
                                : clsArg.readUtf16String();
                        }
                    }
                    const wnArg = args[2];
                    if (wnArg && !wnArg.isNull()) {
                        this.wn = (name === 'CreateWindowExA')
                            ? wnArg.readCString()
                            : wnArg.readUtf16String();
                    }
                } catch (_) { /* unreadable strings are harmless */ }

                // Strip WS_VISIBLE pre-create if hide_window is on.
                // We don't have an hwnd yet to gate by isOwnedHwnd, so we
                // apply this to every CreateWindowEx from the target
                // process — that excludes MessageBox / system error popups
                // (they go through a kernel-internal path that doesn't
                // hit this export), which is exactly the dividing line
                // isOwnedHwnd was approximating.
                if (g_hide_window) {
                    const style = args[3].toUInt32();
                    if (style & WS_VISIBLE) {
                        const newStyle = (style & ~WS_VISIBLE) >>> 0;
                        args[3] = ptr(newStyle);
                        this.stripped_visible = true;
                    }
                }
            },
            onLeave: function (retval) {
                markOwnedHwnd(retval, name);
                if (retval && !retval.isNull()) {
                    send({kind:  'hwnd_owned_detail',
                          hwnd:  hwndKey(retval),
                          api:   name,
                          cls:   this.cls || '',
                          title: this.wn  || ''});
                    if (this.stripped_visible) {
                        send({kind: 'hide_force', api: name + '/WS_VISIBLE',
                              hwnd: hwndKey(retval), original: WS_VISIBLE});
                    }
                }
            },
        });
        count++;
    });
    log('CreateWindowEx tracking installed (' + count + '/2)');
}

function installWindowHideHooks() {
    const u32 = Process.findModuleByName('user32.dll');
    if (!u32) { err('installWindowHideHooks', 'user32.dll not loaded'); return; }

    const showWindow = u32.findExportByName('ShowWindow');
    if (showWindow) {
        Interceptor.attach(showWindow, {
            onEnter: function (args) {
                if (!g_hide_window) return;
                const hwnd = args[0];
                const cmd  = args[1].toInt32();
                if (cmd === SW_HIDE) return;
                if (!isOwnedHwnd(hwnd)) return;
                args[1] = ptr(SW_HIDE);
                send({kind: 'hide_force', api: 'ShowWindow',
                      hwnd: hwndKey(hwnd), original: cmd});
            },
        });
    } else { err('installWindowHideHooks', 'ShowWindow not found'); }

    const showWindowAsync = u32.findExportByName('ShowWindowAsync');
    if (showWindowAsync) {
        Interceptor.attach(showWindowAsync, {
            onEnter: function (args) {
                if (!g_hide_window) return;
                const hwnd = args[0];
                const cmd  = args[1].toInt32();
                if (cmd === SW_HIDE) return;
                if (!isOwnedHwnd(hwnd)) return;
                args[1] = ptr(SW_HIDE);
                send({kind: 'hide_force', api: 'ShowWindowAsync',
                      hwnd: hwndKey(hwnd), original: cmd});
            },
        });
    }

    const setWindowPos = u32.findExportByName('SetWindowPos');
    if (setWindowPos) {
        Interceptor.attach(setWindowPos, {
            onEnter: function (args) {
                if (!g_hide_window) return;
                const hwnd  = args[0];
                if (!isOwnedHwnd(hwnd)) return;
                const flags = args[6].toInt32();
                if ((flags & SWP_SHOWWINDOW) === 0) return;
                const newFlags = (flags & ~SWP_SHOWWINDOW) | SWP_HIDEWINDOW;
                args[6] = ptr(newFlags);
                send({kind: 'hide_force', api: 'SetWindowPos',
                      hwnd: hwndKey(hwnd), original: flags});
            },
        });
    } else { err('installWindowHideHooks', 'SetWindowPos not found'); }

    ['SetWindowLongA', 'SetWindowLongW'].forEach(function (name) {
        const fn = u32.findExportByName(name);
        if (!fn) return;
        Interceptor.attach(fn, {
            onEnter: function (args) {
                if (!g_hide_window) return;
                const hwnd   = args[0];
                const nIndex = args[1].toInt32();
                if (nIndex !== GWL_STYLE) return;
                if (!isOwnedHwnd(hwnd)) return;
                const newLong = args[2].toInt32();
                if ((newLong & WS_VISIBLE) === 0) return;
                args[2] = ptr((newLong & ~WS_VISIBLE) >>> 0);
                send({kind: 'hide_force', api: name,
                      hwnd: hwndKey(hwnd), original: newLong});
            },
        });
    });

    log('hide_window hooks installed: ShowWindow + ShowWindowAsync + ' +
        'SetWindowPos + SetWindowLong{A,W}');
}

// ─── MessageBox → stderr redirect (always on) ─────────────────────────
// Same workflow rationale as the C-side dev_hooks in src/dev_hooks.c:
// any modal popup from the vendor exe (engine error path, missing
// DirectX, missing CD, save-game corruption) will stop the process
// dead in agent-mode runs where nobody's watching the screen.

let g_msgbox_a_cb = null;
let g_msgbox_w_cb = null;

function installMessageBoxRedirectHook() {
    const u32 = Process.findModuleByName('user32.dll');
    if (!u32) { err('installMessageBoxRedirect', 'user32.dll not loaded'); return; }

    const mbA = u32.findExportByName('MessageBoxA');
    if (mbA) {
        g_msgbox_a_cb = new NativeCallback(function (hwnd, text, caption, type) {
            let captionStr = '(empty)';
            let bodyStr    = '(empty)';
            try { if (!caption.isNull()) captionStr = caption.readAnsiString(); } catch (e) {}
            try { if (!text   .isNull()) bodyStr    = text   .readAnsiString(); } catch (e) {}
            send({kind: 'messagebox_intercepted',
                  api: 'MessageBoxA',
                  hwnd: hwnd.toString(),
                  caption: captionStr,
                  body: bodyStr,
                  type: type >>> 0});
            return 1;  // IDOK
        }, 'int', ['pointer', 'pointer', 'pointer', 'uint32'], 'stdcall');
        Interceptor.replace(mbA, g_msgbox_a_cb);
        log('msgbox_redirect: MessageBoxA → IDOK @ ' + mbA);
    } else {
        err('installMessageBoxRedirect', 'user32!MessageBoxA not exported');
    }

    const mbW = u32.findExportByName('MessageBoxW');
    if (mbW) {
        g_msgbox_w_cb = new NativeCallback(function (hwnd, text, caption, type) {
            let captionStr = '(empty)';
            let bodyStr    = '(empty)';
            try { if (!caption.isNull()) captionStr = caption.readUtf16String(); } catch (e) {}
            try { if (!text   .isNull()) bodyStr    = text   .readUtf16String(); } catch (e) {}
            send({kind: 'messagebox_intercepted',
                  api: 'MessageBoxW',
                  hwnd: hwnd.toString(),
                  caption: captionStr,
                  body: bodyStr,
                  type: type >>> 0});
            return 1;  // IDOK
        }, 'int', ['pointer', 'pointer', 'pointer', 'uint32'], 'stdcall');
        Interceptor.replace(mbW, g_msgbox_w_cb);
        log('msgbox_redirect: MessageBoxW → IDOK @ ' + mbW);
    } else {
        err('installMessageBoxRedirect', 'user32!MessageBoxW not exported');
    }
}

// ─── force windowed — three layers ─────────────────────────────────────
// Lords II is a 1996 DDraw-1.x game.  It uses the legacy
// `IDirectDraw::SetCooperativeLevel` API to take exclusive fullscreen
// (`DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN`) before calling `SetDisplayMode`
// to switch the desktop to 640×480 (or 800×600 patched) at 256-color.
//
// On a modern Windows 10/11 host that's a heavyweight mode-switch that
// minimises other windows and disrupts the entire desktop.  For the
// dev loop, we want windowed mode with no resolution change.
//
// Strategy (three layers, install all of them; any one is sufficient
// but defense in depth):
//
//   L1 (GDI):    stub user32!ChangeDisplaySettings*A/W
//   L2 (DDraw):  wrap ddraw!DirectDrawCreate{,Ex} so we can patch the
//                returned IDirectDraw / IDirectDraw7 vtable's
//                SetCooperativeLevel slot to strip the EXCLUSIVE +
//                FULLSCREEN bits.
//   L3 (extra):  hook ddraw!SetDisplayMode (only if the engine somehow
//                calls it directly — defensive; Lords II goes through
//                the COM interface, not a flat export).
//
// DDraw cooperative-level flags (ddraw.h):
//   DDSCL_FULLSCREEN  = 0x0001
//   DDSCL_EXCLUSIVE   = 0x0010
//   DDSCL_NORMAL      = 0x0008
// We want NORMAL (windowed), so OR in 0x08 and strip 0x01 | 0x10.
const DDSCL_FULLSCREEN = 0x0001;
const DDSCL_EXCLUSIVE  = 0x0010;
const DDSCL_NORMAL     = 0x0008;

let g_ddraw1_set_cooplevel_orig = null;
let g_ddraw7_set_cooplevel_orig = null;
let g_dsound_set_cooplevel_orig = null;

function installChangeDisplaySettingsStub() {
    const u32 = Process.findModuleByName('user32.dll');
    if (!u32) { err('installForceWindowed[L1]', 'user32.dll not loaded'); return; }
    const DISP_OK = 0;  // DISP_CHANGE_SUCCESSFUL
    let stubbed = 0;
    ['ChangeDisplaySettingsA', 'ChangeDisplaySettingsW'].forEach(function (name) {
        const exp = u32.findExportByName(name);
        if (!exp) return;
        Interceptor.replace(exp, new NativeCallback(function (devmode, flags) {
            send({kind: 'display_settings_blocked', api: name, flags: flags >>> 0});
            return DISP_OK;
        }, 'int32', ['pointer', 'uint32'], 'stdcall'));
        stubbed++;
    });
    ['ChangeDisplaySettingsExA', 'ChangeDisplaySettingsExW'].forEach(function (name) {
        const exp = u32.findExportByName(name);
        if (!exp) return;
        Interceptor.replace(exp, new NativeCallback(function (devname, devmode, hwnd, flags, param) {
            send({kind: 'display_settings_blocked', api: name, flags: flags >>> 0});
            return DISP_OK;
        }, 'int32', ['pointer', 'pointer', 'pointer', 'uint32', 'pointer'], 'stdcall'));
        stubbed++;
    });
    log('force_windowed[L1]: ' + stubbed + '/4 ChangeDisplaySettings* stubbed');
}

// Wrap IDirectDraw{,7}::SetCooperativeLevel — slot 0x14 (20) in both the
// IDirectDraw and IDirectDraw7 vtables (it's at the same vtable offset
// in every version of the interface; see ddraw.h IUnknown(3) + the seven
// pre-SetCooperativeLevel methods).
//
// Slot offsets (DDraw interface, identical layout across DDraw1..7):
//   0   QueryInterface
//   1   AddRef
//   2   Release
//   3   Compact
//   4   CreateClipper
//   5   CreatePalette
//   6   CreateSurface
//   7   DuplicateSurface
//   8   EnumDisplayModes
//   9   EnumSurfaces
//   10  FlipToGDISurface
//   11  GetCaps
//   12  GetDisplayMode
//   13  GetFourCCCodes
//   14  GetGDISurface
//   15  GetMonitorFrequency
//   16  GetScannerLine
//   17  GetVerticalBlankStatus
//   18  Initialize
//   19  RestoreDisplayMode
//   20  SetCooperativeLevel              ← here, x86 byte offset 0x50
//   21  SetDisplayMode                    ← x86 byte offset 0x54
//
// Be conservative: only patch each unique vtable pointer once.

const g_patched_ddraw_vtables = {};

function patchDDrawVtable(ifacePtr, ifaceName) {
    try {
        const vtable = ifacePtr.readPointer();
        const vtKey  = vtable.toString();
        if (vtKey in g_patched_ddraw_vtables) return;
        g_patched_ddraw_vtables[vtKey] = true;

        // SetCooperativeLevel @ slot 20 ⇒ byte offset 20 * sizeof(void*) = 0x50
        const setCoopLevel = vtable.add(20 * Process.pointerSize).readPointer();
        Interceptor.attach(setCoopLevel, {
            onEnter: function (args) {
                const hwnd     = args[1];
                const original = args[2].toUInt32();
                // Strip EXCLUSIVE + FULLSCREEN; force NORMAL.
                const forced =
                    (original & ~(DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN)) | DDSCL_NORMAL;
                args[2] = ptr(forced);
                send({kind: 'ddraw_cooplevel_rewrite',
                      iface: ifaceName,
                      hwnd:  hwnd.toString(),
                      original: original,
                      forced:   forced});
            },
        });

        // SetDisplayMode @ slot 21 ⇒ byte offset 0x54 — make it a no-op too.
        // Some games call this even in windowed mode; for our case we never
        // want a resolution change.
        const setDispMode = vtable.add(21 * Process.pointerSize).readPointer();
        Interceptor.replace(setDispMode, new NativeCallback(function (self, w, h, bpp) {
            send({kind: 'log',
                  msg: 'force_windowed: ' + ifaceName +
                       '::SetDisplayMode(' + w + 'x' + h + 'x' + bpp + ') blocked'});
            return 0;  // DD_OK
        }, 'int32', ['pointer', 'uint32', 'uint32', 'uint32'], 'stdcall'));

        log('force_windowed[L2]: ' + ifaceName +
            ' vtable patched (SetCooperativeLevel @ ' + setCoopLevel + ', SetDisplayMode stubbed)');
    } catch (e) {
        err('patchDDrawVtable', e.message);
    }
}

function installForceWindowedDDraw() {
    function armDDrawCreate() {
        const ddraw = Process.findModuleByName('ddraw.dll');
        if (!ddraw) return false;

        const ddc = ddraw.findExportByName('DirectDrawCreate');
        if (ddc) {
            Interceptor.attach(ddc, {
                onEnter: function (args) {
                    // args[1] = *LPDIRECTDRAW (out param)
                    this.outPP = args[1];
                },
                onLeave: function (retval) {
                    if (retval.toInt32() !== 0) return;
                    if (!this.outPP || this.outPP.isNull()) return;
                    const iface = this.outPP.readPointer();
                    if (iface.isNull()) return;
                    patchDDrawVtable(iface, 'IDirectDraw');
                },
            });
            log('force_windowed[L2]: ddraw!DirectDrawCreate armed');
        }

        const ddcEx = ddraw.findExportByName('DirectDrawCreateEx');
        if (ddcEx) {
            Interceptor.attach(ddcEx, {
                onEnter: function (args) {
                    this.outPP = args[1];
                },
                onLeave: function (retval) {
                    if (retval.toInt32() !== 0) return;
                    if (!this.outPP || this.outPP.isNull()) return;
                    const iface = this.outPP.readPointer();
                    if (iface.isNull()) return;
                    patchDDrawVtable(iface, 'IDirectDraw7');
                },
            });
            log('force_windowed[L2]: ddraw!DirectDrawCreateEx armed');
        }
        return true;
    }

    if (!armDDrawCreate()) {
        // ddraw.dll not loaded yet — watch LoadLibrary and arm later.
        const k32 = Process.findModuleByName('kernel32.dll');
        if (!k32) {
            err('installForceWindowed[L2]', 'kernel32.dll not loaded');
            return;
        }
        const arm = function (name, isWide) {
            const exp = k32.findExportByName(name);
            if (!exp) return;
            Interceptor.attach(exp, {
                onEnter: function (args) {
                    this.lib = isWide ? args[0].readUtf16String() : args[0].readCString();
                },
                onLeave: function () {
                    if (!this.lib) return;
                    if (!/ddraw\.dll/i.test(this.lib)) return;
                    armDDrawCreate();
                },
            });
        };
        arm('LoadLibraryA', false);
        arm('LoadLibraryW', true);
        log('force_windowed[L2]: ddraw.dll not loaded; armed LoadLibrary* watch');
    }
}

// ─── silent audio ──────────────────────────────────────────────────────
// Two paths:
//   1. winmm!waveOutSetVolume — direct PCM volume (the Sierra startup
//      chime, and any WAV played via PlaySound-style APIs).  Clamp to 0.
//   2. dsound!DirectSoundCreate → wrap IDirectSound::SetCooperativeLevel
//      AND patch IDirectSoundBuffer::SetVolume on every secondary buffer.
//      SetVolume takes a LONG in 1/100th dB, range -10000 (silent) to 0.
//
// We always also mute the master via waveOutSetVolume which dominates on
// 1996-era engines anyway.

function installWaveOutMute() {
    const winmm = Process.findModuleByName('winmm.dll');
    if (!winmm) { err('silent_audio', 'winmm.dll not loaded'); return; }
    const wosv = winmm.findExportByName('waveOutSetVolume');
    if (!wosv) { err('silent_audio', 'winmm!waveOutSetVolume not found'); return; }
    Interceptor.attach(wosv, {
        onEnter: function (args) {
            // waveOutSetVolume(HWAVEOUT, DWORD dwVolume).  Low/high WORDs
            // are left/right channels, 0xFFFF = max.  Force to 0.
            const original = args[1].toUInt32();
            args[1] = ptr(0);
            send({kind: 'audio_mute', api: 'waveOutSetVolume', original: original});
        },
    });
    log('silent_audio: winmm!waveOutSetVolume → 0');
}

// IDirectSoundBuffer vtable layout (dsound.h, x86) — SetVolume at slot 15:
//   0..2  IUnknown
//   3     GetCaps
//   4     GetCurrentPosition
//   5     GetFormat
//   6     GetVolume
//   7     GetPan
//   8     GetFrequency
//   9     GetStatus
//   10    Initialize
//   11    Lock
//   12    Play
//   13    SetCurrentPosition
//   14    SetFormat
//   15    SetVolume             ← here, byte offset 0x3C
//   16    SetPan
//   17    SetFrequency
//   18    Stop
//   19    Unlock
//   20    Restore
//
// IDirectSound vtable layout (dsound.h, x86) — SetCooperativeLevel at slot 6:
//   0..2  IUnknown
//   3     CreateSoundBuffer
//   4     GetCaps
//   5     DuplicateSoundBuffer
//   6     SetCooperativeLevel    ← here, byte offset 0x18
//   7     Compact
//   8     GetSpeakerConfig
//   9     SetSpeakerConfig
//   10    Initialize

const g_patched_dsound_vtables = {};
const g_patched_dsbuf_vtables  = {};

function patchDSoundVtable(ifacePtr) {
    try {
        const vtable = ifacePtr.readPointer();
        const vtKey  = vtable.toString();
        if (vtKey in g_patched_dsound_vtables) return;
        g_patched_dsound_vtables[vtKey] = true;

        // SetCooperativeLevel @ slot 6.  Strip exclusive bits — DSound
        // exclusive mode steals audio focus from other processes.
        // DSSCL_NORMAL      = 1
        // DSSCL_PRIORITY    = 2
        // DSSCL_EXCLUSIVE   = 3
        // DSSCL_WRITEPRIMARY= 4
        const setCoopLevel = vtable.add(6 * Process.pointerSize).readPointer();
        Interceptor.attach(setCoopLevel, {
            onEnter: function (args) {
                const hwnd     = args[1];
                const original = args[2].toUInt32();
                // Force NORMAL (1) regardless of what the engine asked for.
                args[2] = ptr(1);
                send({kind: 'dsound_cooplevel_rewrite',
                      iface:    'IDirectSound',
                      hwnd:     hwnd.toString(),
                      original: original,
                      forced:   1});
            },
        });

        // CreateSoundBuffer @ slot 3 — patch every returned buffer's
        // SetVolume so engine volume changes can't unmute us.
        const createSB = vtable.add(3 * Process.pointerSize).readPointer();
        Interceptor.attach(createSB, {
            onLeave: function (retval) {
                if (retval.toInt32() !== 0) return;
                // args[2] = *LPDIRECTSOUNDBUFFER out param.  We need it
                // from the args saved in onEnter via this.context isn't
                // available here — re-derive via this.ctx (set in onEnter).
                // Wait — we didn't set onEnter. Need both.
            },
        });
        // Re-do CreateSoundBuffer with both onEnter + onLeave.
        // (Frida doesn't allow attaching twice to the same address from
        // the same invocation; instead we do it below in one block.)
        log('silent_audio: IDirectSound vtable patched (SetCooperativeLevel)');
    } catch (e) {
        err('patchDSoundVtable', e.message);
    }
}

// Helper: patch a freshly-returned IDirectSoundBuffer's SetVolume so the
// engine's volume calls clamp to -10000 (silent).
function patchDSoundBufferVtable(bufPtr) {
    try {
        const vtable = bufPtr.readPointer();
        const vtKey  = vtable.toString();
        if (vtKey in g_patched_dsbuf_vtables) return;
        g_patched_dsbuf_vtables[vtKey] = true;
        const setVolume = vtable.add(15 * Process.pointerSize).readPointer();
        Interceptor.attach(setVolume, {
            onEnter: function (args) {
                if (!g_silent_audio_enabled) return;
                const original = args[1].toInt32();
                if (original === -10000) return;  // already silent
                args[1] = ptr((-10000) >>> 0);  // sign-extended as DWORD
                send({kind: 'audio_mute',
                      api: 'IDirectSoundBuffer::SetVolume',
                      original: original});
            },
        });
        log('silent_audio: IDirectSoundBuffer::SetVolume clamp armed @ ' + setVolume);
    } catch (e) {
        err('patchDSoundBufferVtable', e.message);
    }
}

function installSilentAudioDSound() {
    function armDSoundCreate() {
        const dsound = Process.findModuleByName('dsound.dll');
        if (!dsound) return false;
        const dsc = dsound.findExportByName('DirectSoundCreate');
        if (dsc) {
            Interceptor.attach(dsc, {
                onEnter: function (args) { this.outPP = args[1]; },
                onLeave: function (retval) {
                    if (retval.toInt32() !== 0) return;
                    if (!this.outPP || this.outPP.isNull()) return;
                    const iface = this.outPP.readPointer();
                    if (iface.isNull()) return;
                    patchDSoundVtable(iface);

                    // Wrap CreateSoundBuffer (slot 3) to catch every buffer
                    // the engine subsequently creates.  This pass attaches
                    // properly with onEnter+onLeave.
                    try {
                        const vtable = iface.readPointer();
                        const createSB = vtable.add(3 * Process.pointerSize).readPointer();
                        Interceptor.attach(createSB, {
                            onEnter: function (args) { this.bufPP = args[3]; },
                            onLeave: function (rc) {
                                if (rc.toInt32() !== 0) return;
                                if (!this.bufPP || this.bufPP.isNull()) return;
                                const buf = this.bufPP.readPointer();
                                if (buf.isNull()) return;
                                patchDSoundBufferVtable(buf);
                            },
                        });
                        log('silent_audio: IDirectSound::CreateSoundBuffer hooked');
                    } catch (e) {
                        err('armDSoundCreate.CreateSoundBuffer', e.message);
                    }
                },
            });
            log('silent_audio: dsound!DirectSoundCreate armed');
        }
        return true;
    }

    if (!armDSoundCreate()) {
        const k32 = Process.findModuleByName('kernel32.dll');
        if (!k32) { err('silent_audio', 'kernel32.dll not loaded'); return; }
        const arm = function (name, isWide) {
            const exp = k32.findExportByName(name);
            if (!exp) return;
            Interceptor.attach(exp, {
                onEnter: function (args) {
                    this.lib = isWide ? args[0].readUtf16String() : args[0].readCString();
                },
                onLeave: function () {
                    if (!this.lib) return;
                    if (!/dsound\.dll/i.test(this.lib)) return;
                    armDSoundCreate();
                },
            });
        };
        arm('LoadLibraryA', false);
        arm('LoadLibraryW', true);
        log('silent_audio: dsound.dll not loaded; armed LoadLibrary* watch');
    }
}

// ─── turbo ──────────────────────────────────────────────────────────────
// Two cooperating pieces (mirrors OpenMare's pattern):
//
//   v1) kernel32!Sleep → no-op. Lords2 sleeps in its render-rate limiter
//       (see docs/findings/render-rate-limiter.md); zeroing Sleep lets the
//       main loop spin as fast as the host CPU allows.
//
//   v2) winmm!timeGetTime → virtual clock, gated to the engine main thread.
//       Seeded from a one-shot real timeGetTime read at install time; bumped
//       by g_turbo_step_ms on every entry to the per-tick driver
//       FUN_004b99c0 (the natural per-frame anchor). All retail timeGetTime
//       call sites that we care about — the FPS counter (FUN_004b27bf), the
//       frame-delta generator (FUN_004b20ed), the sim rate-gate
//       (FUN_004bb978) — read this virtualised clock; DPlay's GetTickCount
//       reads stay on real wall-clock (the DPlay pump uses GetTickCount, not
//       timeGetTime — verified via decompile grep, see main-loop-tick.md).
//
// Without v2, v1 alone makes the loop spin pointlessly: timeGetTime still
// returns wall-clock, so the rate-gate falls back to "no progress yet" and
// the FPS counter just counts more iterations per real second.  With v2 on,
// the engine's *perceived* time moves at (1000 / step_ms) Hz — at the
// default 17 ms/iter, ~58 ticks/s — which is what the FPS counter samples
// to compute current_fps.
//
// Side-thread safety: only main-thread callers get the virtual return;
// DSound/Smacker/audio threads see the real timeGetTime untouched so their
// internal scheduling isn't disrupted.

function installTurboSleepHook() {
    const k32 = Process.findModuleByName('kernel32.dll');
    if (!k32) { err('installTurbo', 'kernel32.dll not loaded'); return; }
    const sleepAddr = k32.findExportByName('Sleep');
    if (!sleepAddr) { err('installTurbo', 'kernel32!Sleep not found'); return; }
    Interceptor.replace(sleepAddr, new NativeCallback(
        function (ms) { /* swallow */ },
        'void',
        ['uint32'],
        'stdcall'));
    log('turbo v1: kernel32!Sleep no-op installed');
}

function installVirtualClockHook() {
    const winmm = Process.findModuleByName('winmm.dll');
    if (!winmm) { err('installVirtualClock', 'winmm.dll not loaded'); return; }
    const tgt = winmm.findExportByName('timeGetTime');
    if (!tgt) { err('installVirtualClock', 'winmm!timeGetTime not found'); return; }

    // Seed the virtual clock with the current real timeGetTime so the
    // engine's first read sees a sane baseline — the FPS counter +
    // frame-delta both subtract this from later samples, so a zero seed
    // would make the first window look like the engine has been running
    // for 49.7 days.  Use NativeFunction (not Interceptor.replace) so the
    // seed read doesn't re-enter our own onLeave handler.
    const realTGT = new NativeFunction(tgt, 'uint32', [], 'stdcall');
    g_virtual_now_ms = realTGT() >>> 0;

    Interceptor.attach(tgt, {
        onLeave: function (retval) {
            if (!g_clock_virtualised) return;
            if (g_main_thread_id === -1) return;
            if (Process.getCurrentThreadId() !== g_main_thread_id) return;
            retval.replace(ptr(g_virtual_now_ms >>> 0));
        },
    });

    g_clock_virtualised = true;
    log('turbo v2: winmm!timeGetTime virtualised ' +
        '(step=' + g_turbo_step_ms + ' ms/tick, main-thread-gated, ' +
        'seed=' + g_virtual_now_ms + ')');
}

// Per-tick anchor — `FUN_004b99c0` is called once per main-loop iteration.
// We use its onEnter as:
//   1. main-thread-id capture (first entry only) for the virtual-clock gate.
//   2. virtual-clock advance by g_turbo_step_ms (when turbo v2 is active).
// We use its onLeave to publish an `fps_sample` event whenever the engine's
// FPS-counter cumulative_seconds counter ticks — this is the verification
// knob for turbo: without turbo, fps_sample.current_fps lands wherever
// retail's render path actually clocks (~30 in Lords2's default cap);
// with turbo at 17 ms/tick we expect ~58.
//
// Hook installs unconditionally — the bump is gated on g_clock_virtualised
// so a non-turbo run still benefits from the FPS-sample emission without
// disturbing wall-clock time.

function installEngineTickHook() {
    Interceptor.attach(rva(ADDR.fn_engine_tick), {
        onEnter: function () {
            if (g_main_thread_id === -1) {
                g_main_thread_id = Process.getCurrentThreadId();
                log('engine_tick: main thread id captured = ' + g_main_thread_id);
            }
            if (g_clock_virtualised) {
                g_virtual_now_ms = (g_virtual_now_ms + g_turbo_step_ms) >>> 0;
            }
        },
        onLeave: function () {
            try {
                const cum_secs = rva(ADDR.var_fps_cum_secs).readU32();
                if (cum_secs === g_last_fps_cum_secs) return;
                g_last_fps_cum_secs = cum_secs;
                const current_fps = rva(ADDR.var_fps_current).readU32();
                const cum_sum     = rva(ADDR.var_fps_cum_sum).readU32();
                const avg_fps     = cum_secs > 0 ? (cum_sum / cum_secs) | 0 : 0;
                g_fps_sample_count++;
                send({kind: 'fps_sample',
                      current_fps: current_fps,
                      cumulative_fps: cum_sum,
                      cumulative_seconds: cum_secs,
                      avg_fps: avg_fps,
                      virtual_now_ms: g_virtual_now_ms,
                      t_ms: nowMs()});
            } catch (e) {
                err('engine_tick.onLeave', e.message);
            }
        },
    });
    log('engine_tick anchor installed @ ' + rva(ADDR.fn_engine_tick) +
        ' (FUN_004b99c0)');
}

// ─── init RPC ──────────────────────────────────────────────────────────

rpc.exports = {
    init: function (opts) {
        opts = opts || {};
        g_target_module_name = String(opts.module || MODULE_NAME);
        g_hide_window        = !!opts.hide_window;
        g_silent_audio_enabled = !!opts.silent_audio;
        g_force_windowed     = !!opts.force_windowed;
        g_turbo_enabled      = !!opts.turbo;
        // Turbo step in virtual-ms per per-tick-driver iteration. The
        // default (17 ms) yields ~58 ticks/s perceived time, close to
        // 60 Hz; raise it to fast-forward.  0 disables turbo v2 even if
        // opts.turbo is set, falling back to turbo v1 (Sleep no-op only).
        const stepOpt = (opts.turbo_step_ms | 0);
        g_turbo_step_ms = stepOpt > 0 ? stepOpt : 17;

        const mod = Process.findModuleByName(g_target_module_name);
        if (!mod) {
            err('init', 'target module not loaded: ' + g_target_module_name);
            return;
        }
        g_base = mod.base;

        // Always-on hooks: HWND tracking, MessageBox capture, engine-tick
        // anchor (publishes fps_sample even without turbo so the harness
        // can plot a baseline curve).
        installCreateWindowExHooks();
        installMessageBoxRedirectHook();
        installEngineTickHook();

        // Conditionally-on hooks based on init flags.
        if (g_hide_window)     installWindowHideHooks();
        if (g_force_windowed)  { installChangeDisplaySettingsStub();
                                 installForceWindowedDDraw(); }
        if (g_silent_audio_enabled) { installWaveOutMute();
                                      installSilentAudioDSound(); }
        if (g_turbo_enabled) {
            // Order matters: virtualise timeGetTime BEFORE the Sleep
            // no-op so the engine never observes a real-clock baseline
            // once turbo is armed.  The clock is gated on the main
            // thread, which won't be known until the first engine_tick
            // entry — so g_clock_virtualised goes true at install but
            // the gate inside the onLeave hook fails silently until
            // engine_tick captures the thread id.
            try { installVirtualClockHook(); }
            catch (e) { err('installVirtualClockHook', e.message); }
            try { installTurboSleepHook(); }
            catch (e) { err('installTurboSleepHook', e.message); }
        }

        send({kind: 'ready',
              module:        g_target_module_name,
              base:          g_base.toString(),
              imageBase:     IMAGE_BASE.toString(),
              fridaVersion:  Frida.version,
              hide_window:   g_hide_window,
              force_windowed: g_force_windowed,
              silent_audio:  g_silent_audio_enabled,
              turbo:         g_turbo_enabled,
              turbo_step_ms: g_turbo_step_ms});
    },
};
