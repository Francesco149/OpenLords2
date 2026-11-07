# play.ps1 - Lords II free-play capture (OpenLords2 rhost). Launched from the Desktop shortcut "Lords II capture"
# (play.cmd; installed by `rhost.py play-install`). Retail Lords2.exe runs PURE (no port hooks) inside rhost with real
# input, realtime, sound, the movies; every input event is recorded. Close the game window (or quit the game) to end.
#   C:\ol2-rhost\play\current.txt            the frozen rhost build (play\bin\<sha>) new sessions use
#   C:\ol2-rhost\play\carry\                 the files the game wrote in the newest finished session (saves, castles.dat
#                                            ...): the next session starts with them, so "Load game" continues a campaign
#   C:\ol2-rhost\play\sessions\<stamp>\      trace.jsonl  start\ (the carry it began with)  meta.json  rhost.log  state_end.rhs
# rhost.py play-status / play-fetch (maximus) list the sessions and import them as scenarios (tests/scenarios/hp-<stamp>),
# each replayed headless as a self-check (same frame count + state hash as the recording).
# Test mode (rhost.py play-test): -Root another dir, -TestArgs extra rhost flags (e.g. "--frames 600"); no pause.
param([string]$Bin = '', [string]$Root = 'C:\ol2-rhost\play', [string]$TestArgs = '')
$ErrorActionPreference = 'Stop'
$P = $Root
$game = 'C:\ol2-rhost\game'
if (-not $Bin) { $Bin = (Get-Content "$P\current.txt" -TotalCount 1).Trim() }
$exe = "$P\bin\$Bin\ol2-rhost.exe"
if (-not (Test-Path $exe)) { Write-Host "no rhost build at $exe (rhost.py play-install)"; exit 2 }
$one = New-Object System.Threading.Mutex($false, $(if ($TestArgs) { 'Local\ol2-rhost-playtest' } else { 'Local\ol2-rhost-play' }))
try { $mine = $one.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $mine = $true }
if (-not $mine) { Write-Host 'a capture session is already running (its console window)'; exit 2 }

$session = Get-Date -Format 'yyyyMMdd-HHmmss'
$d = "$P\sessions\$session"
New-Item -ItemType Directory -Force "$d\start" | Out-Null
if (Test-Path "$P\volatile.txt") { Copy-Item "$P\volatile.txt" $d }
$carry = "$P\carry"
$startFiles = @()
if (Test-Path $carry) {
    foreach ($f in Get-ChildItem $carry -File) {
        Copy-Item $f.FullName "$d\start\$($f.Name)"
        Copy-Item $f.FullName "$d\$($f.Name)"          # the run dir: rhost's file overlay reads the game's writes from here
        $startFiles += $f.Name
    }
}
Write-Host "Lords II capture session $session (rhost $Bin)"
Write-Host "  $(if ($startFiles.Count) { 'carried over from the last session: ' + ($startFiles -join ', ') } else { 'fresh start (no saves yet)' })"
Write-Host "  play as long as you like; close the game window (or quit the game) to end the recording."

$argv = @('--exe', "$game\Lords2.exe", '--game', $game, '--run', $d, '--record', 'trace.jsonl', '--movies', '--dump-at', 'end')
if ($TestArgs) { $argv += ($TestArgs -split ' ' | Where-Object { $_ }) }
$t0 = Get-Date
$proc = Start-Process -FilePath $exe -ArgumentList $argv -PassThru -NoNewWindow `
        -RedirectStandardOutput "$d\stdout.txt" -RedirectStandardError "$d\stderr.txt"
$null = $proc.Handle                       # hold the handle: ExitCode reads empty after exit without it
try { $proc.PriorityClass = 'AboveNormal' } catch {}
$proc.WaitForExit()
$code = $proc.ExitCode
$t1 = Get-Date

$res = @{}
if (Test-Path "$d\rhost.log") {
    $l = Select-String -Path "$d\rhost.log" -Pattern '^RESULT ' | Select-Object -Last 1
    if ($l) { foreach ($kv in ($l.Line -split ' ')) { $i = $kv.IndexOf('='); if ($i -gt 0) { $res[$kv.Substring(0, $i)] = $kv.Substring($i + 1) } } }
}
# what the game wrote this session (the run dir minus rhost's output and retail's log status.txt) becomes the carry
$ours = @('trace.jsonl', 'status.txt', 'rhost.log', 'stdout.txt', 'stderr.txt', 'volatile.txt', 'meta.json', 'hooks.txt')
$wrote = @(Get-ChildItem $d -File | Where-Object { $ours -notcontains $_.Name -and $_.Name -notlike 'state_*.rhs' -and $_.Name -notlike 'shot_*' })
$adopted = $false
if ($res.Count -gt 0 -and (Test-Path "$d\trace.jsonl")) {
    $tmp = "$P\carry.new"
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $tmp | Out-Null
    foreach ($f in $wrote) { Copy-Item $f.FullName "$tmp\$($f.Name)" }
    if (Test-Path $carry) { Remove-Item $carry -Recurse -Force }
    Move-Item $tmp $carry
    $adopted = $true
}
$meta = [ordered]@{ session = $session; bin = $Bin; exit = $code; started = $t0.ToString('s'); ended = $t1.ToString('s');
                    wall_s = [int]($t1 - $t0).TotalSeconds; start_files = $startFiles; wrote = @($wrote | ForEach-Object { $_.Name });
                    carry_adopted = $adopted; result = $res }
$meta | ConvertTo-Json -Depth 4 | Set-Content "$d\meta.json"
Write-Host ("session {0}: frame {1}, {2:N0} s, {3}" -f $session, $res['frame'], ($t1 - $t0).TotalSeconds,
            $(if ($res['reason']) { $res['reason'] } else { "exit $code (no RESULT line: see $d\rhost.log)" }))
Write-Host "  recorded to $d\trace.jsonl - thanks!"
