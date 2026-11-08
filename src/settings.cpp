#include "settings.h"
#include <Preferences.h>

Settings settings;

static Preferences prefs;

void settingsLoad() {
  prefs.begin("clock", true);
  settings.city = prefs.getString("city", "");
  settings.lat = prefs.getFloat("lat", NAN);
  settings.lon = prefs.getFloat("lon", NAN);
  settings.use24h = prefs.getBool("24h", true);
  settings.metric = prefs.getBool("metric", true);
  settings.weatherMinutes = prefs.getUShort("wxmin", 30);
  settings.utcOffsetSec = prefs.getInt("utcoff", 0);
  settings.batteryInstalled = prefs.getBool("batt", false);
  settings.theme = prefs.getUChar("theme", THEME_DARK);
  settings.layout = prefs.getUChar("layout", LAYOUT_WEATHER);
  settings.clockStyle = prefs.getUChar("cstyle", CLOCK_DIGITAL);
  settings.show = prefs.getUChar("show", SHOW_CLOCK);
  settings.solarView = prefs.getUChar("solar", SOLAR_EARTH_MOON);
  settings.solarEllipses = prefs.getBool("ellipse", false);
  prefs.end();
}

void settingsSave() {
  prefs.begin("clock", false);
  prefs.putString("city", settings.city);
  prefs.putFloat("lat", settings.lat);
  prefs.putFloat("lon", settings.lon);
  prefs.putBool("24h", settings.use24h);
  prefs.putBool("metric", settings.metric);
  prefs.putUShort("wxmin", settings.weatherMinutes);
  prefs.putInt("utcoff", settings.utcOffsetSec);
  prefs.putBool("batt", settings.batteryInstalled);
  prefs.putUChar("theme", settings.theme);
  prefs.putUChar("layout", settings.layout);
  prefs.putUChar("cstyle", settings.clockStyle);
  prefs.putUChar("show", settings.show);
  prefs.putUChar("solar", settings.solarView);
  prefs.putBool("ellipse", settings.solarEllipses);
  prefs.end();
}
