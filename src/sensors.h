#pragma once
#include <Arduino.h>

// Optional hardware for the v2 upgrade. Both are safe to call with nothing connected.

// Probes for an SHT31 on the I2C pins. Returns true if one answered.
bool indoorBegin();
// Reads the SHT31. Returns false if no sensor or the read failed.
bool indoorRead(float &tempC, float &humidity);

// Battery voltage from the A0 divider, and a rough percentage for a single LiPo cell.
float batteryVolts();
int batteryPercent(float volts);
