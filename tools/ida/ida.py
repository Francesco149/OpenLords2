#!/usr/bin/env python3
"""ida.py — drive headless IDA 9.x on the Windows box (`ssh win`) for Lords2.exe (GOG 2.0.0.3).

  python3 tools/ida/ida.py export [--fresh] [--noasm] [--no-names]   # (re)analyse + export + fetch
  python3 tools/ida/ida.py fetch                                     # re-fetch outputs only
  python3 tools/ida/ida.py names                                     # regenerate tools/ida/names.tsv
  python3 tools/ida/ida.py status

Windows layout (own dir; never touch C:\\orc-ida / C:\\oss-ida / C:\\de-ida):
  C:\\ol2-ida\\Lords2.exe       copy of the pristine C:\\ol2-rhost\\game\\Lords2.exe (sha256-checked)
  C:\\ol2-ida\\Lords2.exe.i64   analysed DB cache (IDA analysis + loader fixup only; no port names)
  C:\\ol2-ida\\names.tsv        staged tools/ida/names.tsv (applied in memory each export)
  C:\\ol2-ida\\out\\             outputs -> fetched to /mnt/ephemeral/openlords2/decompiled-ida/
The outputs are derived from the retail binary: never commit them.
"""
import argparse, os, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.path.join(ROOT, "tools", "ida")
LOCAL = os.environ.get("OL2_IDA_LOCAL", "/mnt/ephemeral/openlords2/decompiled-ida")
WIN = r"C:\ol2-ida"
EXE = WIN + r"\Lords2.exe"
I64 = EXE + ".i64"
PRISTINE = r"C:\ol2-rhost\game\Lords2.exe"
SHA256 = "6FEC043E59ED3D7AD5DE5B313093B8E2EE2D7832CCC224705E010BFE164D004A"
OUTPUTS = ["all_ida.c", "all_ida.asm", "functions_ida.tsv", "data_xrefs.tsv", "strings_ida.tsv",
           "export_summary.txt", "export_log.txt", "ida.log"]


def ssh(cmd, check=True):
    r = subprocess.run(["ssh", "-o", "BatchMode=yes", "win", cmd], capture_output=True, text=True)
    if check and r.returncode:
        sys.exit("ssh failed (%d): %s\n%s" % (r.returncode, cmd[:200], (r.stderr or "")[-2000:]))
    return r.stdout


def scp_to(local, remote):
    subprocess.run(["scp", "-q", "-o", "BatchMode=yes", local, "win:" + remote.replace("\\", "/")], check=True)


def stage(names):
    ssh(r"New-Item -ItemType Directory -Force %s\out | Out-Null" % WIN)
    have = ssh("if (Test-Path '%s') { (Get-FileHash '%s').Hash } else { 'missing' }" % (EXE, EXE)).strip()
    if have != SHA256:
        if have != "missing":
            sys.exit("refusing: %s exists with an unexpected hash (%s); inspect it by hand" % (EXE, have[:16]))
        print("staging %s from %s" % (EXE, PRISTINE))
        ssh("Copy-Item '%s' '%s'" % (PRISTINE, EXE))
        have = ssh("(Get-FileHash '%s').Hash" % EXE).strip()
        if have != SHA256:
            sys.exit("sha256 mismatch for %s: %s" % (EXE, have))
    for f in ("ol2_ida_export.py", "run_ida.ps1"):
        scp_to(os.path.join(HERE, f), WIN + "\\" + f)
    if names:
        scp_to(os.path.join(HERE, "names.tsv"), WIN + r"\names.tsv")


def gen_names():
    subprocess.run([sys.executable, os.path.join(HERE, "gen_names.py")], check=True)


def export(fresh, noasm, names):
    if fresh:
        ssh("Remove-Item -ErrorAction SilentlyContinue '%s'; exit 0" % I64)
    have_db = "True" in ssh("Test-Path '%s'" % I64)
    opts = []
    if names: opts.append(r"names=%s\names.tsv" % WIN)
    if noasm: opts.append("noasm")
    cmd = r"powershell -NoProfile -ExecutionPolicy Bypass -File %s\run_ida.ps1 -Exe '%s' -Out '%s\out'" % (
        WIN, I64 if have_db else EXE, WIN)
    if opts:
        cmd += " -Opts '%s'" % ";".join(opts)
    print("running IDA (%s) ..." % ("cached .i64" if have_db else "fresh analysis"), flush=True)
    t0 = time.time()
    out = ssh(cmd).strip()
    print("%s  [wall %.0fs]" % (out, time.time() - t0))


def fetch():
    os.makedirs(LOCAL, exist_ok=True)
    for f in OUTPUTS:
        subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "win:%s/out/%s" % (WIN.replace("\\", "/"), f), LOCAL + "/"],
                       check=False)
    s = os.path.join(LOCAL, "export_summary.txt")
    print("fetched -> %s" % LOCAL)
    print(open(s).read().strip() if os.path.exists(s) else "(no summary)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["export", "fetch", "names", "status"])
    ap.add_argument("--fresh", action="store_true", help="re-analyse from the exe (drop our cached .i64)")
    ap.add_argument("--noasm", action="store_true", help="skip all_ida.asm")
    ap.add_argument("--no-names", action="store_true", help="export with IDA's own names only")
    a = ap.parse_args()
    if a.cmd == "status":
        print(ssh(r"Get-ChildItem %s, %s\out -ErrorAction SilentlyContinue | Select FullName,Length,LastWriteTime"
                  r" | ft -AutoSize | Out-String -Width 200" % (WIN, WIN)))
        return
    if a.cmd == "names":
        gen_names(); return
    if a.cmd == "export":
        if not a.no_names:
            gen_names()
        stage(not a.no_names)
        export(a.fresh, a.noasm, not a.no_names)
    fetch()


if __name__ == "__main__":
    main()
