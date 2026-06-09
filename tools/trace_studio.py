#!/usr/bin/env python3
"""trace_studio — thin launcher for the tools/trace_studio/ package.

(A package dir shadows a same-named .py on the import path, so this file and
the package coexist; the documented command stays
`python3 tools/trace_studio.py …`.)
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from trace_studio.cli import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
