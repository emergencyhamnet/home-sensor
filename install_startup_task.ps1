# Creates a Scheduled Task that starts the dashboard at boot (runs hidden with pythonw).
# Run PowerShell as Administrator and execute:
#   powershell -ExecutionPolicy Bypass -File .\install_startup_task.ps1
#
# To remove later:
#   Unregister-ScheduledTask -TaskName "WS2000LocalWeather" -Confirm:$false

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

# Find pythonw.exe from PATH
$pythonw = (Get-Command pythonw.exe -ErrorAction SilentlyContinue).Source
if (-not $pythonw) {
  throw "pythonw.exe not found on PATH. Reinstall Python and check 'Add Python to PATH'."
}

$action = New-ScheduledTaskAction -Execute $pythonw -Argument "`"$here\app.py`""
$trigger = New-ScheduledTaskTrigger -AtStartup
$principal = New-ScheduledTaskPrincipal -UserId "NT AUTHORITY\LOCAL SERVICE" -LogonType ServiceAccount -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable

Register-ScheduledTask -TaskName "WS2000LocalWeather" -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
Write-Host "Scheduled Task 'WS2000LocalWeather' installed. Reboot or run it now from Task Scheduler." -ForegroundColor Green
