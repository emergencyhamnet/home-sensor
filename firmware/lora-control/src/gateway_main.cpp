#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SPI.h>

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

#ifndef SERVER_BASE_URL
#define SERVER_BASE_URL "http://192.168.1.49:8080"
#endif

#ifndef GATEWAY_ID
#define GATEWAY_ID "gw-main"
#endif

#ifndef NODE_ID
#define NODE_ID "base"
#endif

#ifndef DUMMY_NODE_ID
#define DUMMY_NODE_ID "laundry"
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

#ifndef CM1107_ENABLED
#define CM1107_ENABLED 1
#endif

#ifndef CM1107_BAUD
#define CM1107_BAUD 9600
#endif

#ifndef CM1107_RX_PIN
#define CM1107_RX_PIN 16
#endif

#ifndef CM1107_TX_PIN
#define CM1107_TX_PIN 17
#endif

#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 21
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 22
#endif

#ifndef PMU_ENABLED
#define PMU_ENABLED 1
#endif

#ifndef PMU_ADDR
#define PMU_ADDR 0x34
#endif

#ifndef PMU_VBAT_GAIN
#define PMU_VBAT_GAIN 1.0f
#endif

#ifndef PMU_VBAT_OFFSET
#define PMU_VBAT_OFFSET 0.0f
#endif

#ifndef SCD41_ENABLED
#define SCD41_ENABLED 1
#endif

#ifndef SCD41_ADDR
#define SCD41_ADDR 0x62
#endif

static HardwareSerial cm1107Serial(2);

struct PmuReading {
  bool present;
  bool valid;
  float vbatRawV;
  float vbatV;
  float vbusV;
  float ichgMa;
  float idisMa;
  uint32_t rxMs;
};

struct Cm1107Reading {
  bool valid;
  uint16_t co2ppm;
  uint16_t tempRaw;
  uint16_t rhRaw;
  uint16_t abcDays;
  uint16_t status;
  uint16_t checksumRx;
  uint16_t checksumCalc;
  uint32_t rxMs;
};

struct Scd41Reading {
  bool present;
  bool valid;
  uint16_t co2ppm;
  float tC;
  float rh;
  uint32_t rxMs;
};

struct LoraPinProfile {
  int8_t ss;
  int8_t rst;
  int8_t dio0;
  int8_t sck;
  int8_t miso;
  int8_t mosi;
};

static uint32_t lastBasePostMs = 0;
static uint32_t lastDummyLoraMs = 0;
static bool loraReady = false;
static bool pmuPresent = false;
static bool scd41Present = false;
static uint8_t cmFrameBuf[16];
static uint8_t cmFramePos = 0;
static Cm1107Reading cmLast = {false, 0, 0, 0, 0, 0, 0, 0, 0};
static PmuReading pmuLast = {false, false, 0, 0, 0, 0, 0, 0};
static Scd41Reading scdLast = {false, false, 0, 0.0f, 0.0f, 0};
static uint32_t lastScdPollMs = 0;

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
  Wire.beginTransmission(SCD41_ADDR);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

bool scd41ReadBytes(uint8_t* out, size_t len) {
  uint8_t req = Wire.requestFrom((int)SCD41_ADDR, (int)len);
  if (req != len) {
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    out[i] = (uint8_t)Wire.read();
  }
  return true;
}

void initScd41() {
#if SCD41_ENABLED
  Wire.beginTransmission(SCD41_ADDR);
  uint8_t err = Wire.endTransmission();
  scd41Present = (err == 0);
  if (!scd41Present) {
    Serial.printf("[SCD41] Not found at 0x%02X (err=%u)\n", SCD41_ADDR, err);
    return;
  }

  if (scd41WriteCmd(0x21B1)) {
    Serial.printf("[SCD41] Started periodic measurement at 0x%02X\n", SCD41_ADDR);
  } else {
    Serial.printf("[SCD41] Failed to start periodic measurement at 0x%02X\n", SCD41_ADDR);
  }
#endif
}

void pollScd41() {
#if SCD41_ENABLED
  if (!scd41Present) {
    return;
  }

  if ((millis() - lastScdPollMs) < 2000) {
    return;
  }
  lastScdPollMs = millis();

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
  bool dataReady = (readyWord & 0x07FF) != 0;
  if (!dataReady) {
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

  float tC = -45.0f + 175.0f * ((float)tRaw / 65535.0f);
  float rh = 100.0f * ((float)rhRaw / 65535.0f);

  scdLast.present = true;
  scdLast.valid = true;
  scdLast.co2ppm = co2;
  scdLast.tC = tC;
  scdLast.rh = rh;
  scdLast.rxMs = millis();

  Serial.printf("[SCD41] CO2=%uppm t=%.2fC rh=%.1f%%\n", co2, tC, rh);
#endif
}

bool pmuReadReg8(uint8_t reg, uint8_t& out) {
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
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.beginTransmission(PMU_ADDR);
  uint8_t err = Wire.endTransmission();
  pmuPresent = (err == 0);
  if (pmuPresent) {
    Serial.printf("[PMU] Found at 0x%02X (SDA=%d SCL=%d)\n", PMU_ADDR, I2C_SDA_PIN, I2C_SCL_PIN);
  } else {
    Serial.printf("[PMU] Not found at 0x%02X (err=%u)\n", PMU_ADDR, err);
  }
#endif
}

void pollPmu() {
#if PMU_ENABLED
  if (!pmuPresent) {
    return;
  }

  uint16_t vbatRaw = 0;
  uint16_t vbusRaw = 0;
  uint16_t ichgRaw = 0;
  uint16_t idisRaw = 0;

  bool hasVbat = pmuReadReg16(0x78, 0x79, vbatRaw);
  bool hasVbus = pmuReadReg16(0x5A, 0x5B, vbusRaw);
  bool hasIchg = pmuReadReg16(0x7A, 0x7B, ichgRaw);
  bool hasIdis = pmuReadReg16(0x7C, 0x7D, idisRaw);

  if (!(hasVbat || hasVbus || hasIchg || hasIdis)) {
    pmuLast.valid = false;
    return;
  }

  pmuLast.present = true;
  pmuLast.valid = true;
  pmuLast.vbatRawV = hasVbat ? (((float)(vbatRaw & 0x0FFF)) * 1.1f / 1000.0f) : NAN;
  pmuLast.vbatV = hasVbat ? ((pmuLast.vbatRawV * PMU_VBAT_GAIN) + PMU_VBAT_OFFSET) : NAN;
  pmuLast.vbusV = hasVbus ? (((float)(vbusRaw & 0x0FFF)) * 1.7f / 1000.0f) : NAN;
  pmuLast.ichgMa = hasIchg ? (((float)(ichgRaw & 0x0FFF)) * 0.5f) : NAN;
  pmuLast.idisMa = hasIdis ? (((float)(idisRaw & 0x1FFF)) * 0.5f) : NAN;
  pmuLast.rxMs = millis();
  if (hasVbat) {
    Serial.printf("[PMU] VBAT raw=%.3fV cal=%.3fV (gain=%.3f offset=%.3f)\n",
                  pmuLast.vbatRawV,
                  pmuLast.vbatV,
                  PMU_VBAT_GAIN,
                  PMU_VBAT_OFFSET);
  }
#endif
}

uint16_t cm1107CalcChecksum(const uint8_t* frame) {
  uint32_t sum = 0;
  for (int i = 0; i <= 13; i++) {
    sum += frame[i];
  }
  return (uint16_t)((sum + 0x0406) & 0xFFFF);
}

bool parseCm1107Frame(const uint8_t* frame, Cm1107Reading& out) {
  if (frame[0] != 0x42 || frame[1] != 0x4D) {
    return false;
  }

  out.co2ppm = ((uint16_t)frame[2] << 8) | frame[3];
  out.tempRaw = ((uint16_t)frame[4] << 8) | frame[5];
  out.rhRaw = ((uint16_t)frame[6] << 8) | frame[7];
  out.abcDays = ((uint16_t)frame[8] << 8) | frame[9];
  out.status = ((uint16_t)frame[10] << 8) | frame[11];
  out.checksumRx = ((uint16_t)frame[14] << 8) | frame[15];
  out.checksumCalc = cm1107CalcChecksum(frame);
  out.valid = (out.checksumRx == out.checksumCalc);
  out.rxMs = millis();
  return true;
}

void initCm1107() {
#if CM1107_ENABLED
  cm1107Serial.begin(CM1107_BAUD, SERIAL_8N1, CM1107_RX_PIN, CM1107_TX_PIN);
  Serial.printf("[CM1107] UART ready baud=%d rx=%d tx=%d\n", CM1107_BAUD, CM1107_RX_PIN, CM1107_TX_PIN);
#endif
}

void pollCm1107() {
#if CM1107_ENABLED
  while (cm1107Serial.available() > 0) {
    uint8_t b = (uint8_t)cm1107Serial.read();

    if (cmFramePos == 0) {
      if (b == 0x42) {
        cmFrameBuf[cmFramePos++] = b;
      }
      continue;
    }

    if (cmFramePos == 1) {
      if (b == 0x4D) {
        cmFrameBuf[cmFramePos++] = b;
      } else {
        cmFramePos = (b == 0x42) ? 1 : 0;
        if (cmFramePos == 1) {
          cmFrameBuf[0] = 0x42;
        }
      }
      continue;
    }

    cmFrameBuf[cmFramePos++] = b;

    if (cmFramePos >= sizeof(cmFrameBuf)) {
      Cm1107Reading parsed = {false, 0, 0, 0, 0, 0, 0, 0, 0};
      if (parseCm1107Frame(cmFrameBuf, parsed)) {
        cmLast = parsed;
        if (parsed.valid) {
          Serial.printf("[CM1107] CO2=%uppm tempRaw=%u rhRaw=%u abcDays=%u status=%u\n",
                        parsed.co2ppm,
                        parsed.tempRaw,
                        parsed.rhRaw,
                        parsed.abcDays,
                        parsed.status);
        } else {
          Serial.printf("[CM1107] checksum mismatch rx=0x%04X calc=0x%04X\n",
                        parsed.checksumRx,
                        parsed.checksumCalc);
        }
      }
      cmFramePos = 0;
    }
  }
#endif
}

bool connectWiFi() {
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("[WIFI] WIFI_SSID is empty; gateway HTTP forward disabled.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WIFI] Connecting to '%s'", WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 20000) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[WIFI] Connect failed (status=%d)\n", WiFi.status());
    return false;
  }

  Serial.printf("[WIFI] Connected IP=%s RSSI=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

bool postJson(const String& path, const String& jsonBody) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  String url = String(SERVER_BASE_URL) + path;
  http.begin(url);
  http.setTimeout(4000);
  http.addHeader("Content-Type", "application/json");
  Serial.printf("[HTTP] POST start %s\n", path.c_str());
  int code = http.POST((uint8_t*)jsonBody.c_str(), jsonBody.length());
  String resp = http.getString();
  http.end();

  Serial.printf("[HTTP] POST %s -> %d %s\n", path.c_str(), code, resp.c_str());
  return code >= 200 && code < 300;
}

void postBaseDummy() {
  bool cmFresh = false;
  uint16_t cmCo2 = 0;
  bool scdFresh = false;
  uint16_t scdCo2 = 0;
#if CM1107_ENABLED
  cmFresh = cmLast.valid && (millis() - cmLast.rxMs) <= 5000;
  if (cmFresh) {
    cmCo2 = cmLast.co2ppm;
  }
#endif

#if SCD41_ENABLED
  scdFresh = scdLast.valid && (millis() - scdLast.rxMs) <= 12000;
  if (scdFresh) {
    scdCo2 = scdLast.co2ppm;
  }
#endif

  bool useCm = cmFresh;
  bool useScd = (!useCm && scdFresh);
  float outTC = useScd ? scdLast.tC : (22.0f + ((millis() / 5000) % 10) * 0.1f);
  float outRh = useScd ? scdLast.rh : (40.0f + ((millis() / 7000) % 8) * 0.5f);

  StaticJsonDocument<512> doc;
  doc["src"] = NODE_ID;
  doc["co2_ppm"] = useCm ? cmCo2 : (useScd ? scdCo2 : (650 + (millis() / 1000) % 120));
  doc["t_c"] = outTC;
  doc["rh"] = outRh;
  doc["voc_index"] = 95 + ((millis() / 3000) % 20);
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["uptime_s"] = millis() / 1000;
  doc["co2_src"] = useCm ? "cm1107n" : (useScd ? "scd41" : "dummy");
  if (cmFresh) {
    doc["co2_cm1107_ppm"] = cmCo2;
  }
  if (scdFresh) {
    doc["co2_scd41_ppm"] = scdCo2;
    doc["scd41_t_c"] = scdLast.tC;
    doc["scd41_rh"] = scdLast.rh;
  }

#if PMU_ENABLED
  bool pmuFresh = pmuLast.valid && (millis() - pmuLast.rxMs) <= 10000;
  doc["pmu_present"] = pmuPresent;
  if (pmuFresh) {
    doc["pmu_vbat_raw_v"] = pmuLast.vbatRawV;
    doc["pmu_vbat_v"] = pmuLast.vbatV;
    doc["pmu_vbus_v"] = pmuLast.vbusV;
    doc["pmu_ichg_ma"] = pmuLast.ichgMa;
    doc["pmu_idis_ma"] = pmuLast.idisMa;
  }
#endif

#if CM1107_ENABLED
  if (cmLast.valid) {
    doc["cm1107_temp_raw"] = cmLast.tempRaw;
    doc["cm1107_rh_raw"] = cmLast.rhRaw;
    doc["cm1107_status"] = cmLast.status;
    doc["cm1107_abc_days"] = cmLast.abcDays;
  }
#endif

  String body;
  serializeJson(doc, body);
  postJson("/api/base", body);
}

void forwardLoraPacket(const String& payloadStr, int rssi, float snr) {
  StaticJsonDocument<384> payloadDoc;
  DeserializationError err = deserializeJson(payloadDoc, payloadStr);
  if (err) {
    Serial.printf("[LORA] JSON parse failed: %s\n", err.c_str());
    return;
  }

  if (!payloadDoc["id"].is<const char*>()) {
    Serial.println("[LORA] Missing payload.id, dropped");
    return;
  }

  StaticJsonDocument<640> bodyDoc;
  bodyDoc["gateway_id"] = GATEWAY_ID;
  bodyDoc["rssi"] = rssi;
  bodyDoc["snr"] = snr;
  bodyDoc["payload"] = payloadDoc.as<JsonObject>();

  String body;
  serializeJson(bodyDoc, body);
  postJson("/api/lora", body);
}

void postDummyLoraPacket() {
  StaticJsonDocument<256> payloadDoc;
  payloadDoc["v"] = 1;
  payloadDoc["id"] = DUMMY_NODE_ID;
  payloadDoc["seq"] = (millis() / 1000);
  payloadDoc["t_c"] = 21.8 + ((millis() / 12000) % 12) * 0.1;
  payloadDoc["rh"] = 44.0 + ((millis() / 9000) % 10) * 0.3;
  int leak = ((millis() / 45000) % 8 == 7) ? 1 : 0;
  payloadDoc["leak"] = leak;
  payloadDoc["wet"] = leak ? 3150 : 140;
  payloadDoc["vbatt"] = 3.70;
  payloadDoc["batt_pct"] = 35;

  StaticJsonDocument<512> bodyDoc;
  bodyDoc["gateway_id"] = GATEWAY_ID;
  bodyDoc["rssi"] = -70;
  bodyDoc["snr"] = 8.2;
  bodyDoc["payload"] = payloadDoc.as<JsonObject>();

  String body;
  serializeJson(bodyDoc, body);
  postJson("/api/lora", body);
  Serial.printf("[SIM] Posted dummy /api/lora payload=%s\n", body.c_str());
}

bool initLora() {
  const LoraPinProfile profiles[] = {
    {(int8_t)LORA_PIN_SS, (int8_t)LORA_PIN_RST, (int8_t)LORA_PIN_DIO0, 5, 19, 27},
    {(int8_t)LORA_PIN_SS, 23, (int8_t)LORA_PIN_DIO0, 5, 19, 27},
    {(int8_t)LORA_PIN_SS, -1, (int8_t)LORA_PIN_DIO0, 5, 19, 27},
    {(int8_t)LORA_PIN_SS, (int8_t)LORA_PIN_RST, (int8_t)LORA_PIN_DIO0, 18, 19, 23},
    {(int8_t)LORA_PIN_SS, 23, (int8_t)LORA_PIN_DIO0, 18, 19, 23},
    {(int8_t)LORA_PIN_SS, -1, (int8_t)LORA_PIN_DIO0, 18, 19, 23},
  };

  for (size_t i = 0; i < (sizeof(profiles) / sizeof(profiles[0])); i++) {
    const LoraPinProfile& profile = profiles[i];
    SPI.begin(profile.sck, profile.miso, profile.mosi, profile.ss);
    LoRa.setPins(profile.ss, profile.rst, profile.dio0);
    if (LoRa.begin(LORA_FREQ_MHZ * 1000000.0)) {
      LoRa.setSignalBandwidth(LORA_BW_HZ);
      LoRa.setSpreadingFactor(LORA_SF);
      LoRa.setCodingRate4(LORA_CR);
      LoRa.setTxPower(LORA_TX_PWR);
      LoRa.enableCrc();

      Serial.printf("[LORA] RX ready pins ss=%d rst=%d dio0=%d sck=%d miso=%d mosi=%d freq=%.3fMHz bw=%.0f sf=%d cr=4/%d\n",
                    profile.ss,
                    profile.rst,
                    profile.dio0,
            profile.sck,
            profile.miso,
            profile.mosi,
                    (double)LORA_FREQ_MHZ,
                    (double)LORA_BW_HZ,
                    LORA_SF,
                    LORA_CR);
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

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("GATEWAY_START");

  initCm1107();
  initPmu();
  initScd41();
  connectWiFi();
  loraReady = initLora();
  if (!loraReady) {
    Serial.println("[SIM] LoRa unavailable, using simulated node packets");
    postDummyLoraPacket();
    lastDummyLoraMs = millis();
  }
}

void loop() {
  pollCm1107();
  pollPmu();
  pollScd41();

  if (WiFi.status() != WL_CONNECTED && strlen(WIFI_SSID) > 0) {
    connectWiFi();
  }

  if (loraReady) {
    int packetSize = LoRa.parsePacket();
    if (packetSize > 0) {
      String payload;
      while (LoRa.available()) {
        payload += (char)LoRa.read();
      }

      int rssi = LoRa.packetRssi();
      float snr = LoRa.packetSnr();

      Serial.printf("[LORA] RX bytes=%d rssi=%d snr=%.2f payload=%s\n",
                    packetSize,
                    rssi,
                    snr,
                    payload.c_str());

      forwardLoraPacket(payload, rssi, snr);
    }
  } else if ((millis() - lastDummyLoraMs) >= 15000) {
    lastDummyLoraMs = millis();
    postDummyLoraPacket();
  }

  if ((millis() - lastBasePostMs) >= 60000) {
    lastBasePostMs = millis();
    postBaseDummy();
  }

  delay(10);
}
