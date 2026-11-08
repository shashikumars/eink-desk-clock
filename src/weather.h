#pragma once
#include <Arduino.h>

struct DayForecast {
  char weekday[4];  // "Fri"
  int code;         // WMO weather code
  float tMax, tMin;
};

struct Weather {
  bool valid = false;
  time_t fetchedAt = 0;
  float temp, feelsLike, wind, todayMax, todayMin;
  int humidity;
  int code;
  bool isDay;
  char sunrise[6], sunset[6];  // "06:07"
  DayForecast days[3];         // the next three days (tomorrow onwards)
};

extern Weather weather;

// Fetches current conditions + 3-day forecast from Open-Meteo (free, no API key).
// Also updates settings.utcOffsetSec from the location's time zone.
bool weatherFetch();

// Looks up a city name. On success fills name/lat/lon and returns true.
bool geocodeCity(const String &query, String &name, float &lat, float &lon);

// Short text for a WMO weather code, e.g. "Light rain".
const char *weatherText(int code);
