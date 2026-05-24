/*
 * dev_hooks — developer-time process instrumentation.
 *
 * These hooks are intentionally NOT part of the drop-in replication —
 * they make OpenLords2 friendlier to run under the agent / CI / WSL
 * harness, where a modal dialog stops the world without surfacing
 * anything to stderr.  In a real drop-in shipping scenario you'd
 * probably want to leave them off (pass --show-msgbox).
 *
 * Hooks currently installed:
 *   - user32!MessageBoxA  → log to stderr + return IDOK
 *   - user32!MessageBoxW  → log to stderr (UTF-16 → UTF-8) + return IDOK
 *
 * The hook returns IDOK unconditionally so the caller's "did the user
 * click Yes?" gate always takes the positive branch.  If a popup needs
 * a different default we'll add a per-call table later.
 *
 * Why: 1996-era engines like Lords II will throw MessageBox modals for
 * missing DirectX, missing CD, save-file corruption, etc.  Without this
 * hook, an agent-mode run will hang on the first such modal with no
 * indication in stderr.  Borrowed wholesale from OpenMare's dev_hooks.
 */
#ifndef OPENLORDS2_DEV_HOOKS_H
#define OPENLORDS2_DEV_HOOKS_H

/* Patch user32!MessageBoxA / MessageBoxW so calls land in stderr and
 * return IDOK.  Returns 1 on full success, 0 on any patch failure (with
 * details to stderr).  Idempotent. */
int dev_hooks_install_messagebox(void);

/* Restore the original prologues.  Safe to call even if install failed
 * or wasn't called. */
void dev_hooks_uninstall_messagebox(void);

#endif /* OPENLORDS2_DEV_HOOKS_H */
