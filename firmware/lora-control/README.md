LoRa Control + Dummy Sensor Firmware
===================================

Target Hardware
---------------

- PlatformIO board target is `esp32dev` (generic ESP32 DevKit profile).
- Current live deployment uses ESP32 + LoRa nodes with pin mapping compatible with TTGO T-Beam style hardware.
- This repo uses environment-specific pin defines in `platformio.ini`; if your board differs, adjust pin flags per env.

Current Working Pair (recommended)
----------------------------------

Use this pair for the current live setup:

- Control board (`COM8`): `env:control_wifi`
- Monitor board (`COM9`): `env:monitor_lora_lp`

This pairing provides:

- Control board OLED rotation (TEMP / HUMID / CO2 at ~2s page interval)
- Server alarm display on OLED
- Alarm silence/reset button on GPIO38
- Buzzer on active alarm
- Control board local sensor payload POST to `/api/lora`
- LoRa RX from monitor node forwarded to `/api/lora`

OLED + alarm reset behavior (control node)
-----------------------------------------

- OLED is enabled on control node (`OLED_ENABLED=1`, `OLED_ADDR=0x3C`, `128x64`).
- Normal display rotates through `TEMP`, `HUMID`, and `CO2`.
- Active alarms force OLED alarm screen (for example, leak).
- Alarm reset/silence button is on `GPIO38` (active LOW).
- `GPIO38` is input-only with no internal pull-up; use external `10k` pull-up to `3.3V`.
- Pressing the button silences buzzer output while alarm condition remains active; OLED shows muted alarm state.
- Buzzer uses `GPIO4` (`BUZZER_ACTIVE_HIGH=1`).

Flash commands (explicit env):

- Control board (COM8):
   - `C:\Users\steve\.platformio\penv\Scripts\python.exe -m platformio run -e control_wifi -t upload --upload-port COM8`
- Monitor board (COM9):
   - `C:\Users\steve\.platformio\penv\Scripts\python.exe -m platformio run -e monitor_lora_lp -t upload --upload-port COM9`

If using VS Code Build/Upload buttons, ensure `default_envs = control_wifi` in `platformio.ini`.

Safe control pin profile (rewire reference)
------------------------------------------

For `env:control_wifi`, the current alarm/buzzer related pins are:

- `ALARM_SILENCE_PIN=38`
- `ALARM_SILENCE_ACTIVE_LEVEL=0` (button press = LOW)
- `ALARM_SILENCE_USE_PULLUP=0` (external pull-up required)
- `BUZZER_PIN=4`
- `SENSOR_PWR_PIN=13`

Wiring notes:

- For alarm silence on GPIO38, wire switch-to-GND (active LOW).
- GPIO38 is input-only and has no internal pull-up, so add external `10k` pull-up to `3.3V`.

Safe monitor pin profile (rewire reference)
------------------------------------------

To avoid known pin conflicts on T-Beam style boards, `env:monitor_lora_lp` is currently set to:

- `LEAK_DIGITAL_PIN=4` (active LOW)
- `GARAGE_DOOR_PIN=25` (single combined `open_door` input)
- `SIDE_DOOR_PIN=-1` (disabled)
- `MOTION_PIN=15`
- `LIGHT_SENSE_PIN=-1` (disabled for stability)
- `IO4_IDLE_LEVEL=-1` (do not force GPIO4)

Wiring notes:

- Combine side + garage door contacts in series to one door alarm line into GPIO25.
- Door/motion inputs use `INPUT_PULLUP` in firmware, so external pull-ups are optional for GPIO25/GPIO15.
- For stronger noise immunity on long wires, add external pull-ups (for example `10k` to `3.3V`) near the board.
- Leak input on GPIO4 should use active-low logic (`0V = leak`, `3.3V = no leak`).

This folder provides two minimal PlatformIO targets:

- `gateway`: LoRa receive + HTTP forward to your WS-2000 server
- `sensor_dummy`: LoRa transmitter sending dummy sensor JSON
- `i2c_scan`: quick bus/address scan
- `pmu_probe`: inspect PMU/battery registers at `0x34`
- `oled_roll_test`: simple rolling OLED page test

Server endpoints used
---------------------

- `POST /api/lora`
- `POST /api/base`
- Dashboard readback: `GET /monitor_data`

Quick start
-----------

1. Edit `platformio.ini` under `[env:gateway]`:
   - `WIFI_SSID`
   - `WIFI_PASS`
   - `SERVER_BASE_URL` (example: `http://192.168.1.49:8080`)

2. Optional CM1107N UART CO2 input (enabled by default):
   - `CM1107_ENABLED=1`
   - `CM1107_BAUD=9600`
   - `CM1107_RX_PIN=16`
   - `CM1107_TX_PIN=17`
   - If no valid CM1107 frame is seen, gateway falls back to dummy `co2_ppm`.

3. Confirm LoRa pins/frequency in build flags:
   - `LORA_PIN_SS`, `LORA_PIN_RST`, `LORA_PIN_DIO0`
   - `LORA_FREQ_MHZ` (default `915.0`)

4. Flash gateway firmware (gateway board on COM8):
   - `C:\Users\steve\.platformio\penv\Scripts\python.exe -m platformio run -e gateway -t upload --upload-port COM8`

5. Flash dummy sensor firmware (sensor board):
   - set that board's COM port
   - `C:\Users\steve\.platformio\penv\Scripts\python.exe -m platformio run -e sensor_dummy -t upload --upload-port COMX`

6. Observe gateway serial monitor:
   - `C:\Users\steve\.platformio\penv\Scripts\python.exe -m platformio device monitor -b 115200 -p COM8`

Expected behavior
-----------------

- `sensor_dummy` sends JSON payload every interval (default 10s)
- `gateway` receives packet, logs RSSI/SNR, posts to `/api/lora`
- `gateway` posts base dummy values to `/api/base` every 60s
- If CM1107N is wired and valid, `gateway` posts CM1107-derived `co2_ppm` to `/api/base`
- server monitor endpoint (`/monitor_data`) shows sensor and base updates

Gateway battery alert behavior
------------------------------

- `gateway` now includes PMU fields in `/api/base` when available.
- Server monitor raises a warning when base battery voltage is below `3.40V`.

Dual CO2 test mode (CM1107N + SCD41)
------------------------------------

- `gateway` now polls both CO2 sensors in parallel:
   - CM1107N on UART (Serial2)
   - SCD41 on I2C (`0x62`)
- `/api/base` now includes comparison fields when available:
   - `co2_src` (active value used for `co2_ppm`)
   - `co2_cm1107_ppm`
   - `co2_scd41_ppm`
   - `scd41_t_c`, `scd41_rh`

OLED rolling test
-----------------

Use this to verify your I2C OLED plus known sensor addresses while wiring:

- Flash test:
   - `C:\Users\steve\.platformio\penv\Scripts\python.exe -m platformio run -e oled_roll_test -t upload --upload-port COM8`
- Monitor output:
   - `C:\Users\steve\.platformio\penv\Scripts\python.exe -m platformio device monitor -b 115200 -p COM8`

Screen pages rotate every ~2.5s and show:

- boot/uptime page
- I2C presence for `0x3C` (OLED) and `0x34` (PMU)
- I2C presence for `0x44` (temp/RH) and `0x62` (SCD41)
- A/B CO2 test reminder page

Notes
-----

- Keep only one app open per COM port (PuTTY/monitor/upload conflict).
- If no packets are received, verify pin map and LoRa frequency/SF/CR match on both boards.

Monitor-side status and remaining tests
--------------------------------------

Current monitor-side prototype status:

- Base ingest path is working (`/api/base`) with CO2/T/RH + PMU fields.
- Dashboard monitor rendering is working (`/monitor`, `/monitor_data`).
- Alert logic is implemented for leak, node battery, offline, high CO2, and base low battery.

Recommended remaining monitor-side checks:

1. API reachability from gateway board to server (`/api/base` and `/api/lora` both return 2xx).
2. Long-run stability test (30-60 min) for sensor reads and monitor refresh.
3. A/B CO2 drift check (CM1107 vs SCD41) after warm-up.
4. Power-state test (USB present vs battery-only) to verify PMU values and low-battery alert timing.

Second board LoRa link test (no sensors required)
-------------------------------------------------

Use the dedicated transmitter target `sensor_test_tx` on the second board.

Build only:

- `C:\Users\steve\.platformio\penv\Scripts\python.exe -m platformio run -e sensor_test_tx`

Flash second board:

- `C:\Users\steve\.platformio\penv\Scripts\python.exe -m platformio run -e sensor_test_tx -t upload --upload-port COMX`

Expected behavior:

- Second board sends dummy LoRa payload every 5s (`id=test-node`).
- Gateway logs LoRa RX packets and forwards to `/api/lora`.
- `GET /monitor_data` should show `test-node` with RSSI/SNR and packet updates.

Low-power sensor mode (deep sleep + switched sensor rail)
--------------------------------------------------------

`sensor_test_tx` is now configured for low-power operation:

- disables Wi-Fi/Bluetooth at boot
- turns board LED off (configurable)
- enables external sensor rail output pin
- waits warmup delay
- transmits one LoRa payload
- sleeps for long interval via ESP32 deep sleep timer

Current defaults in `platformio.ini` (`env:sensor_test_tx`):

- `LOW_POWER_MODE=1`
- `SENSOR_SLEEP_SECONDS=300`
- `SENSOR_PWR_PIN=13`
- `SENSOR_PWR_ACTIVE_HIGH=1`
- `SENSOR_PWR_WARMUP_MS=1500`
- `BOARD_LED_PIN=25`
- `BOARD_LED_ACTIVE_LOW=1`

If your external sensor power switch transistor/MOSFET is active-low, set:

- `SENSOR_PWR_ACTIVE_HIGH=0`

Central Node Pinout (I2C + UART CO2 + OLED)
--------------------------------------------

Use this wiring for your requested test phase (original I2C sensors plus CM1107N UART in parallel):

- LoRa radio control pins (as currently compiled):
   - `LORA_PIN_SS` = GPIO18
   - `LORA_PIN_RST` = GPIO14
   - `LORA_PIN_DIO0` = GPIO26
   - `LORA_FREQ_MHZ` = 923.0 (TTGO 923MHz module default)

- CM1107N (UART CO2, Serial2):
   - `CM1107_RX_PIN` = GPIO34 (ESP32 RX2, connect from sensor TX)
   - `CM1107_TX_PIN` = -1 (not used in receive-only mode)
   - `CM1107_BAUD` = 9600

- I2C bus (shared):
   - SDA = GPIO21
   - SCL = GPIO22
   - Put these devices on the same bus:
      - original I2C CO2 sensor (for A/B testing)
      - original I2C temp/RH sensor
      - I2C OLED display (usually address `0x3C`)

Electrical notes:

- ESP32 GPIO is 3.3V and not 5V tolerant.
- If CM1107N UART TX is 5V logic, level-shift before GPIO16.
- Keep all grounds common.
- Ensure I2C pull-ups are to 3.3V.

PMU / Unknown I2C device at 0x34
-------------------------------

- On this board, scan results showed `0x34` consistently, which is typically an AXP power-management IC.
- Gateway firmware now polls `0x34` and includes PMU telemetry in `/api/base` when available:
   - `pmu_present`
   - `pmu_vbat_v`
   - `pmu_vbus_v`
   - `pmu_ichg_ma`
   - `pmu_idis_ma`

Address conflict fallback:

- If two I2C devices share the same fixed address, move one to a second bus
   (example `Wire1`: SDA=GPIO25, SCL=GPIO27).
