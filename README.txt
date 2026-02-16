WS-2000 → Windows 10 (LAN-only) → iPad Dashboard
================================================

What this is
------------
This package lets your Ambient Weather WS-2000 upload to your Windows 10 home server on your LAN (no internet/cloud),
and provides a local web dashboard suitable for an old iPad/tablet display.

The iPad/browser stays "dumb": it only loads the page and polls the local server at /data.
The Windows server adds a 5‑day forecast plus sun/moon info so older Safari/iPads don't need to call external APIs.

URLs
----
Dashboard:  http://<YOUR_WINDOWS_IP>:8080/
JSON Data:  http://<YOUR_WINDOWS_IP>:8080/data
Health:     http://<YOUR_WINDOWS_IP>:8080/health

/data includes:
  - Raw station fields (latest.json)
  - sunrise/sunset/moonrise/moonset
  - moon_phase (Waxing/Waning/etc)
  - forecast_5day (high/low + simple condition)

1) Install Python (one time)
----------------------------
Install Python 3.10+ from python.org
During install, CHECK: "Add Python to PATH"

2) Install dependencies
-----------------------
Double-click: install.bat

Note: Forecast + sun/moon can run without extra packages (uses Python standard library fallbacks).
If you do install extras, they improve accuracy/reliability:
  - astral (more accurate sun/moon)
  - requests (simpler HTTP than urllib)

3) Open firewall port (one time)
--------------------------------
Run PowerShell as Administrator in this folder, then:

  powershell -ExecutionPolicy Bypass -File .\open_firewall.ps1

(Or allow TCP 8080 inbound in Windows Defender Firewall Advanced settings.)

4) Run the server
-----------------
Double-click: run.bat

Test from the Windows machine:
  http://localhost:8080/data

5) Configure the WS-2000 (Customized)
-------------------------------------
On the WS-2000 console:
  Menu → Weather Server → Customized

Set:
  Server IP:   <YOUR_WINDOWS_IP>   (example: 192.168.1.25)
  Port:        8080
  Path:        /weather
  Interval:    5–16 seconds (whatever the console allows)
  Protocol:    HTTP

After you save, the station should start posting to:
  http://<YOUR_WINDOWS_IP>:8080/weather

You can verify data is arriving by loading:
  http://<YOUR_WINDOWS_IP>:8080/data

6) iPad display (kiosk-style)
-----------------------------
- Connect iPad to the same Wi‑Fi/LAN
- Open Safari to: http://<YOUR_WINDOWS_IP>:8080/
- Enable Guided Access for kiosk mode
- Set Auto-Lock to Never

Optional: Start automatically at boot (no extra software)
---------------------------------------------------------
Run PowerShell as Administrator:

  powershell -ExecutionPolicy Bypass -File .\install_startup_task.ps1

This creates a Scheduled Task named: WS2000LocalWeather
that runs at startup using pythonw.exe (no console window).

You do NOT need to add run.bat to Startup. The scheduled task starts app.py directly.
If you prefer a visible console window, skip the task and just double-click run.bat when needed.

Notes / Key Names
-----------------
Different firmware can use slightly different field names.
This dashboard expects common "Customized" keys such as:
  tempf, humidity, windspeedmph, windgustmph, rainratein, dailyrainin, baromrelin, baromabsin

If your JSON at /data uses different keys, tell me what you see and I’ll update the dashboard mapping.

Troubleshooting
---------------
- If the iPad can't load the page: confirm Windows IP, firewall rule, and that the server is running.
- If /data stays empty: confirm WS-2000 is set to Customized and pointing at the correct IP/port/path.
- Make sure the Windows box is on wired ethernet (you are) and the WS-2000 can reach it on the LAN.
