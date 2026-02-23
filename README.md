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
Monitor:    http://<YOUR_WINDOWS_IP>:8080/monitor
Monitor API:http://<YOUR_WINDOWS_IP>:8080/monitor_data
Health:     http://<YOUR_WINDOWS_IP>:8080/health
LoRa Ingest: http://<YOUR_WINDOWS_IP>:8080/api/lora
Base Ingest: http://<YOUR_WINDOWS_IP>:8080/api/base

/data includes:
  - Raw station fields (latest.json)
  - sunrise/sunset/moonrise/moonset
  - moon_phase (Waxing/Waning/etc)
  - forecast_5day (high/low + simple condition)

/api/lora expects gateway packets like:
  {
    "gateway_id":"gw-main",
    "rx_ts":1760000000,
    "rssi":-92,
    "snr":7.5,
    "payload":{
      "v":1,
      "id":"laundry",
      "seq":1042,
      "t_c":21.6,
      "rh":44.2,
      "co2_ppm":821,
      "leak":1,
      "garage_open":0,
      "side_open":1,
      "motion":0,
      "alarm_silenced":0,
      "wet":3120,
      "vbatt":3.62,
      "batt_pct":28
    }
  }

/api/base expects gateway environmental readings like:
  {
    "src":"base",
    "co2_ppm":865,
    "t_c":22.3,
    "rh":41.2,
    "voc_index":143
  }

Server writes monitor files to:
  data/latest/<nodeId>.json
  data/latest/base.json
  data/events/leak.log

Monitor alert thresholds include:
  - Node battery warn: batt_pct < 20%
  - Base battery warn: PMU VBAT < 3.40V
  - Base CO2 warn: > 1200 ppm
  - Offline timeout uses per-node windows (default 10 min)

Telemetry Map (Source -> Field -> Units -> Alert)
-------------------------------------------------

Central node / gateway (`/api/base` -> `data/latest/base.json`):
  - `co2_ppm`          -> ppm      -> Warn if > 1200
  - `co2_src`          -> text     -> active source: `cm1107n` / `scd41` / `dummy`
  - `co2_cm1107_ppm`   -> ppm      -> compare only (no direct alert)
  - `co2_scd41_ppm`    -> ppm      -> compare only (no direct alert)
  - `t_c`              -> deg C    -> display
  - `rh`               -> %RH      -> display
  - `scd41_t_c`        -> deg C    -> compare/display
  - `scd41_rh`         -> %RH      -> compare/display
  - `voc_index`        -> index    -> display
  - `wifi_rssi`        -> dBm      -> display/diagnostic
  - `uptime_s`         -> seconds  -> diagnostic

Central PMU (`0x34`, merged into base payload):
  - `pmu_present`      -> bool     -> indicates PMU detected
  - `pmu_vbat_v`       -> volts    -> Warn if < 3.40
  - `pmu_vbus_v`       -> volts    -> display/diagnostic
  - `pmu_ichg_ma`      -> mA       -> display/diagnostic
  - `pmu_idis_ma`      -> mA       -> display/diagnostic

CM1107N UART diagnostics (in base payload when available):
  - `cm1107_temp_raw`  -> raw      -> diagnostic
  - `cm1107_rh_raw`    -> raw      -> diagnostic
  - `cm1107_status`    -> raw      -> diagnostic
  - `cm1107_abc_days`  -> days     -> calibration/ABC diagnostic

Remote LoRa node payload (`/api/lora` -> `data/latest/<nodeId>.json`):
  - `payload.id`       -> text     -> node identifier
  - `payload.seq`      -> count    -> packet sequence
  - `payload.t_c`      -> deg C    -> display
  - `payload.rh`       -> %RH      -> display
  - `payload.co2_ppm`  -> ppm      -> display
  - `payload.leak`     -> 0/1      -> Critical alert if 1
  - `payload.garage_open` -> 0/1   -> Warn if 1
  - `payload.side_open`   -> 0/1   -> Warn if 1
  - `payload.motion`      -> 0/1   -> Warn if 1
  - `payload.alarm_silenced` -> 0/1 -> display
  - `payload.wet`      -> raw      -> leak context
  - `payload.vbatt`    -> volts    -> display
  - `payload.batt_pct` -> %        -> Warn if < 20
  - `rssi`             -> dBm      -> link quality
  - `snr`              -> dB       -> link quality
  - `rx_ts`            -> epoch s  -> offline logic

Derived monitor state (`/monitor_data`):
  - `offline`          -> bool     -> Warn if no telemetry within node timeout window
  - `alerts[]`         -> list     -> issue-only events (leak/door/motion/battery/offline/high-CO2/base-battery)
  - `thresholds`       -> object   -> active threshold values used by server

Current monitor UI behavior:
  - Node cards show each node status (`ONLINE` / `OFFLINE`) at the top.
  - Alerts card shows only active issues.
  - Offline appears as an alert only when a node is offline.

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
  Server IP:   <YOUR_WINDOWS_IP>   (example: 192.168.1.49)
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

Central Node Wiring (ESP32 + LoRa + I2C + CM1107N)
---------------------------------------------------
For your planned test setup (gateway + monitor module), wire as follows.

Current firmware pin assumptions (gateway / central node):
  - LoRa control pins:
    - NSS/SS: GPIO18
    - RST:    GPIO14
    - DIO0:   GPIO26
  - LoRa SPI profile in active use:
    - SCK: GPIO5, MISO: GPIO19, MOSI: GPIO27
  - CM1107N UART (gateway):
    - ESP32 RX2: GPIO34  <- sensor TX (read-only)
    - ESP32 TX2: not used
    - UART baud: 9600

Monitor module pin map (remote node):
  - I2C Temp/Humidity bus:
    - SDA: GPIO21
    - SCL: GPIO22
  - Leak detect digital input:
    - GPIO23 (active LOW, uses INPUT_PULLUP)
  - Garage door contact input:
    - GPIO32 (active LOW, INPUT_PULLUP)
  - Side door contact input:
    - GPIO33 (active LOW, INPUT_PULLUP)
  - Motion detect input:
    - GPIO25 (active LOW, INPUT_PULLUP)
  - Light sense analog input:
    - GPIO35 (ADC input, 0-4095 raw)
  - Buzzer output (to transistor buffer):
    - GPIO4 (active HIGH)
  - Alarm silence/reset button:
    - GPIO15 (active LOW, INPUT_PULLUP)
    - wire normally-open button from GPIO15 to GND
  - Sensor rail power enable:
    - GPIO13 (active HIGH)
  - Spare UART for future sensors/debug:
    - RX: GPIO16
    - TX: GPIO17
  - OLED display (I2C shared bus):
    - Addr 0x3C, 128x64 (SSD1306)

Monitor payload fields from these digital inputs:
  - `leak` (from GPIO23): 1=active, 0=inactive
  - `garage_open` (from GPIO32): 1=active/open, 0=inactive/closed
  - `side_open` (from GPIO33): 1=active/open, 0=inactive/closed
  - `motion` (from GPIO25): 1=motion/active, 0=inactive
  - `light_level` (from GPIO35 ADC): raw level 0-4095 (higher/lower depends on sensor divider orientation)

Buzzer behavior:
  - Firmware uses GPIO4 to drive a transistor-buffered buzzer output.
  - Leak alert pattern is three beeps and repeats on cooldown while leak remains active.
  - Press silence button (GPIO15 -> GND) to mute buzzer while leak remains active.
  - Silence latch resets automatically after leak clears.

OLED behavior (control node):
  - Data source is local onboard sensors on the control board (SCD41 on I2C 0x62), not monitor node telemetry.
  - Normal mode cycles every 2 seconds: `TEMP` -> `HUMID` -> `CO2`.
  - On alarm (currently leak), OLED overrides cycle and shows `ALARM: LEAK`.
  - If silenced, OLED shows `ALARM: LEAK (MUTED)` until leak clears.
  - Monitor-node data continues to flow to the server independently.

GPIO15 note:
  - GPIO15 is a boot-strapping pin; avoid holding the silence button during reset/power-up.

Leak detector module (MH-Sensor series, LM393 comparator)
----------------------------------------------------------
Module pins are typically: `VCC`, `GND`, `D0`, `A0`.

Recommended connection (digital threshold mode):
  - `VCC` -> ESP32 `3V3`
  - `GND` -> ESP32 `GND`
  - `D0`  -> ESP32 `GPIO23` (configured as leak input)
  - `A0`  -> leave unconnected (optional; only needed for analog trend)

Important electrical note:
  - Power this LM393 board from `3.3V` when connected directly to ESP32 GPIO.
  - If powered from `5V`, `D0` may output 5V logic and can damage ESP32 input pins unless level shifted.

How the threshold potentiometer works:
  - The trim-pot sets the comparator trip point.
  - Firmware uses digital `D0` only, so leak state toggles when measured conductivity crosses that threshold.
  - Current firmware expects active-LOW leak (`LEAK_ACTIVE_LEVEL=0`), meaning:
    - `D0 = LOW` -> leak detected
    - `D0 = HIGH` -> dry/no leak

Quick calibration procedure:
  1) Power module and ESP32, keep probe dry.
  2) Watch serial output/payload leak field (`leak=0` expected when dry).
  3) Put a small drop of water on probe area.
  4) Adjust potentiometer until leak flips reliably to `leak=1`.
  5) Dry probe and confirm it returns to `leak=0`.

Optional analog mode (`A0`):
  - If you want raw wetness trend (not just threshold), wire `A0` to an ESP32 ADC input (example `GPIO35`) and add ADC read logic.
  - Keep module VCC at 3.3V for safe ADC range.

Optional light-left-on sensor (solar cell / photo sensor):
  - Wire sensor divider output to `GPIO35` (ADC).
  - Keep divider output within 0-3.3V.
  - Firmware publishes `light_level` so you can set thresholds server-side (for example, warn if value stays above/below calibrated nighttime baseline).

Pin selection notes:
  - GPIO0 is a boot-strap pin; avoid it for leak probes/switches that can pull it low during reset.
  - GPIO4 is currently used for LED behavior in `sensor_test_tx`; keep it free unless LED mapping is changed.
  - GPIO23 is available for leak input because monitor firmware now avoids LoRa fallback profiles that used GPIO23.

Power and signal notes:
  - Common GND is required across all modules.
  - ESP32 GPIO is 3.3V logic and not 5V tolerant.
  - 3.3V I2C pull-ups are recommended on the shared I2C bus.

Address conflict fallback:
  - If two I2C devices conflict on address, move one device to a second I2C bus
    (example spare pins: SDA=GPIO25, SCL=GPIO27) and initialize separately.

Troubleshooting
---------------
- If the iPad can't load the page: confirm Windows IP, firewall rule, and that the server is running.
- If /data stays empty: confirm WS-2000 is set to Customized and pointing at the correct IP/port/path.
- Make sure the Windows box is on wired ethernet (you are) and the WS-2000 can reach it on the LAN.
