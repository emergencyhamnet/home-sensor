# Adds an inbound firewall rule for TCP 8080 (private networks only).
# Run PowerShell as Administrator and execute:
#   powershell -ExecutionPolicy Bypass -File .\open_firewall.ps1

New-NetFirewallRule `
  -DisplayName "WS-2000 Local Weather Dashboard (TCP 8080)" `
  -Direction Inbound `
  -Action Allow `
  -Protocol TCP `
  -LocalPort 8080 `
  -Profile Private
Write-Host "Firewall rule added for TCP 8080 (Private profile)." -ForegroundColor Green
