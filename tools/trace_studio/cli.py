"""cli.py — thin argparse → orchestrator."""
from __future__ import annotations

import argparse

from .capture import CaptureConfig, cmd_compile, cmd_lint, run_capture
from .paths import DEFAULT_REMOTE


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="trace_studio",
        description="OpenLords2 port↔retail trace workhorse")
    sub = ap.add_subparsers(dest="cmd", required=True)

    cap = sub.add_parser("capture", help="drive both sides + diff")
    cap.add_argument("--trace", help="studio trace (path or tools/traces "
                                     "name); optional when the session "
                                     "already has one")
    cap.add_argument("--session", help="session name (default: trace stem — "
                                       "re-captures overwrite in place)")
    cap.add_argument("--target", choices=("both", "port", "retail"),
                     default="both")
    cap.add_argument("--only", choices=("port", "retail"), default=None,
                     help="re-run just this side; reuse the cached other "
                          "side (the fast port-fix loop)")
    cap.add_argument("--amp", type=float, default=6.0)
    cap.add_argument("--remote", default=DEFAULT_REMOTE)
    cap.add_argument("--no-rng-seed", action="store_true",
                     help="don't pin retail's LFSRs (deliberate phase study)")
    cap.add_argument("--no-lint", action="store_true")
    cap.add_argument("--no-videos", action="store_true")
    cap.add_argument("--keep-raw", action="store_true",
                     help="keep frames-raw BMPs (debugging)")
    cap.add_argument("--port-max-frames", type=int, default=4000)
    cap.add_argument("--port-timeout-ms", type=int, default=240_000)

    li = sub.add_parser("lint", help="preflight a studio trace")
    li.add_argument("--trace", required=True)
    li.add_argument("--target", choices=("both", "port", "retail"),
                    default="both")

    co = sub.add_parser("compile", help="print the lowered primitive trace")
    co.add_argument("--trace", required=True)

    srv = sub.add_parser("serve", help="serve the scrub/compare UI")
    srv.add_argument("--session", default=None)
    srv.add_argument("--http-port", type=int, default=8780)
    srv.add_argument("--host", default="127.0.0.1")
    srv.add_argument("--remote", default=DEFAULT_REMOTE)

    args = ap.parse_args(argv)
    if args.cmd == "capture":
        return run_capture(CaptureConfig(
            trace=args.trace, session=args.session, target=args.target,
            only=args.only, amp=args.amp, remote=args.remote,
            rng_seed=not args.no_rng_seed, lint=not args.no_lint,
            encode_videos=not args.no_videos, prune_raw=not args.keep_raw,
            port_max_frames=args.port_max_frames,
            port_timeout_ms=args.port_timeout_ms))
    if args.cmd == "lint":
        return cmd_lint(args.trace, args.target)
    if args.cmd == "compile":
        return cmd_compile(args.trace)
    if args.cmd == "serve":
        from .server.app import serve
        from .paths import SESS_ROOT, ROOT
        return serve(SESS_ROOT, ROOT / "tools" / "trace_studio_web",
                     host=args.host, port=args.http_port,
                     default_session=args.session, remote=args.remote)
    return 2
