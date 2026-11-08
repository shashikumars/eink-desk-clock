#pragma once
#include <Arduino.h>

// Screen theme. Auto is dark from sunset to sunrise.
enum : uint8_t { THEME_LIGHT = 0, THEME_DARK = 1, THEME_AUTO = 2 };
// Screen layout: clock with weather (design 1b), or a big clock with the date only (design 2).
enum : uint8_t { LAYOUT_WEATHER = 0, LAYOUT_CLOCK = 1 };
// Clock face, in either layout.
enum : uint8_t { CLOCK_DIGITAL = 0, CLOCK_ANALOG = 1, CLOCK_FACE = 2 };  // FACE: full-screen analog
// What the screen shows: which plugin is on the display.
enum : uint8_t { SHOW_CLOCK = 0, SHOW_PHOTO = 1, SHOW_SOLAR = 2, SHOW_COUNT };
// Solar system plugin view.
enum : uint8_t { SOLAR_EARTH_MOON = 0, SOLAR_PLANETS = 1 };

// User settings, stored in flash (NVS) so they survive power loss and firmware updates.
struct Settings {
  String city;              // display name, e.g. "Bengaluru"
  float lat = NAN;
  float lon = NAN;
  bool use24h = true;
  bool metric = true;       // false = °F and mph
  uint16_t weatherMinutes = 30;  // each weather update = one full colour refresh
  int32_t utcOffsetSec = 0; // learned from the weather API, cached for boot before Wi-Fi
  bool batteryInstalled = false;
  uint8_t theme = THEME_DARK;
  uint8_t layout = LAYOUT_WEATHER;  // weather is still fetched in clock-only mode
  uint8_t clockStyle = CLOCK_DIGITAL;
  uint8_t show = SHOW_CLOCK;
  uint8_t solarView = SOLAR_EARTH_MOON;
  bool solarEllipses = false;  // draw orbits as their true ellipses instead of circles

  bool hasLocation() const { return !isnan(lat) && !isnan(lon); }
};

extern Settings settings;

void settingsLoad();
void settingsSave();
