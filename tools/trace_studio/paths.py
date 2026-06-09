"""paths.py — shared roots + defaults."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent     # repo root
SESS_ROOT = ROOT / "runs" / "trace-studio"
TRACES_DIR = ROOT / "tools" / "traces"

# WSL2's Hyper-V firewall blocks loopback to the Windows host; the dev shell's
# frida-server is reached by hostname (see memory: reference_frida_remote).
DEFAULT_REMOTE = "cutestation.soy:27042"
