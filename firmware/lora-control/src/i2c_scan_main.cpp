#include <Arduino.h>
#include <Wire.h>

#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 21
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 22
#endif

static uint32_t lastScanMs = 0;

const char* hintForAddress(uint8_t addr) {
  if (addr == 0x3C || addr == 0x3D) return "OLED (common)";
  if (addr == 0x62) return "SCD41 (common)";
  if (addr == 0x44 || addr == 0x45) return "SHT3x temp/RH (common)";
  if (addr == 0x76 || addr == 0x77) return "BME/BMP (common)";
  return "";
}

void runScan() {
  uint8_t found = 0;
  Serial.println("[I2C] Scanning...");

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      found++;
      const char* hint = hintForAddress(address);
      if (hint[0] != '\0') {
        Serial.printf("[I2C] Found 0x%02X  %s\n", address, hint);
      } else {
        Serial.printf("[I2C] Found 0x%02X\n", address);
      }
    } else if (error == 4) {
      Serial.printf("[I2C] Unknown error at 0x%02X\n", address);
    }
  }

  Serial.printf("[I2C] Scan done. Devices found: %u\n", found);
  Serial.println("----");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("I2C_SCAN_START");
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Serial.printf("[I2C] Bus init SDA=%d SCL=%d\n", I2C_SDA_PIN, I2C_SCL_PIN);
  delay(200);
  runScan();
  lastScanMs = millis();
}

void loop() {
  if (millis() - lastScanMs >= 5000) {
    lastScanMs = millis();
    runScan();
  }
  delay(20);
}
