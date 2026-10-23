# Register + start the ol2-rhost interactive runner (run once, as the desktop user; safe to re-run).
$u = "$env:COMPUTERNAME\$env:USERNAME"
$a = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument '-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File C:\ol2-rhost\runner.ps1'
$t = New-ScheduledTaskTrigger -AtLogOn -User $u
$p = New-ScheduledTaskPrincipal -UserId $u -LogonType Interactive -RunLevel Limited
$s = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName 'ol2-rhost-runner' -Action $a -Trigger $t -Principal $p -Settings $s -Force | Out-Null
Start-ScheduledTask -TaskName 'ol2-rhost-runner'
Start-Sleep 2
Get-ScheduledTask -TaskName 'ol2-rhost-runner' | ft TaskName,State
