#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 21
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 22
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

static Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);
static uint32_t lastPageMs = 0;
static uint32_t bootMs = 0;
static uint8_t pageIdx = 0;
static bool has34 = false;
static bool has3c = false;
static bool has44 = false;
static bool has62 = false;
static bool scdValid = false;
static uint16_t scdCo2 = 0;
static float scdTempC = 0.0f;
static float scdRh = 0.0f;
static uint32_t lastScdPollMs = 0;

uint8_t scd41Crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

bool scd41WriteCmd(uint16_t cmd) {
  Wire.beginTransmission(0x62);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

bool scd41ReadBytes(uint8_t* out, size_t len) {
  uint8_t req = Wire.requestFrom((int)0x62, (int)len);
  if (req != len) {
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    out[i] = (uint8_t)Wire.read();
  }
  return true;
}

void initScd41() {
  if (!has62) {
    return;
  }
  if (scd41WriteCmd(0x21B1)) {
    Serial.println("[SCD41] periodic measurement started");
  } else {
    Serial.println("[SCD41] periodic measurement start failed");
  }
}

void pollScd41() {
  if (!has62) {
    scdValid = false;
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
  if ((readyWord & 0x07FF) == 0) {
    return;
  }

  if (!scd41WriteCmd(0xEC05)) {
    return;
  }
  delay(1);

  uint8_t raw[9];
  if (!scd41ReadBytes(raw, sizeof(raw))) {
    return;
  }
  if (scd41Crc8(&raw[0], 2) != raw[2] || scd41Crc8(&raw[3], 2) != raw[5] || scd41Crc8(&raw[6], 2) != raw[8]) {
    return;
  }

  uint16_t co2 = ((uint16_t)raw[0] << 8) | raw[1];
  uint16_t tRaw = ((uint16_t)raw[3] << 8) | raw[4];
  uint16_t rhRaw = ((uint16_t)raw[6] << 8) | raw[7];

  scdCo2 = co2;
  scdTempC = -45.0f + 175.0f * ((float)tRaw / 65535.0f);
  scdRh = 100.0f * ((float)rhRaw / 65535.0f);
  scdValid = true;

  Serial.printf("[SCD41] CO2=%uppm t=%.2fC rh=%.1f%%\n", scdCo2, scdTempC, scdRh);
}

bool i2cPresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void scanKnown() {
  has34 = i2cPresent(0x34);
  has3c = i2cPresent(0x3C);
  has44 = i2cPresent(0x44);
  has62 = i2cPresent(0x62);
}

void drawPage(const String& line1, const String& line2, const String& line3) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(line1);
  display.println();
  display.println(line2);
  display.println(line3);
  display.display();
}

String yesNo(bool v) {
  return v ? "YES" : "NO";
}

void showCurrentPage() {
  uint32_t uptime = (millis() - bootMs) / 1000;

  if (pageIdx == 0) {
    if (scdValid) {
      drawPage("SCD41 LIVE", "CO2: " + String(scdCo2) + " ppm", "T: " + String(scdTempC, 1) + "C RH: " + String(scdRh, 0) + "%");
    } else {
      drawPage("SCD41 LIVE", "CO2/T/RH waiting...", "uptime: " + String(uptime) + "s");
    }
  } else if (pageIdx == 1) {
    drawPage("I2C DEVICES", "0x3C OLED: " + yesNo(has3c), "0x34 PMU: " + yesNo(has34));
  } else if (pageIdx == 2) {
    drawPage("SENSORS", "0x44 TEMP/RH: " + yesNo(has44), "0x62 SCD41: " + yesNo(has62));
  } else {
    if (scdValid) {
      float tF = (scdTempC * 9.0f / 5.0f) + 32.0f;
      drawPage("ENV SUMMARY", "CO2 " + String(scdCo2) + "ppm", "T " + String(tF, 1) + "F  RH " + String(scdRh, 0) + "%");
    } else {
      drawPage("CO2 A/B TEST", "CM1107 UART + SCD41", "compare drift/response");
    }
  }

  Serial.printf("[OLED] page=%u uptime=%lus 3C=%d 34=%d 44=%d 62=%d\n",
                pageIdx,
                (unsigned long)uptime,
                has3c ? 1 : 0,
                has34 ? 1 : 0,
                has44 ? 1 : 0,
                has62 ? 1 : 0);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("OLED_ROLL_TEST_START");

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  scanKnown();
  initScd41();

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.printf("[OLED] init failed at 0x%02X\n", OLED_ADDR);
    return;
  }

  bootMs = millis();
  lastPageMs = millis();
  showCurrentPage();
}

void loop() {
  pollScd41();
  if ((millis() - lastPageMs) >= 2500) {
    lastPageMs = millis();
    pageIdx = (pageIdx + 1) % 4;
    scanKnown();
    showCurrentPage();
  }
  delay(20);
}
