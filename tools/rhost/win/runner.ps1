# ol2-rhost job runner — lives in the INTERACTIVE logon session (scheduled task at logon).
# Headless rhost runs need no desktop (software DirectDraw) and go straight over ssh; jobs that
# show a window (human recording, --show) are dropped here: C:\ol2-rhost\queue\<id>.job.ps1 →
# <id>.out (stdout+stderr) and <id>.done (exit code). FIFO. Separate from the sibling projects'
# runners (orc-/oss-/de-rhost): own dir, own task, own queue.
$q = 'C:\ol2-rhost\queue'
New-Item -ItemType Directory -Force $q | Out-Null
Set-Content -Path "$q\runner.pid" -Value $PID
while ($true) {
    $jobs = Get-ChildItem $q -Filter '*.job.ps1' -ErrorAction SilentlyContinue | Sort-Object Name
    foreach ($j in $jobs) {
        $id = $j.Name -replace '\.job\.ps1$', ''
        $run = Join-Path $q "$id.running.ps1"
        try { Move-Item $j.FullName $run -ErrorAction Stop } catch { continue }
        $out = Join-Path $q "$id.out"
        $p = Start-Process -FilePath powershell.exe -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',$run) `
             -NoNewWindow -Wait -PassThru -RedirectStandardOutput $out -RedirectStandardError "$out.err"
        if (Test-Path "$out.err") { Get-Content "$out.err" | Add-Content $out; Remove-Item "$out.err" }
        Set-Content -Path (Join-Path $q "$id.done") -Value $p.ExitCode
        Remove-Item $run -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 100
}
