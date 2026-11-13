# bunker — rhost headless under wine in ONE bounded pool (temporary NixOS box)

The third rhost host, next to Windows and maximus-wine. Port of openrecet's `tools/bunker` (same design), namespaced
`ol2-*`: openrecet's `orc-bunker-pool` runs on the same box — **never touch `/var/lib/orc-bunker/` or `orc-bunker-*`**.
Host `bunker` (NixOS, i5-10600K 6c/12t, 16 GB incl. ZFS ARC, 16 GB swap, the owner's desktop session).
`RHOST_BUNKER` = ssh target (default `root@10.0.10.58`). **Temporary: access is lost when it reboots** — then every
bunker path fails fast (`up()` is False).

Used by:
- **`rhost.py … --target bunker`**: `run`, `snap-check`, `corpus --check` (`rhost.run_batch` → `bunker.run_batch`).
  The end state is checked against `expected.wine.json` (same wine engine; `expected_diffs` maps bunker → wine).
  Headless only (`--show` refused).
- **live sessions** (`tools/drive/drive.py`): the API below.
- the standalone CLI:

```sh
python3 tools/bunker/bunker.py setup                 # once: work dir, nix GC roots, wine prefix (null display driver)
python3 tools/bunker/bunker.py stage                 # → sha12 + C:\ol2-rhost\bin\<sha12>\ol2-rhost.exe
python3 tools/bunker/bunker.py run SCEN.. [-j N] [--frames N] [--extra " --x"] [--check] [--keep]
python3 tools/bunker/bunker.py status | teardown
```

A batch stages first — rhost build if stale → `root/bin/<sha12>/ol2-rhost.exe` (sha12 = sha256 of `build/rhost/rhost.exe`),
the pristine game dir (`rhost.WINE_GAME`, 521 MB), the scripts, each only when its stamp differs — builds the batch with
`rhost.make_batch` (the identical argv.json as `--target wine`; scenario `saves` copied into the run dir), stages the
batch's own exe content-addressed (so `exe_pin` works), runs it in one container in the pool (`batch.py`, -j at a time), fetches the run dirs to `/mnt/ephemeral/openlords2/bunker-runs/<batch>/`, deletes them on the box.

## Python API (tools/drive/drive.py)
| name | |
|---|---|
| `HOST` | ssh target |
| `up(timeout=3) → bool` | the ssh master is up or can be made |
| `stage(build=True) → sha12` | exe = `staged_exe(sha)` = `C:\ol2-rhost\bin\<sha12>\ol2-rhost.exe` |
| `spawn_session(name, files: {str: bytes}, exe_win, args, wait, cap, floor_mb)` | → `("LIVE", pid, port)` / `("NOADMIT", why)` / `("NOSTART", text)`; files land in `C:\ol2-rhost\runs\drive\<name>` |
| `forward(lport, rport, cancel=False) → bool` | `127.0.0.1:lport` → bunker `127.0.0.1:rport` on the master |
| `kill_session(pid, name)`, `session_alive(pid, name) → bool`, `session_file(name, f) → bytes\|None` | |
| `run_batch(a, runs, tag, fetch=None, keep=False)` | `rhost.run_batch`'s contract |

Live args (as drive.py builds them): `--exe C:\ol2-rhost\game\Lords2.exe --game C:\ol2-rhost\game --run
C:\ol2-rhost\runs\drive\<name> --live 0 --live-idle N` → rhost writes `<run>\live.port` ("PORT PID").

## ssh: one multiplexed connection
bunker's sshd refuses past `MaxStartups 10:30:100`. Every command, rsync and session forward rides ONE master:
`ControlPath=$XDG_RUNTIME_DIR/ol2-bunker-<host>.ctl` (own master, separate from openrecet's), started once under a
lock (`ssh -fN -o ControlMaster=yes -o ControlPersist=2h`), checked with `ssh -O check`; sessions add
`ssh -O forward -L lp:127.0.0.1:port` and remove it with `-O cancel`.

## The pool (`pool.py`, runs on bunker as root)
- One transient unit **`ol2-bunker-pool.service`** (`sleep infinity` holding a cgroup): `MemoryMax=4G MemorySwapMax=0`,
  `CPUQuota=600% AllowedCPUs=0-3,6-9` (physical cores 4, 5 = CPUs 4,5,10,11 stay free). 4 G, not openrecet's 6 G: the
  box has 16 GB and openrecet's pool already takes 6. Env overrides on the box: `OL2_MEM OL2_CPUS OL2_QUOTA OL2_WINE
  OL2_HOST_FLOOR_MB OL2_STARTING_MB`. Created on first use, `--collect`.
- **Everything joins it**: each launcher writes its pid to the pool's `cgroup.procs` before it execs bwrap.
- **Admission** under one lock (`root/.admit.lock`, shared with `batch.py`, which sees the pool's cgroup at `/pool`):
  free = `memory.max − (memory.current − (file − shmem))`. Live session: ≤ `cap` sessions and ≥ `floor_mb` free after
  reserving `STARTING_MB` = 150 MB per session still replaying (no `live.port` yet), host MemAvailable ≥ 1 GB; else
  `NOADMIT` after `wait` s. Suggested drive.py defaults: **cap 8, floor_mb 512** (pool.py's own defaults). Batch run:
  ≥ 300 MB free (+ 150 MB per run started in the last 5 s) and host ≥ 1 GB, else it waits.
- `pool.py stat` (= `bunker.py status`).

## Container (bwrap per batch / per session)
- `bwrap --unshare-all --share-net --uid 1000 --gid 1000 --cap-drop ALL --die-with-parent --new-session`:
  `/nix/store` ro, `/proc` `/dev` `/tmp`(tmpfs), writable only `/ol2` (= `C:\ol2-rhost`); the prefix is a
  copy-on-write overlay (`--overlay-src prefix-headless --tmp-overlay /prefix`): one wineserver per container; scripts
  ro at `/bunker`; `--clearenv`, `WINEDEBUG=-all`, no DISPLAY (+ the prefix's `Graphics=null`, as `rhost.wine_stage`).
- A batch = one container (`batch.py`: RESULT in rhost.log → 20 s → kill the process group; `wineserver -k` at the end).
- A session = its own container under a detached `sh` (bwrap's parent); `pool.py kill PID NAME` (only when the PID's
  argv names `C:\ol2-rhost\runs\drive\NAME`) takes the pid namespace with it.

## Layout on bunker — everything under `/var/lib/ol2-bunker/`
| path | what |
|---|---|
| `gcroots/{wine-wow64-unstable,bwrap,python,fontconfig}` + `wine` → the first | `nix build --out-link` (cache hits; same store paths as openrecet's) |
| `bin/{pool.py,batch.py}` | pool / container launcher, in-container batch runner |
| `root/` = `C:\ol2-rhost` (`/ol2` inside) | `game/` (pristine copy, `.ol2-staged` stamp), `bin/<sha12>/ol2-rhost.exe`, `runs/<batch>/` (deleted after fetch unless `--keep`), `runs/drive/<session>/`, `.admit.lock` |
| `prefix-headless/` (`/prefix` inside, overlaid) | WINEPREFIX (`wineboot -i`, `Graphics=null`); `drive_c/ol2-rhost → /ol2` |

Outside it: only `/nix/var/nix/gcroots/auto/*` links to the gcroots (teardown deletes them) and the transient unit.
Wine: `/nix/store/zjxh9vbxp2h12a018i0krpswfidbjh1k-wine-wow64-11.19` (maximus: 11.20).

## Measured (2026-11-13, bin `b8b9a4da5784`)
- `rhost.py run endturn-2 --target bunker`: `ok f=3700 tick=764 rng=16f30ddd/37cfbfdd sem=9519efd095cd9d45` =
  `--target wine` = `expected.wine.json` (Lords2 state_sem matches maximus wine; openrecet saw one DLL-pointer slot differ).
- `rhost.py snap-check --only 'endturn|bot-s1-|siege-castles' --target bunker -j 4`: 8/8 PASS, 9 s batch.
- Batch run peak RSS (rhost + wine loader, plain or `--snap-test`): 53–60 MB.
- Live session (boot, `--live 0`): pool accounting +56 MB at boot, +70 MB after `run w1500` + 50 savestates (6 MB slabs);
  summed RSS of its container (wineserver, services, rhost) 163–177 MB (shared pages counted per process).
  → ~50 sessions fit the 4 G bound by memory; CPU (6 cores of quota) is the real limit for busy ones.

## Teardown
```sh
python3 tools/bunker/bunker.py teardown      # stops ol2-bunker-* units, rm -rf /var/lib/ol2-bunker, its gcroots/auto links
```
