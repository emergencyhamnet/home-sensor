#include <Arduino.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_bt.h>
#include <esp_wifi.h>
#include <esp_sleep.h>

#ifndef NODE_ID
#define NODE_ID "laundry"
#endif

#ifndef SENSOR_TX_INTERVAL_MS
#define SENSOR_TX_INTERVAL_MS 10000
#endif

#ifndef CONTROL_WIFI_DIRECT
#define CONTROL_WIFI_DIRECT 0
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

#ifndef SERVER_BASE_URL
#define SERVER_BASE_URL "http://192.168.1.49:8080"
#endif

#ifndef LOW_POWER_MODE
#define LOW_POWER_MODE 0
#endif

#ifndef ACTIVE_DIAG_ENABLED
#define ACTIVE_DIAG_ENABLED 1
#endif

#ifndef ACTIVE_DIAG_INTERVAL_MS
#define ACTIVE_DIAG_INTERVAL_MS 2000
#endif

#ifndef SYNTHETIC_BATTERY_ENABLED
#define SYNTHETIC_BATTERY_ENABLED 1
#endif

#ifndef PMU_ENABLED
#define PMU_ENABLED 1
#endif

#ifndef PMU_ADDR
#define PMU_ADDR 0x34
#endif

#ifndef BATTERY_FULL_VOLTAGE
#define BATTERY_FULL_VOLTAGE 4.20f
#endif

#ifndef BATTERY_EMPTY_VOLTAGE
#define BATTERY_EMPTY_VOLTAGE 3.30f
#endif

#ifndef BATTERY_WARN_PCT
#define BATTERY_WARN_PCT 20
#endif

#ifndef BATTERY_POLL_INTERVAL_MS
#define BATTERY_POLL_INTERVAL_MS 5000
#endif

#ifndef BATTERY_VOLTAGE_GAIN
#define BATTERY_VOLTAGE_GAIN 1.0f
#endif

#ifndef BATTERY_VOLTAGE_OFFSET
#define BATTERY_VOLTAGE_OFFSET 0.0f
#endif

#ifndef SENSOR_SLEEP_SECONDS
#define SENSOR_SLEEP_SECONDS 300
#endif

#ifndef SENSOR_PWR_PIN
#define SENSOR_PWR_PIN 13
#endif

#ifndef SENSOR_PWR_ACTIVE_HIGH
#define SENSOR_PWR_ACTIVE_HIGH 1
#endif

#ifndef SENSOR_PWR_WARMUP_MS
#define SENSOR_PWR_WARMUP_MS 1500
#endif

#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 21
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 22
#endif

#ifndef LEAK_DIGITAL_PIN
#define LEAK_DIGITAL_PIN 23
#endif

#ifndef LEAK_ACTIVE_LEVEL
#define LEAK_ACTIVE_LEVEL LOW
#endif

#ifndef AUX_UART_RX_PIN
#define AUX_UART_RX_PIN 16
#endif

#ifndef AUX_UART_TX_PIN
#define AUX_UART_TX_PIN 17
#endif

#ifndef GARAGE_DOOR_PIN
#define GARAGE_DOOR_PIN 32
#endif

#ifndef SIDE_DOOR_PIN
#define SIDE_DOOR_PIN 33
#endif

#ifndef MOTION_PIN
#define MOTION_PIN 25
#endif

#ifndef LIGHT_SENSE_PIN
#define LIGHT_SENSE_PIN 35
#endif

#ifndef BUZZER_PIN
#define BUZZER_PIN 4
#endif

#ifndef BUZZER_ACTIVE_HIGH
#define BUZZER_ACTIVE_HIGH 1
#endif

#ifndef BUZZER_FREQ_HZ
#define BUZZER_FREQ_HZ 2200
#endif

#ifndef BUZZER_ALERT_COOLDOWN_MS
#define BUZZER_ALERT_COOLDOWN_MS 15000
#endif

#ifndef OLED_ENABLED
#define OLED_ENABLED 1
#endif

#ifndef OLED_ADDR
#define OLED_ADDR 0x3C
#endif

#ifndef OLED_W
#define OLED_W 128
#endif

#ifndef OLED_H
#define OLED_H 64
#endif

#ifndef SCD41_LOCAL_ENABLED
#define SCD41_LOCAL_ENABLED 1
#endif

#ifndef SCD41_LOCAL_ADDR
#define SCD41_LOCAL_ADDR 0x62
#endif

#ifndef SHT3X_LOCAL_ENABLED
#define SHT3X_LOCAL_ENABLED 1
#endif

#ifndef SHT3X_LOCAL_ADDR
#define SHT3X_LOCAL_ADDR 0x44
#endif

#ifndef ALARM_SILENCE_PIN
#define ALARM_SILENCE_PIN 15
#endif

#ifndef ALARM_SILENCE_ACTIVE_LEVEL
#define ALARM_SILENCE_ACTIVE_LEVEL LOW
#endif

#ifndef ALARM_SILENCE_USE_PULLUP
#define ALARM_SILENCE_USE_PULLUP 1
#endif

#ifndef BOARD_LED_PIN
#define BOARD_LED_PIN 2
#endif

#ifndef BOARD_LED_ACTIVE_LOW
#define BOARD_LED_ACTIVE_LOW 1
#endif

#ifndef SECONDARY_LED_PIN
#define SECONDARY_LED_PIN -1
#endif

#ifndef SECONDARY_LED_ACTIVE_LOW
#define SECONDARY_LED_ACTIVE_LOW 1
#endif

#ifndef LORA_FREQ_MHZ
#define LORA_FREQ_MHZ 915.0
#endif

#ifndef LORA_BW_HZ
#define LORA_BW_HZ 125000.0
#endif

#ifndef LORA_SF
#define LORA_SF 9
#endif

#ifndef LORA_CR
#define LORA_CR 5
#endif

#ifndef LORA_TX_PWR
#define LORA_TX_PWR 17
#endif

#ifndef LORA_PIN_SS
#define LORA_PIN_SS 18
#endif

#ifndef LORA_PIN_RST
#define LORA_PIN_RST 14
#endif

#ifndef LORA_PIN_DIO0
#define LORA_PIN_DIO0 26
#endif

#ifndef IO4_IDLE_LEVEL
#define IO4_IDLE_LEVEL -1
#endif

static uint32_t seqNum = 1;
static uint32_t lastTxMs = 0;
static bool loraReady = false;
static uint32_t lastBuzzerAlertMs = 0;
static bool alarmSilenced = false;
static bool silenceBtnLastPressed = false;
static bool silenceBtnArmed = false;
static uint32_t silenceBtnDebounceMs = 0;
static float lastTempC = 0.0f;
static float lastRh = 0.0f;
static uint16_t lastCo2Ppm = 0;
static int lastLeakState = 0;
static bool leakLatchedForReport = false;
static bool localEnvPresent = false;
static bool localEnvValid = false;
static uint32_t lastLocalEnvPollMs = 0;
static bool localEnvHasCo2 = false;
static uint8_t localEnvType = 0; // 0=none, 1=scd41, 2=sht3x
static bool sensorPwrActiveHighRuntime = (SENSOR_PWR_ACTIVE_HIGH != 0);
static uint8_t lastScdProbeErr = 255;
static uint8_t lastShtProbeErr = 255;
static uint32_t lastRadioCheckMs = 0;
static bool serverAlarmActive = false;
static String serverAlarmMessage = "";
static uint32_t lastServerPollMs = 0;
static uint32_t lastActiveDiagMs = 0;
static bool pmuPresent = false;
static bool batteryValid = false;
static float batteryVbatt = NAN;
static float batteryVbattRaw = NAN;
static int batteryPct = -1;
static uint32_t lastBatteryPollMs = 0;
static bool localBatteryAlarmActive = false;
static String localBatteryAlarmMessage = "";

#if OLED_ENABLED
static Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
static bool oledReady = false;
static uint32_t lastOledUpdateMs = 0;
static uint8_t oledPage = 0;
#endif

void setSensorPower(bool on);

void applyIo4IdleLevel() {
  if (IO4_IDLE_LEVEL < 0 || BUZZER_PIN == 4) {
    return;
  }
  pinMode(4, OUTPUT);
  digitalWrite(4, IO4_IDLE_LEVEL ? HIGH : LOW);
}

int readActiveLowInput(int pin) {
  if (pin < 0) {
    return 0;
  }
  return digitalRead(pin) == LOW ? 1 : 0;
}

int readActiveHighInput(int pin) {
  if (pin < 0) {
    return 0;
  }
  return digitalRead(pin) == HIGH ? 1 : 0;
}

int readLightLevelRaw() {
  if (LIGHT_SENSE_PIN < 0) {
    return -1;
  }

  uint32_t sum = 0;
  const int samples = 8;
  for (int i = 0; i < samples; i++) {
    int value = analogRead(LIGHT_SENSE_PIN);
    if (value < 0) {
      value = 0;
    }
    sum += (uint32_t)value;
    delay(2);
  }
  return (int)(sum / (uint32_t)samples);
}

int readLeakState() {
  if (LEAK_DIGITAL_PIN < 0) {
    return 0;
  }
  int level = digitalRead(LEAK_DIGITAL_PIN);
  return (level == LEAK_ACTIVE_LEVEL) ? 1 : 0;
}

int batteryPercentFromVoltage(float vbatt) {
  if (isnan(vbatt) || BATTERY_FULL_VOLTAGE <= BATTERY_EMPTY_VOLTAGE) {
    return -1;
  }

  float pct = ((vbatt - BATTERY_EMPTY_VOLTAGE) / (BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE)) * 100.0f;
  int out = (int)(pct + (pct >= 0 ? 0.5f : -0.5f));
  if (out < 0) out = 0;
  if (out > 100) out = 100;
  return out;
}

bool pmuReadReg8(uint8_t reg, uint8_t& out) {
#if !PMU_ENABLED
  (void)reg;
  (void)out;
  return false;
#else
  Wire.beginTransmission(PMU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  uint8_t req = Wire.requestFrom((int)PMU_ADDR, 1);
  if (req != 1) {
    return false;
  }
  out = (uint8_t)Wire.read();
  return true;
#endif
}

bool pmuReadReg16(uint8_t regHi, uint8_t regLo, uint16_t& out) {
  uint8_t hi = 0;
  uint8_t lo = 0;
  if (!pmuReadReg8(regHi, hi) || !pmuReadReg8(regLo, lo)) {
    return false;
  }
  out = ((uint16_t)hi << 8) | lo;
  return true;
}

void initPmu() {
#if PMU_ENABLED
  Wire.beginTransmission(PMU_ADDR);
  uint8_t err = Wire.endTransmission();
  pmuPresent = (err == 0);
  if (pmuPresent) {
    Serial.printf("[PMU] Found at 0x%02X\n", PMU_ADDR);
  } else {
    Serial.printf("[PMU] Not found at 0x%02X (err=%u)\n", PMU_ADDR, err);
  }
#else
  pmuPresent = false;
#endif
}

void pollBatteryPmu() {
#if PMU_ENABLED
  if (!pmuPresent) {
    batteryValid = false;
    localBatteryAlarmActive = false;
    localBatteryAlarmMessage = "";
    return;
  }

  uint32_t now = millis();
  if (lastBatteryPollMs != 0 && (now - lastBatteryPollMs) < BATTERY_POLL_INTERVAL_MS) {
    return;
  }
  lastBatteryPollMs = now;

  uint16_t vbatRaw = 0;
  bool hasVbat = pmuReadReg16(0x78, 0x79, vbatRaw);
  if (!hasVbat) {
    batteryValid = false;
    localBatteryAlarmActive = false;
    localBatteryAlarmMessage = "";
    return;
  }

  batteryVbatt = ((float)(vbatRaw & 0x0FFF)) * 1.1f / 1000.0f;
  batteryVbattRaw = batteryVbatt;
  batteryVbatt = (batteryVbattRaw * BATTERY_VOLTAGE_GAIN) + BATTERY_VOLTAGE_OFFSET;
  batteryPct = batteryPercentFromVoltage(batteryVbatt);
  batteryValid = (batteryPct >= 0);

  if (batteryValid && batteryPct <= BATTERY_WARN_PCT) {
    localBatteryAlarmActive = true;
    localBatteryAlarmMessage = String("LOW BATT ") + String(batteryPct) + "% " + String(batteryVbatt, 2) + "V";
  } else {
    localBatteryAlarmActive = false;
    localBatteryAlarmMessage = "";
  }

  Serial.printf("[BATT] raw=%.3fV cal=%.3fV pct=%d warn<=%d active=%d\n",
                batteryVbattRaw,
                batteryVbatt,
                batteryPct,
                BATTERY_WARN_PCT,
                localBatteryAlarmActive ? 1 : 0);
#else
  batteryValid = false;
  localBatteryAlarmActive = false;
  localBatteryAlarmMessage = "";
#endif
}

void logActiveDiagnostics(const char* phase) {
#if ACTIVE_DIAG_ENABLED
  uint32_t now = millis();
  if ((now - lastActiveDiagMs) < ACTIVE_DIAG_INTERVAL_MS) {
    return;
  }
  lastActiveDiagMs = now;

  int leakRaw = (LEAK_DIGITAL_PIN >= 0) ? digitalRead(LEAK_DIGITAL_PIN) : -1;
  int garageRaw = (GARAGE_DOOR_PIN >= 0) ? digitalRead(GARAGE_DOOR_PIN) : -1;
  int sideRaw = (SIDE_DOOR_PIN >= 0) ? digitalRead(SIDE_DOOR_PIN) : -1;
  int motionRaw = (MOTION_PIN >= 0) ? digitalRead(MOTION_PIN) : -1;
  int silenceRaw = (ALARM_SILENCE_PIN >= 0) ? digitalRead(ALARM_SILENCE_PIN) : -1;

  Serial.printf("[ACTIVE] phase=%s ms=%lu leak(raw=%d,st=%d,lat=%d) door=%d side=%d motion=%d silence=%d heap=%u\n",
                phase,
                (unsigned long)now,
                leakRaw,
                lastLeakState,
                leakLatchedForReport ? 1 : 0,
                garageRaw,
                sideRaw,
                motionRaw,
                silenceRaw,
                (unsigned)ESP.getFreeHeap());
#else
  (void)phase;
#endif
}

void pollLeakInput() {
  int leakNow = readLeakState();
  lastLeakState = leakNow;
  if (leakNow == 1) {
    leakLatchedForReport = true;
  }
}

void initMonitorPins() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  if (LEAK_DIGITAL_PIN >= 0) {
    pinMode(LEAK_DIGITAL_PIN, INPUT_PULLUP);
  }
  if (GARAGE_DOOR_PIN >= 0) {
    pinMode(GARAGE_DOOR_PIN, INPUT_PULLUP);
  }
  if (SIDE_DOOR_PIN >= 0) {
    pinMode(SIDE_DOOR_PIN, INPUT_PULLUP);
  }
  if (MOTION_PIN >= 0) {
    pinMode(MOTION_PIN, INPUT_PULLUP);
  }
  if (LIGHT_SENSE_PIN >= 0) {
    pinMode(LIGHT_SENSE_PIN, INPUT);
  }
  if (BUZZER_PIN >= 0) {
    pinMode(BUZZER_PIN, OUTPUT);
#if BUZZER_ACTIVE_HIGH
    digitalWrite(BUZZER_PIN, LOW);
#else
    digitalWrite(BUZZER_PIN, HIGH);
#endif
  }
  if (ALARM_SILENCE_PIN >= 0) {
    if (ALARM_SILENCE_USE_PULLUP) {
      pinMode(ALARM_SILENCE_PIN, INPUT_PULLUP);
    } else {
      pinMode(ALARM_SILENCE_PIN, INPUT);
    }
  }

  Serial.printf("[PINS] I2C SDA=%d SCL=%d | leak=%d(active=%s) | garage=%d side=%d motion=%d (active=HIGH, fail-safe NC) | light_adc=%d | buzzer=%d(active=%s) | silence_btn=%d(active=%s,pullup=%d) | sensor_pwr=%d | aux_uart_rx=%d aux_uart_tx=%d\n",
                I2C_SDA_PIN,
                I2C_SCL_PIN,
                LEAK_DIGITAL_PIN,
                (LEAK_ACTIVE_LEVEL == LOW) ? "LOW" : "HIGH",
                GARAGE_DOOR_PIN,
                SIDE_DOOR_PIN,
                MOTION_PIN,
                LIGHT_SENSE_PIN,
                BUZZER_PIN,
                BUZZER_ACTIVE_HIGH ? "HIGH" : "LOW",
                ALARM_SILENCE_PIN,
                (ALARM_SILENCE_ACTIVE_LEVEL == LOW) ? "LOW" : "HIGH",
                ALARM_SILENCE_USE_PULLUP ? 1 : 0,
                SENSOR_PWR_PIN,
                AUX_UART_RX_PIN,
                AUX_UART_TX_PIN);
}

uint8_t scd41Crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x80) {
        crc = (uint8_t)((crc << 1) ^ 0x31);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

bool scd41WriteCmd(uint16_t cmd) {
  Wire.beginTransmission(SCD41_LOCAL_ADDR);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

bool scd41ReadBytes(uint8_t* out, size_t len) {
  uint8_t req = Wire.requestFrom((int)SCD41_LOCAL_ADDR, (int)len);
  if (req != len) {
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    out[i] = (uint8_t)Wire.read();
  }
  return true;
}

bool sht3xWriteCmd(uint16_t cmd) {
  Wire.beginTransmission(SHT3X_LOCAL_ADDR);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

bool sht3xReadSample(float& tC, float& rh) {
  if (!sht3xWriteCmd(0x2400)) {
    return false;
  }
  delay(20);

  uint8_t raw[6];
  uint8_t req = Wire.requestFrom((int)SHT3X_LOCAL_ADDR, 6);
  if (req != 6) {
    return false;
  }
  for (int i = 0; i < 6; i++) {
    raw[i] = (uint8_t)Wire.read();
  }

  if (scd41Crc8(&raw[0], 2) != raw[2] || scd41Crc8(&raw[3], 2) != raw[5]) {
    return false;
  }

  uint16_t tRaw = ((uint16_t)raw[0] << 8) | raw[1];
  uint16_t rhRaw = ((uint16_t)raw[3] << 8) | raw[4];
  tC = -45.0f + 175.0f * ((float)tRaw / 65535.0f);
  rh = 100.0f * ((float)rhRaw / 65535.0f);
  return true;
}

void initLocalEnvSensors() {
  localEnvPresent = false;
  localEnvValid = false;
  localEnvHasCo2 = false;
  localEnvType = 0;

  auto detectOnce = [&]() -> bool {
    localEnvPresent = false;
    localEnvHasCo2 = false;
    localEnvType = 0;

#if SCD41_LOCAL_ENABLED
    Wire.beginTransmission(SCD41_LOCAL_ADDR);
    lastScdProbeErr = Wire.endTransmission();
    if (lastScdProbeErr == 0) {
      localEnvPresent = true;
      localEnvHasCo2 = true;
      localEnvType = 1;
      if (scd41WriteCmd(0x21B1)) {
        Serial.printf("[LOCAL SCD41] Started periodic measurement at 0x%02X\n", SCD41_LOCAL_ADDR);
      } else {
        Serial.printf("[LOCAL SCD41] Failed start command at 0x%02X\n", SCD41_LOCAL_ADDR);
      }
      return true;
    }
#else
    lastScdProbeErr = 255;
#endif

#if SHT3X_LOCAL_ENABLED
    Wire.beginTransmission(SHT3X_LOCAL_ADDR);
    lastShtProbeErr = Wire.endTransmission();
    if (lastShtProbeErr == 0) {
      localEnvPresent = true;
      localEnvHasCo2 = false;
      localEnvType = 2;
      Serial.printf("[LOCAL SHT3X] Found at 0x%02X\n", SHT3X_LOCAL_ADDR);
      return true;
    }
#else
    lastShtProbeErr = 255;
#endif

    return false;
  };

  if (detectOnce()) {
    return;
  }

  if (SENSOR_PWR_PIN >= 0) {
    sensorPwrActiveHighRuntime = !sensorPwrActiveHighRuntime;
    Serial.printf("[PWR] Retrying sensor detect with SENSOR_PWR_ACTIVE_HIGH=%d\n", sensorPwrActiveHighRuntime ? 1 : 0);
    setSensorPower(true);
    delay(1200);
    if (detectOnce()) {
      Serial.printf("[PWR] Sensor rail polarity auto-detected: active_%s\n", sensorPwrActiveHighRuntime ? "HIGH" : "LOW");
      return;
    }
    sensorPwrActiveHighRuntime = !sensorPwrActiveHighRuntime;
    setSensorPower(true);
  }

  Serial.printf("[LOCAL ENV] No supported temp/RH sensor found (SCD41 err=%u, SHT3X err=%u), using fallback values\n", lastScdProbeErr, lastShtProbeErr);
}

void pollLocalEnvSensors() {
  if (!localEnvPresent) {
    return;
  }

  if ((millis() - lastLocalEnvPollMs) < 2000) {
    return;
  }
  lastLocalEnvPollMs = millis();

  if (localEnvType == 1) {
#if SCD41_LOCAL_ENABLED
    if (!scd41WriteCmd(0xE4B8)) {
      return;
    }
    delay(1);

    uint8_t readyRaw[3];
    if (!scd41ReadBytes(readyRaw, sizeof(readyRaw))) {
      return;
    }
    if (scd41Crc8(readyRaw, 2) != readyRaw[2]) {
      return;
    }

    uint16_t readyWord = ((uint16_t)readyRaw[0] << 8) | readyRaw[1];
    if ((readyWord & 0x07FF) == 0) {
      return;
    }

    if (!scd41WriteCmd(0xEC05)) {
      return;
    }
    delay(1);

    uint8_t m[9];
    if (!scd41ReadBytes(m, sizeof(m))) {
      return;
    }
    if (scd41Crc8(&m[0], 2) != m[2] || scd41Crc8(&m[3], 2) != m[5] || scd41Crc8(&m[6], 2) != m[8]) {
      return;
    }

    uint16_t co2 = ((uint16_t)m[0] << 8) | m[1];
    uint16_t tRaw = ((uint16_t)m[3] << 8) | m[4];
    uint16_t rhRaw = ((uint16_t)m[6] << 8) | m[7];

    lastTempC = -45.0f + 175.0f * ((float)tRaw / 65535.0f);
    lastRh = 100.0f * ((float)rhRaw / 65535.0f);
    lastCo2Ppm = co2;
    localEnvValid = true;
#endif
  } else if (localEnvType == 2) {
#if SHT3X_LOCAL_ENABLED
    float tC = 0.0f;
    float rh = 0.0f;
    if (!sht3xReadSample(tC, rh)) {
      return;
    }
    lastTempC = tC;
    lastRh = rh;
    localEnvValid = true;
#endif
  }
}

void refreshDerivedReadings() {
  pollLocalEnvSensors();
  pollBatteryPmu();
  if (!localEnvValid) {
    lastTempC = 21.5f + ((millis() / 10000) % 20) * 0.1f;
    lastRh = 43.0f + ((millis() / 8000) % 15) * 0.2f;
    lastCo2Ppm = (uint16_t)(650 + ((millis() / 1000) % 120));
  }
  pollLeakInput();
}

#if OLED_ENABLED
void initOled() {
  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!oledReady) {
    Serial.printf("[OLED] init failed addr=0x%02X\n", OLED_ADDR);
    return;
  }

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Control Node OLED");
  oled.display();
  Serial.printf("[OLED] ready addr=0x%02X\n", OLED_ADDR);
}

void oledShowCenterLine(const char* header, const String& value) {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(header);
  oled.setTextSize(2);
  oled.setCursor(0, 24);
  oled.print(value);
  oled.display();
}

void updateOled() {
  if (!oledReady) {
    return;
  }

  bool alarmActive = (lastLeakState == 1) || serverAlarmActive || localBatteryAlarmActive;
  if (alarmActive) {
    String alarmLine;
    String detailLine;
    if (lastLeakState == 1) {
      alarmLine = alarmSilenced ? "ALARM: LEAK (MUTED)" : "ALARM: LEAK";
      detailLine = "Leak input active";
    } else if (localBatteryAlarmActive) {
      alarmLine = alarmSilenced ? "ALARM: LOW BATT (MUTED)" : "ALARM: LOW BATT";
      detailLine = localBatteryAlarmMessage;
    } else {
      alarmLine = alarmSilenced ? "ALARM: SERVER (MUTED)" : "ALARM: SERVER";
      detailLine = serverAlarmMessage;
    }
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print(alarmLine);
    oled.setTextSize(1);
    oled.setCursor(0, 20);
    oled.print("Temp ");
    oled.print(lastTempC, 1);
    oled.print(" C");
    oled.setCursor(0, 32);
    oled.print("RH   ");
    oled.print(lastRh, 1);
    oled.print(" %");
    oled.setCursor(0, 44);
    oled.print("CO2  ");
    oled.print((int)lastCo2Ppm);
    oled.print(" ppm");
    if (detailLine.length() > 0) {
      oled.setCursor(0, 56);
      oled.print(detailLine.substring(0, 20));
    }
    oled.display();
    return;
  }

  if ((millis() - lastOledUpdateMs) < 2000) {
    return;
  }
  lastOledUpdateMs = millis();

  if (oledPage == 0) {
    oledShowCenterLine("TEMP", String(lastTempC, 1) + " C");
  } else if (oledPage == 1) {
    oledShowCenterLine("HUMID", String(lastRh, 1) + " %");
  } else {
    if (localEnvHasCo2) {
      oledShowCenterLine("CO2", String((int)lastCo2Ppm) + " ppm");
    } else {
      oledShowCenterLine("CO2", "N/A");
    }
  }
  oledPage = (uint8_t)((oledPage + 1) % 3);
}
#endif

bool isSilenceButtonPressed() {
  if (ALARM_SILENCE_PIN < 0) {
    return false;
  }
  return digitalRead(ALARM_SILENCE_PIN) == ALARM_SILENCE_ACTIVE_LEVEL;
}

void pollSilenceButton(bool alarmNow) {
  bool pressedRaw = isSilenceButtonPressed();
  uint32_t now = millis();

  if (!pressedRaw) {
    silenceBtnArmed = true;
  }

  if (pressedRaw != silenceBtnLastPressed && (now - silenceBtnDebounceMs) >= 30) {
    silenceBtnDebounceMs = now;
    silenceBtnLastPressed = pressedRaw;
    if (pressedRaw && alarmNow && silenceBtnArmed) {
      alarmSilenced = true;
      Serial.println("[ALARM] Silence button pressed: buzzer muted until alarm clears");
    }
  }

  if (!alarmNow && alarmSilenced) {
    alarmSilenced = false;
    Serial.println("[ALARM] Alarm cleared: buzzer silence reset");
  }
}

void setBuzzer(bool on) {
  if (BUZZER_PIN < 0) {
    return;
  }
  int level = on ? HIGH : LOW;
#if !BUZZER_ACTIVE_HIGH
  level = on ? LOW : HIGH;
#endif
  digitalWrite(BUZZER_PIN, level);
}

void buzzerToneMs(uint16_t freqHz, uint16_t durationMs) {
  if (BUZZER_PIN < 0 || durationMs == 0) {
    return;
  }

  if (freqHz < 100) {
    setBuzzer(true);
    delay(durationMs);
    setBuzzer(false);
    return;
  }

  uint32_t periodUs = 1000000UL / (uint32_t)freqHz;
  uint32_t halfUs = periodUs / 2UL;
  if (halfUs == 0) {
    halfUs = 1;
  }

  uint32_t cycles = ((uint32_t)durationMs * 1000UL) / periodUs;
  if (cycles == 0) {
    cycles = 1;
  }

  for (uint32_t i = 0; i < cycles; i++) {
    setBuzzer(true);
    delayMicroseconds((unsigned int)halfUs);
    setBuzzer(false);
    delayMicroseconds((unsigned int)halfUs);
  }
}

void playLeakAlertPattern() {
  buzzerToneMs(BUZZER_FREQ_HZ, 120);
  delay(70);
  buzzerToneMs(BUZZER_FREQ_HZ, 120);
  delay(70);
  buzzerToneMs(BUZZER_FREQ_HZ, 200);
}

void maybeSoundLeakAlert(int leak) {
  if (BUZZER_PIN < 0 || leak == 0 || alarmSilenced) {
    return;
  }

#if LOW_POWER_MODE
  playLeakAlertPattern();
#else
  if ((millis() - lastBuzzerAlertMs) >= BUZZER_ALERT_COOLDOWN_MS) {
    lastBuzzerAlertMs = millis();
    playLeakAlertPattern();
  }
#endif
}

bool connectWifiIfNeeded() {
#if !CONTROL_WIFI_DIRECT
  return false;
#else
  if (strlen(WIFI_SSID) == 0) {
    return false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 8000) {
    delay(150);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] Connected: %s RSSI=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }

  Serial.printf("[WIFI] Not connected (status=%d)\n", (int)WiFi.status());
  return false;
#endif
}

int postJsonToServer(const char* path, const String& body) {
#if !CONTROL_WIFI_DIRECT
  (void)path;
  (void)body;
  return -1;
#else
  if (!connectWifiIfNeeded()) {
    return -2;
  }

  HTTPClient http;
  String url = String(SERVER_BASE_URL) + path;
  if (!http.begin(url)) {
    return -3;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t*)body.c_str(), body.length());
  http.end();
  return code;
#endif
}

void postControlPayloadToServer(const String& payload) {
#if CONTROL_WIFI_DIRECT
  DynamicJsonDocument payloadDoc(512);
  if (deserializeJson(payloadDoc, payload) != DeserializationError::Ok) {
    Serial.println("[NET] payload json parse failed");
    return;
  }

  DynamicJsonDocument bodyDoc(768);
  bodyDoc["gateway_id"] = "control-direct";
  bodyDoc["node_id"] = payloadDoc["id"] | NODE_ID;
  bodyDoc["rx_ts"] = (uint32_t)(millis() / 1000UL);
  bodyDoc["payload"] = payloadDoc.as<JsonObject>();

  String body;
  serializeJson(bodyDoc, body);
  int code = postJsonToServer("/api/lora", body);
  Serial.printf("[NET] POST /api/lora code=%d\n", code);
#else
  (void)payload;
#endif
}

void forwardReceivedLoraPayloadToServer(const String& payload, int rssi, float snr) {
#if CONTROL_WIFI_DIRECT
  DynamicJsonDocument payloadDoc(512);
  if (deserializeJson(payloadDoc, payload) != DeserializationError::Ok) {
    Serial.println("[LORA] RX payload json parse failed");
    return;
  }

  if (!payloadDoc["id"].is<const char*>()) {
    Serial.println("[LORA] RX missing payload.id, dropped");
    return;
  }

  DynamicJsonDocument bodyDoc(768);
  bodyDoc["gateway_id"] = NODE_ID;
  bodyDoc["node_id"] = payloadDoc["id"] | "unknown";
  bodyDoc["rx_ts"] = (uint32_t)(millis() / 1000UL);
  bodyDoc["rssi"] = rssi;
  bodyDoc["snr"] = snr;
  bodyDoc["payload"] = payloadDoc.as<JsonObject>();

  String body;
  serializeJson(bodyDoc, body);
  int code = postJsonToServer("/api/lora", body);
  Serial.printf("[NET] FWD /api/lora id=%s code=%d\n", (const char*)(payloadDoc["id"] | "unknown"), code);
#else
  (void)payload;
  (void)rssi;
  (void)snr;
#endif
}

bool shouldTriggerServerAlarm(const char* type, const char* severity) {
  if (type == nullptr || severity == nullptr) {
    return false;
  }

  bool validSeverity = (strcmp(severity, "critical") == 0 || strcmp(severity, "warning") == 0);
  if (!validSeverity) {
    return false;
  }

  return strcmp(type, "leak") == 0 || strcmp(type, "garage") == 0 || strcmp(type, "side_door") == 0 || strcmp(type, "motion") == 0 || strcmp(type, "battery") == 0;
}

void pollServerAlarmState() {
#if CONTROL_WIFI_DIRECT
  uint32_t now = millis();
  if ((now - lastServerPollMs) < 5000) {
    return;
  }
  lastServerPollMs = now;

  serverAlarmActive = false;
  serverAlarmMessage = "";

  if (!connectWifiIfNeeded()) {
    return;
  }

  HTTPClient http;
  String url = String(SERVER_BASE_URL) + "/monitor_data";
  if (!http.begin(url)) {
    return;
  }

  int code = http.GET();
  if (code != 200) {
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  DynamicJsonDocument monitorDoc(3072);
  if (deserializeJson(monitorDoc, body) != DeserializationError::Ok) {
    return;
  }

  JsonArray alerts = monitorDoc["alerts"].as<JsonArray>();
  for (JsonObject alert : alerts) {
    const char* type = alert["type"] | "";
    const char* severity = alert["severity"] | "";
    if (shouldTriggerServerAlarm(type, severity)) {
      serverAlarmActive = true;
      serverAlarmMessage = String(alert["message"] | "SERVER ALARM");
      break;
    }
  }

  if (serverAlarmActive) {
    Serial.printf("[ALARM] Server alarm active: %s\n", serverAlarmMessage.c_str());
  }
#endif
}

void disableUnusedRadios() {
#if CONTROL_WIFI_DIRECT
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  btStop();
  delay(20);
  Serial.printf("[RADIO] Control direct mode: WiFi=%d BT status=%d\n", (int)WiFi.getMode(), (int)esp_bt_controller_get_status());
#else
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  esp_err_t wifiNullMode = esp_wifi_set_mode(WIFI_MODE_NULL);
  esp_err_t wifiStop = esp_wifi_stop();
  btStop();
  delay(20);
  wifi_mode_t lowLevelMode = WIFI_MODE_NULL;
  esp_err_t wifiGetMode = esp_wifi_get_mode(&lowLevelMode);
  Serial.printf("[RADIO] WiFi Arduino=%d ESP-IDF=%d (set_null=%d stop=%d get_mode=%d) BT status=%d\n",
                (int)WiFi.getMode(),
                (int)lowLevelMode,
                (int)wifiNullMode,
                (int)wifiStop,
                (int)wifiGetMode,
                (int)esp_bt_controller_get_status());
#endif
}

void verifyRadiosRemainOff(const char* phase) {
#if CONTROL_WIFI_DIRECT
  (void)phase;
  if (WiFi.getMode() != WIFI_STA) {
    WiFi.mode(WIFI_STA);
  }
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
    btStop();
  }
  return;
#else
  bool wifiOff = (WiFi.getMode() == WIFI_OFF);
  bool btOff = (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE);
  if (!wifiOff || !btOff) {
    Serial.printf("[RADIO] Unexpected active state at %s -> forcing off (wifi=%d bt=%d)\n",
                  phase,
                  (int)WiFi.getMode(),
                  (int)esp_bt_controller_get_status());
    disableUnusedRadios();
    return;
  }

  uint32_t now = millis();
  if ((now - lastRadioCheckMs) >= 10000) {
    lastRadioCheckMs = now;
    Serial.printf("[RADIO] Verified off at %s (wifi=%d bt=%d)\n",
                  phase,
                  (int)WiFi.getMode(),
                  (int)esp_bt_controller_get_status());
  }
#endif
}

void setBoardLed(bool on) {
  if (BOARD_LED_PIN < 0) {
    return;
  }
  pinMode(BOARD_LED_PIN, OUTPUT);
  int level = on ? HIGH : LOW;
#if BOARD_LED_ACTIVE_LOW
  level = on ? LOW : HIGH;
#endif
  digitalWrite(BOARD_LED_PIN, level);
}

void setSecondaryLed(bool on) {
  if (SECONDARY_LED_PIN < 0) {
    return;
  }
  pinMode(SECONDARY_LED_PIN, OUTPUT);
  int level = on ? HIGH : LOW;
#if SECONDARY_LED_ACTIVE_LOW
  level = on ? LOW : HIGH;
#endif
  digitalWrite(SECONDARY_LED_PIN, level);
}

void forceKnownLedsOff() {
  setBoardLed(false);
  setSecondaryLed(false);
}

void setAwakeIndicators() {
  setBoardLed(true);
  setSecondaryLed(false);
}

void setSensorPower(bool on) {
  if (SENSOR_PWR_PIN < 0) {
    return;
  }
  pinMode(SENSOR_PWR_PIN, OUTPUT);
  int level = on ? HIGH : LOW;
  if (!sensorPwrActiveHighRuntime) {
    level = on ? LOW : HIGH;
  }
  digitalWrite(SENSOR_PWR_PIN, level);
}

void enterDeepSleep() {
#if LOW_POWER_MODE
  Serial.printf("[PWR] Deep sleep for %u s\n", (unsigned)SENSOR_SLEEP_SECONDS);
  delay(50);
  esp_sleep_enable_timer_wakeup((uint64_t)SENSOR_SLEEP_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
#endif
}

struct LoraPinProfile {
  int8_t ss;
  int8_t rst;
  int8_t dio0;
  int8_t sck;
  int8_t miso;
  int8_t mosi;
};

void applyLoraRadioConfig() {
  LoRa.setSignalBandwidth(LORA_BW_HZ);
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setTxPower(LORA_TX_PWR);
  LoRa.enableCrc();
}

bool initLora() {
  const LoraPinProfile profiles[] = {
    {(int8_t)LORA_PIN_SS, (int8_t)LORA_PIN_RST, (int8_t)LORA_PIN_DIO0, 5, 19, 27},
    {(int8_t)LORA_PIN_SS, -1, (int8_t)LORA_PIN_DIO0, 5, 19, 27},
  };

  for (size_t i = 0; i < (sizeof(profiles) / sizeof(profiles[0])); i++) {
    const LoraPinProfile& profile = profiles[i];
    SPI.begin(profile.sck, profile.miso, profile.mosi, profile.ss);
    LoRa.setPins(profile.ss, profile.rst, profile.dio0);
    if (LoRa.begin(LORA_FREQ_MHZ * 1000000.0)) {
      applyLoraRadioConfig();
      Serial.printf("[LORA] TX ready pins ss=%d rst=%d dio0=%d sck=%d miso=%d mosi=%d freq=%.3fMHz\n",
                    profile.ss,
                    profile.rst,
                    profile.dio0,
                    profile.sck,
                    profile.miso,
                    profile.mosi,
                    (double)LORA_FREQ_MHZ);
      return true;
    }
    Serial.printf("[LORA] begin failed pins ss=%d rst=%d dio0=%d sck=%d miso=%d mosi=%d\n",
                  profile.ss,
                  profile.rst,
                  profile.dio0,
                  profile.sck,
                  profile.miso,
                  profile.mosi);
    delay(80);
  }

  Serial.println("[LORA] begin() failed for all tested pin profiles");
  return false;
}

String buildPayload() {
  StaticJsonDocument<256> doc;
  doc["v"] = 1;
  doc["id"] = NODE_ID;
  doc["seq"] = seqNum++;
  refreshDerivedReadings();

  doc["t_c"] = lastTempC;
  doc["rh"] = lastRh;
  if (localEnvHasCo2) {
    doc["co2_ppm"] = lastCo2Ppm;
  }

  int leak = (lastLeakState == 1 || leakLatchedForReport) ? 1 : 0;
  pollSilenceButton((leak == 1) || serverAlarmActive);
  doc["leak"] = leak;
  doc["wet"] = leak ? 3200 : 120;
  doc["garage_open"] = readActiveHighInput(GARAGE_DOOR_PIN);
  doc["side_open"] = readActiveHighInput(SIDE_DOOR_PIN);
  doc["motion"] = readActiveHighInput(MOTION_PIN);
  int lightLevel = readLightLevelRaw();
  if (lightLevel >= 0) {
    doc["light_level"] = lightLevel;
  }
  doc["alarm_silenced"] = alarmSilenced ? 1 : 0;

  if (batteryValid) {
    doc["vbatt"] = batteryVbatt;
    doc["vbatt_raw"] = batteryVbattRaw;
    doc["batt_pct"] = batteryPct;
  } else {
#if SYNTHETIC_BATTERY_ENABLED
    float vbatt = 3.95f - (((millis() / 30000) % 30) * 0.01f);
    if (vbatt < 3.30f) {
      vbatt = 3.30f;
    }
    doc["vbatt"] = vbatt;

    int battPct = batteryPercentFromVoltage(vbatt);
    if (battPct >= 0) {
      doc["batt_pct"] = battPct;
    }
#endif
  }

  String out;
  serializeJson(doc, out);
  if (leakLatchedForReport && lastLeakState == 0) {
    leakLatchedForReport = false;
  }
  return out;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("SENSOR_DUMMY_START");

  initMonitorPins();
  initPmu();
  applyIo4IdleLevel();

  setSensorPower(true);
  if (SENSOR_PWR_WARMUP_MS > 0) {
    Serial.printf("[PWR] Sensor rail ON (pin=%d), warmup %u ms\n", SENSOR_PWR_PIN, (unsigned)SENSOR_PWR_WARMUP_MS);
    delay(SENSOR_PWR_WARMUP_MS);
  }

  initLocalEnvSensors();
  refreshDerivedReadings();
#if OLED_ENABLED
  initOled();
  updateOled();
#endif

  disableUnusedRadios();
  setAwakeIndicators();

#if CONTROL_WIFI_DIRECT
  loraReady = initLora();
  connectWifiIfNeeded();
#else
  loraReady = initLora();
#endif

#if LOW_POWER_MODE
  if (!loraReady) {
    setSensorPower(false);
    applyIo4IdleLevel();
    forceKnownLedsOff();
    enterDeepSleep();
  }
#endif
}

void loop() {
#if CONTROL_WIFI_DIRECT
  verifyRadiosRemainOff("control-loop");
  pollLeakInput();
  refreshDerivedReadings();
  logActiveDiagnostics("control-loop");
  pollServerAlarmState();

  if (loraReady) {
    int packetSize = LoRa.parsePacket();
    if (packetSize > 0) {
      String rxPayload;
      while (LoRa.available()) {
        rxPayload += (char)LoRa.read();
      }

      int rssi = LoRa.packetRssi();
      float snr = LoRa.packetSnr();
      Serial.printf("[LORA] RX bytes=%d rssi=%d snr=%.2f payload=%s\n", packetSize, rssi, snr, rxPayload.c_str());
      forwardReceivedLoraPayloadToServer(rxPayload, rssi, snr);
    }
  }

  bool alarmNow = (lastLeakState == 1) || serverAlarmActive || localBatteryAlarmActive;
  pollSilenceButton(alarmNow);
  maybeSoundLeakAlert(alarmNow ? 1 : 0);

#if OLED_ENABLED
  updateOled();
#endif

  if ((millis() - lastTxMs) >= SENSOR_TX_INTERVAL_MS) {
    lastTxMs = millis();
    String payload = buildPayload();
    postControlPayloadToServer(payload);
    Serial.printf("[CTRL] Sent payload=%s\n", payload.c_str());
  }

  delay(20);
  return;
#endif

  if (!loraReady) {
    delay(200);
    return;
  }

#if LOW_POWER_MODE
  setAwakeIndicators();
  verifyRadiosRemainOff("wake");
  refreshDerivedReadings();
  logActiveDiagnostics("wake");
  int leakNow = lastLeakState;
  bool alarmNow = (leakNow == 1) || localBatteryAlarmActive;
  pollSilenceButton(alarmNow);
  maybeSoundLeakAlert(alarmNow ? 1 : 0);
#if OLED_ENABLED
  updateOled();
#endif
  String payload = buildPayload();
  LoRa.beginPacket();
  LoRa.print(payload);
  int state = LoRa.endPacket();
  Serial.printf("[LORA] TX state=%d payload=%s\n", state, payload.c_str());

  LoRa.sleep();
  setSensorPower(false);
  applyIo4IdleLevel();
  Serial.printf("[PWR] Pre-sleep GPIO4=%d\n", digitalRead(4));
  forceKnownLedsOff();
  enterDeepSleep();
  delay(1000);
  return;
#else
  verifyRadiosRemainOff("loop");
  pollLeakInput();
  logActiveDiagnostics("loop");
  if ((millis() - lastTxMs) >= SENSOR_TX_INTERVAL_MS) {
    lastTxMs = millis();

    refreshDerivedReadings();
    int leakNow = lastLeakState;
    bool alarmNow = (leakNow == 1) || localBatteryAlarmActive;
    pollSilenceButton(alarmNow);
    maybeSoundLeakAlert(alarmNow ? 1 : 0);

#if OLED_ENABLED
    updateOled();
#endif

    String payload = buildPayload();

    LoRa.beginPacket();
    LoRa.print(payload);
    int state = LoRa.endPacket();

    Serial.printf("[LORA] TX state=%d payload=%s\n", state, payload.c_str());
  }

  delay(10);

#if OLED_ENABLED
  refreshDerivedReadings();
  updateOled();
#endif
#endif
}
