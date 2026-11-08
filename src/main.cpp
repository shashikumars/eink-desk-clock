// E-Ink desk clock: time, date, weather and 3-day forecast on a Waveshare 7.5" (B)
// black/white/red e-paper. The clock updates every minute with a fast black/white partial
// refresh; everything else (including red) with a full colour refresh when weather changes.
// Settings: http://eink-clock.local   Firmware updates: PlatformIO env "ota".

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "config.h"
#include "sensors.h"
#include "settings.h"
#include "ui.h"
#include "weather.h"
#include "webui.h"
#include "wifi_nets.h"

static bool hasIndoor = false;
static int lastMinute = -1;
static uint32_t minutesSinceFull = 0;
static uint32_t lastWeatherMs = 0;
static bool weatherChanged = false;
static int lastDay = -1;

static bool timeValid() { return time(nullptr) > 1700000000; }  // after Nov 2023

static void applyTimeZone() {
  configTime(settings.utcOffsetSec, 0, "pool.ntp.org", "time.google.com");
}

static void connectWifi() {
  WiFi.setHostname(HOSTNAME);
  wifiNetsLoad();
  if (!wifiConnectKnown()) {
    // None of the saved networks is in range: open the setup hotspot. The network entered
    // there is added to the saved list (the home network stays the default).
    WiFiManager wm;
    wm.setHostname(HOSTNAME);
    wm.setConfigPortalTimeout(300);  // give up after 5 min, then reboot and retry
    wm.setAPCallback([](WiFiManager *) { uiShowSetup(SETUP_AP_NAME); });
    if (!wm.startConfigPortal(SETUP_AP_NAME)) {
      Serial.println("Wi-Fi setup timed out, restarting");
      ESP.restart();
    }
    wifiNetAdd(wm.getWiFiSSID(), wm.getWiFiPass());
  }
  Serial.printf("Wi-Fi connected to %s, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

// If Wi-Fi has been down for a minute (out of range, router off), try the other saved networks.
static void keepWifi() {
  static uint32_t offlineSince = 0;
  if (WiFi.status() == WL_CONNECTED) {
    offlineSince = 0;
  } else if (!offlineSince) {
    offlineSince = millis();
  } else if (millis() - offlineSince > 60000) {
    Serial.println("Wi-Fi lost, trying saved networks");
    if (wifiConnectKnown()) Serial.printf("Joined %s\n", WiFi.SSID().c_str());
    offlineSince = millis();
  }
}

static void startOta() {
  ArduinoOTA.setHostname(HOSTNAME);  // also starts mDNS, so eink-clock.local resolves
  ArduinoOTA.onStart([] { Serial.println("OTA update starting"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA error %u\n", e); });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);
}

static void updateWeather() {
  if (WiFi.status() != WL_CONNECTED || !settings.hasLocation()) return;
  int32_t oldOffset = settings.utcOffsetSec;
  if (weatherFetch()) {
    lastWeatherMs = millis();
    weatherChanged = true;
    if (settings.utcOffsetSec != oldOffset) {
      applyTimeZone();
      settingsSave();  // remember the time zone for the next boot
    }
  } else {
    // Try again soon instead of waiting a full interval
    lastWeatherMs = millis() - settings.weatherMinutes * 60000UL + WEATHER_RETRY_MS;
  }
}

static Status readStatus() {
  Status st;
  st.wifiOk = WiFi.status() == WL_CONNECTED;
  st.hasIndoor = hasIndoor && indoorRead(st.indoorC, st.indoorHum);
  if (settings.batteryInstalled) {
    st.hasBattery = true;
    st.batteryPct = batteryPercent(batteryVolts());
  }
  return st;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);  // give a PC log window time to attach
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  settingsLoad();
  uiBegin();
  hasIndoor = indoorBegin();

  connectWifi();
  applyTimeZone();
  startOta();
  webBegin();

  for (int i = 0; i < 100 && !timeValid(); i++) delay(100);  // wait up to 10 s for NTP
  updateWeather();
  webWantsFullRedraw = true;
}

void loop() {
  ArduinoOTA.handle();
  webLoop();
  keepWifi();

  if (webWantsWeather || millis() - lastWeatherMs >= settings.weatherMinutes * 60000UL) {
    webWantsWeather = false;
    updateWeather();
  }

  if (!timeValid()) {
    delay(50);
    return;
  }

  time_t t = time(nullptr);
  tm now;
  localtime_r(&t, &now);

  if (now.tm_min != lastMinute || webWantsFullRedraw) {
    bool minuteTick = now.tm_min != lastMinute;
    lastMinute = now.tm_min;
    if (minuteTick) minutesSinceFull++;

    // Colour refresh when something outside the clock changed (new weather, new day,
    // settings) or ghosting needs clearing; otherwise just the fast clock update.
    bool full = webWantsFullRedraw || weatherChanged || now.tm_mday != lastDay ||
                minutesSinceFull >= FULL_REFRESH_MINUTES;
    webWantsFullRedraw = false;

    Status st = readStatus();
    if (full) {
      minutesSinceFull = 0;
      weatherChanged = false;
      lastDay = now.tm_mday;
      uiShowFull(now, st);
    } else {
      uiShowClock(now, st);
    }
  }

  delay(50);
}
