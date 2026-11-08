#include "weather.h"
#include "settings.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

Weather weather;

static const char *WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// Day of week for a "YYYY-MM-DD" string (Sakamoto's method), 0 = Sunday.
static int weekdayOf(const char *iso) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = atoi(iso), m = atoi(iso + 5), d = atoi(iso + 8);
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// Copies "HH:MM" out of "YYYY-MM-DDTHH:MM".
static void copyTime(char *dst, const char *iso) {
  const char *t = iso ? strchr(iso, 'T') : nullptr;
  if (t) {
    strncpy(dst, t + 1, 5);
    dst[5] = '\0';
  } else {
    strcpy(dst, "--:--");
  }
}

static bool getJson(const String &url, JsonDocument &doc) {
  WiFiClientSecure client;
  client.setInsecure();  // public weather data; skipping certificate checks keeps this simple
  HTTPClient http;
  http.useHTTP10(true);  // avoids chunked responses so the stream can be parsed directly
  http.setTimeout(10000);
  if (!http.begin(client, url)) return false;

  int status = http.GET();
  bool ok = false;
  if (status == HTTP_CODE_OK) {
    DeserializationError err = deserializeJson(doc, http.getStream());
    ok = !err;
    if (err) Serial.printf("JSON error: %s\n", err.c_str());
  } else {
    Serial.printf("HTTP %d for %s\n", status, url.c_str());
  }
  http.end();
  return ok;
}

bool weatherFetch() {
  if (!settings.hasLocation()) return false;

  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(settings.lat, 4) +
               "&longitude=" + String(settings.lon, 4) +
               "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
               "weather_code,wind_speed_10m,is_day"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
               "&timezone=auto&forecast_days=4";
  if (!settings.metric) url += "&temperature_unit=fahrenheit&wind_speed_unit=mph";

  JsonDocument doc;
  if (!getJson(url, doc)) return false;

  settings.utcOffsetSec = doc["utc_offset_seconds"] | settings.utcOffsetSec;

  JsonObject cur = doc["current"];
  weather.temp = cur["temperature_2m"];
  weather.feelsLike = cur["apparent_temperature"];
  weather.humidity = cur["relative_humidity_2m"];
  weather.code = cur["weather_code"];
  weather.wind = cur["wind_speed_10m"];
  weather.isDay = cur["is_day"] | 1;

  JsonObject daily = doc["daily"];
  weather.todayMax = daily["temperature_2m_max"][0];
  weather.todayMin = daily["temperature_2m_min"][0];
  copyTime(weather.sunrise, daily["sunrise"][0]);
  copyTime(weather.sunset, daily["sunset"][0]);

  for (int i = 0; i < 3; i++) {
    DayForecast &d = weather.days[i];
    const char *date = daily["time"][i + 1] | "2000-01-01";
    strcpy(d.weekday, WEEKDAYS[weekdayOf(date)]);
    d.code = daily["weather_code"][i + 1];
    d.tMax = daily["temperature_2m_max"][i + 1];
    d.tMin = daily["temperature_2m_min"][i + 1];
  }

  weather.valid = true;
  weather.fetchedAt = time(nullptr);
  return true;
}

static String urlEncode(const String &s) {
  String out;
  for (char c : s) {
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') {
      out += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}

bool geocodeCity(const String &query, String &name, float &lat, float &lon) {
  String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&name=" +
               urlEncode(query);
  JsonDocument doc;
  if (!getJson(url, doc)) return false;

  JsonObject r = doc["results"][0];
  if (r.isNull()) return false;
  name = r["name"].as<String>();
  lat = r["latitude"];
  lon = r["longitude"];
  return true;
}

const char *weatherText(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Storm with hail";
    default: return "--";
  }
}
