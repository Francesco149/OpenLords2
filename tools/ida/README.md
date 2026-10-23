# tools/ida — headless IDA export of Lords2.exe

Static IDA Pro 9.4 (Hex-Rays) export of the retail binary for the porting loop
(the openrecet methodology: agents read IDA pseudocode from a static export).
Retail: `Lords2.exe` GOG 2.0.0.3, sha256 `6fec043e…`, MSVC 4.2, ImageBase 0x400000.

## Re-run

```
python3 tools/ida/ida.py export            # regen names.tsv, stage, export (cached .i64), fetch  (~70 s)
python3 tools/ida/ida.py export --fresh    # also re-analyse from the exe                         (~80 s)
python3 tools/ida/ida.py names             # only regenerate tools/ida/names.tsv from src/
python3 tools/ida/ida.py fetch | status
```
Options: `--noasm` (skip all_ida.asm), `--no-names` (IDA's own names only).
Needs `ssh win` (BatchMode). No devshell needed (plain python3).

Outputs land in `/mnt/ephemeral/openlords2/decompiled-ida/` (override with
`OL2_IDA_LOCAL`). They are derived from the retail binary: **never commit them**.

| file | content |
|---|---|
| `all_ida.c` | Hex-Rays pseudocode of every function; header `/* ===== name @ 0xVA (N bytes) FUN_VA [tags] ===== */` |
| `all_ida.asm` | disassembly per function (same header, `; ` prefixed; VA-prefixed lines, all chunks) |
| `functions_ida.tsv` | va size name flirt_lib cc nargs callers callees proto name_src chunks addr_taken |
| `data_xrefs.tsv` | target_va target_name func_va insn_va kind (r/w/o) — global → accessing functions |
| `strings_ida.tsv` | va len text referencing-functions |
| `export_summary.txt` | counts (functions / FLIRT lib / port-named / decompile failures), timings, fixups |

`size` counts all chunks. `name_src` is `port` (names.tsv), `flirt`, `ida`
(imports, `start`, …) or empty (`sub_…`). Grep a ledger VA as `FUN_00404a46`
or `0x00404a46`.

## What the export script does (`ol2_ida_export.py`)

1. **GOG patch detour.** GOG appended code to `.text`'s raw data past its
   VirtualSize (vsize 0xceb06, raw 0xcec00): `0x4050f0 jmp 0x4cfb08` → a 16 ms
   `GetTickCount` gate on the map-animation counters → `jmp 0x4050f6`. Its
   globals (0x5ceff8/0x5ceffc) sit in `.data`'s page slack past vsize.
   IDA 9.4 already maps both segments to their page-aligned ends with the raw
   bytes. The script still checks every section against the PE headers and
   extends/copies bytes if a loader ever truncates. It then finds stubs (rel32
   jmp/call from analysed code into raw-past-vsize bytes). A jmp-detour is
   attached as **tail chunks of the detouring function**: `0x4050c0` owns
   `0x4cfb08..0x4cfb48` and `0x4050f6..0x40526e`. That way Hex-Rays decompiles one
   body with its frame (0x4050f6 has no prologue of its own). Ghidra-style
   separate functions decompile to `JUMPOUT(0x4050F6)` and lose the render
   body. `detour=split` is not offered for that reason.
2. **Missed functions.** Every game function opens with `push ebp; mov ebp,esp`.
   IDA misses those reached only via data (handler tables, dead code), so the
   script creates a function at each `55 8B EC` in undefined .text bytes. This
   recovers 185, matching Ghidra's set.
3. **FLIRT false positives.** The VC runtime signatures tag about 140 small game
   functions below the CRT (empty `push ebp…leave; ret` bodies, global-zeroing
   inits named `__cfltcvt_init`, …). Everything below the first import thunk
   (0x4bf3aa; game objs link before the libs) has its lib flag, FLIRT
   name/type and implied noreturn removed. The real (debug) CRT keeps its
   names: 252 functions.
4. The analysed DB is saved as the cache `C:\ol2-ida\Lords2.exe.i64`. Port
   names are then applied **in memory only** (the session exits with
   `DBFL_KILL`), so the cache never holds stale names.

## Port names (`names.tsv`, `gen_names.py`)

`names.tsv` (va → name) is derived from the `FUN_<va>` citations in `src/`:
"Port of FUN_x" doc comments, `CALL_TRACE_ENTER(0xVA)`, file headers, header
prototypes with `/* FUN_x */`, "FUN_x → name" prose, and call-site comments.
Evidence is scored and each VA gets its best name; see the docstring for the
weights. It is regenerated on every `export`. Commit it with the tooling: the
names are our work. The export applies names only to non-library function
starts. A name that lands on a tail-chunk start becomes a label, and shows in
the header's `[chunks …:name]`.

## Windows side

`C:\ol2-ida\` holds `Lords2.exe` (sha256-checked copy of the pristine
`C:\ol2-rhost\game\Lords2.exe`), the `.i64` cache, the staged scripts,
`names.tsv` and `out\`. `run_ida.ps1` runs `idat -A -S…` and kills only the
PID it started (on timeout). Never touch `C:\orc-ida`, `C:\oss-ida` or
`C:\de-ida`, and never kill idat by name: other projects run IDA jobs in
parallel.
