#!/usr/bin/env bash
# rd — drive retail Lords II live (tools/drive/drive.py; card: tools/drive/DRIVER.md)
exec python3 "$(dirname "$(readlink -f "$0")")/tools/drive/drive.py" "$@"
