#include "sensors.h"
#include "config.h"
#include <Wire.h>

static uint8_t shtAddr = 0;

bool indoorBegin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  for (uint8_t addr : {0x44, 0x45}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      shtAddr = addr;
      Serial.printf("SHT31 found at 0x%02X\n", addr);
      return true;
    }
  }
  return false;
}

// SHT3x CRC-8, polynomial 0x31, init 0xFF.
static uint8_t crc8(const uint8_t *data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
  }
  return crc;
}

bool indoorRead(float &tempC, float &humidity) {
  if (!shtAddr) return false;

  // Single-shot measurement, high repeatability, no clock stretching.
  Wire.beginTransmission(shtAddr);
  Wire.write(0x24);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  delay(20);

  uint8_t buf[6];
  if (Wire.requestFrom(shtAddr, (uint8_t)6) != 6) return false;
  for (auto &b : buf) b = Wire.read();
  if (crc8(buf, 2) != buf[2] || crc8(buf + 3, 2) != buf[5]) return false;

  tempC = -45.0f + 175.0f * ((buf[0] << 8) | buf[1]) / 65535.0f;
  humidity = 100.0f * ((buf[3] << 8) | buf[4]) / 65535.0f;
  return true;
}

float batteryVolts() {
  // 2x 220k divider halves the battery voltage.
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BATT_ADC);
  return (mv / 8) * 2 / 1000.0f;
}

int batteryPercent(float v) {
  // Approximate LiPo discharge curve.
  static const float curve[][2] = {{4.20f, 100}, {4.00f, 85}, {3.85f, 65}, {3.75f, 45},
                                   {3.65f, 25},  {3.50f, 10}, {3.30f, 0}};
  if (v >= curve[0][0]) return 100;
  for (int i = 1; i < 7; i++) {
    if (v >= curve[i][0]) {
      float t = (v - curve[i][0]) / (curve[i - 1][0] - curve[i][0]);
      return (int)(curve[i][1] + t * (curve[i - 1][1] - curve[i][1]));
    }
  }
  return 0;
}
