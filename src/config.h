#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Wiring: Waveshare 7.5" HAT (B)  ->  FireBeetle 2 ESP32-S3 (DFR1145)
//   HAT pin   Board label   GPIO
//   VCC       3V3           -
//   GND       GND           -
//   DIN       MOSI          15
//   CLK       SCK           17
//   CS        SS            10
//   DC        D11           13
//   RST       D12           12
//   BUSY      D10           14
//   PWR       D7            9    (only on HAT Rev 2.3+, otherwise leave unconnected)
// ---------------------------------------------------------------------------
constexpr int PIN_EPD_MOSI = 15;
constexpr int PIN_EPD_MISO = 16;  // unused by the display, but part of the SPI bus
constexpr int PIN_EPD_SCK  = 17;
constexpr int PIN_EPD_CS   = 10;
constexpr int PIN_EPD_DC   = 13;
constexpr int PIN_EPD_RST  = 12;
constexpr int PIN_EPD_BUSY = 14;
constexpr int PIN_EPD_PWR  = 9;

// Reserved for the v2 upgrade (nothing needs to be connected yet)
constexpr int PIN_I2C_SDA  = 1;   // SDA label — SHT31 indoor sensor
constexpr int PIN_I2C_SCL  = 2;   // SCL label
constexpr int PIN_BATT_ADC = 4;   // A0 — midpoint of 2x 220k divider from the VCC pin to GND
constexpr int PIN_PROBE    = 5;   // A1 — test lead for the wiring probe page (/probe), read only

constexpr int PIN_LED      = 21;  // on-board LED, kept off to save power

// ---------------------------------------------------------------------------
// Behaviour
// ---------------------------------------------------------------------------
constexpr const char *HOSTNAME        = "eink-clock";   // http://eink-clock.local
constexpr const char *SETUP_AP_NAME   = "EInk-Clock-Setup";
constexpr uint32_t FULL_REFRESH_MINUTES = 60;   // colour refresh at least this often, clears ghosting
constexpr uint32_t WEATHER_RETRY_MS     = 2 * 60 * 1000;  // after a failed fetch
