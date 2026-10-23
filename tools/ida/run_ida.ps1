# run_ida.ps1 - run one headless IDA analysis/export of Lords2.exe (Windows side; staged to C:\ol2-ida\).
#   powershell -File C:\ol2-ida\run_ida.ps1 -Exe <exe|i64> -Out <dir> [-Opts "names=...;noasm;detour=chunk"]
# Writes <Out>\ida.log + the ol2_ida_export.py outputs; prints exit/peak/secs/pid.
# Kills only the idat PID it started (on timeout); never touches other projects' IDA jobs.
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$Out, [string]$Opts = "",
      [int]$TimeoutSec = 5400)
$ErrorActionPreference = 'Stop'
$ida = (Get-ChildItem 'C:\Program Files' -Filter 'IDA*' -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName + '\idat.exe'
$script = Join-Path $PSScriptRoot 'ol2_ida_export.py'
New-Item -ItemType Directory -Force $Out | Out-Null
$o = "ol2:$Out"; if ($Opts) { $o += ";$Opts" }
# (no embedded quotes: idat takes these verbatim; keep the paths space-free, as C:\ol2-ida\ is)
$a = @('-A', "-L$Out\ida.log", "-O$o", "-S$script", $Exe)
$p = Start-Process -FilePath $ida -ArgumentList $a -PassThru -WorkingDirectory (Split-Path $Exe) -WindowStyle Hidden
$t0 = Get-Date; $peak = 0
while (-not $p.HasExited) {
  try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 } } catch {}
  if (((Get-Date) - $t0).TotalSeconds -gt $TimeoutSec) { Stop-Process -Id $p.Id -Force; "TIMEOUT pid=$($p.Id)"; break }
  Start-Sleep -Milliseconds 500
}
"exit=$($p.ExitCode) peakMB=$([int]($peak/1MB)) secs=$([int]((Get-Date)-$t0).TotalSeconds) pid=$($p.Id) out=$Out"
