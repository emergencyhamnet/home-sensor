#include <Arduino.h>
#include <Wire.h>

#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 21
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 22
#endif

#ifndef PMU_ADDR
#define PMU_ADDR 0x34
#endif

static uint32_t lastDumpMs = 0;

bool readReg8(uint8_t reg, uint8_t &out) {
  Wire.beginTransmission(PMU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  uint8_t req = Wire.requestFrom((int)PMU_ADDR, 1);
  if (req != 1) {
    return false;
  }
  out = Wire.read();
  return true;
}

bool readReg16(uint8_t regHi, uint8_t regLo, uint16_t &out) {
  uint8_t hi = 0;
  uint8_t lo = 0;
  if (!readReg8(regHi, hi) || !readReg8(regLo, lo)) {
    return false;
  }
  out = ((uint16_t)hi << 8) | lo;
  return true;
}

void dumpBasicRegs() {
  Serial.println("[PMU] Register snapshot 0x00..0x10");
  for (uint8_t reg = 0x00; reg <= 0x10; reg++) {
    uint8_t v = 0;
    if (readReg8(reg, v)) {
      Serial.printf("  reg[0x%02X] = 0x%02X\n", reg, v);
    } else {
      Serial.printf("  reg[0x%02X] = --\n", reg);
    }
  }
}

void dumpPowerRaw() {
  uint16_t vbatRaw = 0;
  uint16_t vbusRaw = 0;
  uint16_t ichgRaw = 0;
  uint16_t idisRaw = 0;

  bool hasVbat = readReg16(0x78, 0x79, vbatRaw);
  bool hasVbus = readReg16(0x5A, 0x5B, vbusRaw);
  bool hasIchg = readReg16(0x7A, 0x7B, ichgRaw);
  bool hasIdis = readReg16(0x7C, 0x7D, idisRaw);

  if (hasVbat) {
    float vbat = ((float)(vbatRaw & 0x0FFF)) * 1.1f / 1000.0f;
    Serial.printf("[PMU] VBAT raw=0x%04X approx=%.3f V\n", vbatRaw, vbat);
  } else {
    Serial.println("[PMU] VBAT regs unavailable");
  }

  if (hasVbus) {
    float vbus = ((float)(vbusRaw & 0x0FFF)) * 1.7f / 1000.0f;
    Serial.printf("[PMU] VBUS raw=0x%04X approx=%.3f V\n", vbusRaw, vbus);
  } else {
    Serial.println("[PMU] VBUS regs unavailable");
  }

  if (hasIchg) {
    float ichg = ((float)(ichgRaw & 0x0FFF)) * 0.5f;
    Serial.printf("[PMU] ICHG raw=0x%04X approx=%.1f mA\n", ichgRaw, ichg);
  } else {
    Serial.println("[PMU] ICHG regs unavailable");
  }

  if (hasIdis) {
    float idis = ((float)(idisRaw & 0x1FFF)) * 0.5f;
    Serial.printf("[PMU] IDIS raw=0x%04X approx=%.1f mA\n", idisRaw, idis);
  } else {
    Serial.println("[PMU] IDIS regs unavailable");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("PMU_PROBE_START");

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Serial.printf("[I2C] Bus init SDA=%d SCL=%d PMU_ADDR=0x%02X\n", I2C_SDA_PIN, I2C_SCL_PIN, PMU_ADDR);

  Wire.beginTransmission(PMU_ADDR);
  uint8_t err = Wire.endTransmission();
  if (err != 0) {
    Serial.printf("[PMU] Device 0x%02X not responding (err=%u)\n", PMU_ADDR, err);
    return;
  }

  Serial.printf("[PMU] Device 0x%02X ACK OK\n", PMU_ADDR);
  dumpBasicRegs();
  dumpPowerRaw();
  lastDumpMs = millis();
}

void loop() {
  if (millis() - lastDumpMs >= 5000) {
    lastDumpMs = millis();
    dumpPowerRaw();
  }
  delay(20);
}
