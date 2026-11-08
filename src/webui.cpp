#include "webui.h"
#include "config.h"
#include "photo_store.h"
#include "settings.h"
#include "ui.h"
#include "weather.h"
#include "webui_photo.h"
#include "wifi_nets.h"
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

bool webWantsWeather = false;
bool webWantsFullRedraw = false;

static WebServer server(80);

static String htmlEscape(const String &s) {
  String out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>E-Ink Clock</title>
<style>
body{font-family:system-ui,sans-serif;max-width:560px;margin:0 auto;padding:16px;background:#f4f4f4;color:#222}
h1{font-size:1.4em;margin:8px 0 12px}h2{font-size:1em;margin:22px 0 8px;text-transform:uppercase;letter-spacing:.06em;color:#555}
form,.box{background:#fff;padding:16px;border-radius:10px;margin-bottom:16px}
label{display:block;margin:12px 0 4px;font-weight:600}
input[type=text],input[type=password],select{width:100%;padding:10px;font-size:1em;box-sizing:border-box}
.row{display:flex;gap:8px;align-items:center;margin-top:12px}.row label{margin:0;font-weight:400}
button{margin-top:16px;padding:12px;width:100%;font-size:1em;border:0;border-radius:8px;background:#222;color:#fff}
button.mini{width:auto;margin:0;padding:6px 10px;font-size:.85em;background:#e2e2e2;color:#222}
.msg{padding:10px;border-radius:8px;background:#e6f4ea}.err{background:#fce8e6}
small{color:#666}a{color:#222}
.screen{display:block;width:100%;height:auto;border:8px solid #1f1f21;border-radius:4px;box-sizing:border-box;background:#e4e3dd;margin-bottom:16px}
.plugin{border:2px solid #e2e2e2;border-radius:10px;padding:12px 14px;margin-top:10px}
.plugin:has(input[name=show]:checked){border-color:#222}
.plugin>label{display:flex;gap:10px;align-items:center;margin:0;cursor:pointer}
.plugin>label span{font-weight:400;color:#666;font-size:.9em}
.plugin .opts{margin-top:6px}
.plugin:not(:has(input[name=show]:checked)) .opts{display:none}
</style></head><body><h1>E-Ink Clock</h1>
)HTML";

static String option(int value, int current, const char *text) {
  return "<option value='" + String(value) + "'" + (value == current ? " selected" : "") + ">" + text + "</option>";
}

// One card per plugin: a radio button to put it on the display, and its own options.
static String pluginCard(int id, const char *name, const char *desc, const String &opts) {
  String h = "<div class='plugin'><label><input type='radio' name='show' value='" + String(id) + "'";
  h += settings.show == id ? " checked" : "";
  h += "><b>" + String(name) + "</b><span>" + desc + "</span></label>";
  if (opts.length()) h += "<div class='opts'>" + opts + "</div>";
  return h + "</div>";
}

static void sendPage(const String &message = "", bool isError = false) {
  String h = FPSTR(PAGE_HEAD);
  if (message.length()) {
    h += "<p class='msg" + String(isError ? " err" : "") + "'>" + htmlEscape(message) + "</p>";
  }
  // What's on the display right now (cache-busted so it updates after each save)
  h += "<a href='/screen.bmp'><img class='screen' src='/screen.bmp?t=" + String(millis()) +
       "' alt='What the display is showing' width='800' height='480'></a>";

  h += "<form method='post' action='/save'>";
  h += "<h2 style='margin-top:0'>On the display</h2>";

  String clock = "<label>Layout</label><select name='layout'>" + option(LAYOUT_WEATHER, settings.layout, "Clock and weather") +
                 option(LAYOUT_CLOCK, settings.layout, "Clock and date only") + "</select>";
  clock += "<label>Clock style</label><select name='cstyle'>" + option(CLOCK_DIGITAL, settings.clockStyle, "Digital") +
           option(CLOCK_ANALOG, settings.clockStyle, "Analog") +
           option(CLOCK_FACE, settings.clockStyle, "Analog, full screen (own date and weather)") + "</select>";
  clock += "<label>Time format</label><select name='clock'>" + option(24, settings.use24h ? 24 : 12, "24-hour") +
           option(12, settings.use24h ? 24 : 12, "12-hour") + "</select>";
  h += pluginCard(SHOW_CLOCK, "Clock", "time, date and weather", clock);

  String photo = "<p style='margin:8px 0 0'><a href='/photo'>Upload a photo</a> ";
  photo += photoVersion() ? "<small>(a photo is stored; uploading replaces it)</small>" : "<small>(none uploaded yet)</small>";
  photo += "</p>";
  h += pluginCard(SHOW_PHOTO, "Photo", "your picture, from phone or PC", photo);

  String solar = "<label>View</label><select name='solar'>" +
                 option(SOLAR_EARTH_MOON, settings.solarView, "Earth and Moon around the Sun") +
                 option(SOLAR_PLANETS, settings.solarView, "All the planets") + "</select>";
  solar += "<label>Orbit shape</label><select name='ellipse'>" + option(0, settings.solarEllipses, "Simple circles") +
           option(1, settings.solarEllipses, "True ellipses (Sun at the focus)") + "</select>";
  solar += "<small>Real positions for right now, redrawn with the colour refresh. Planet spacing is "
           "compressed so all eight fit; with true ellipses each orbit keeps its real shape.</small>";
  h += pluginCard(SHOW_SOLAR, "Solar system", "planets, Earth and Moon, live", solar);

  h += "<h2>Look</h2><label>Theme</label><select name='theme'>" + option(THEME_LIGHT, settings.theme, "Light") +
       option(THEME_DARK, settings.theme, "Dark") + option(THEME_AUTO, settings.theme, "Auto (dark from sunset to sunrise)") +
       "</select>";

  h += "<h2>Weather and location</h2>";
  h += "<label>City</label><input type='text' name='city' value='" + htmlEscape(settings.city) +
       "' placeholder='e.g. Bengaluru'>";
  if (settings.hasLocation()) {
    h += "<small>" + String(settings.lat, 3) + ", " + String(settings.lon, 3) + "</small>";
  }
  h += "<label>Units</label><select name='units'>";
  h += String("<option value='m'") + (settings.metric ? " selected" : "") + ">&deg;C, km/h</option>";
  h += String("<option value='i'") + (!settings.metric ? " selected" : "") + ">&deg;F, mph</option></select>";
  h += "<label>Weather and colour refresh</label><select name='wx'>";
  for (int m : {15, 30, 60}) h += option(m, settings.weatherMinutes, ("Every " + String(m) + " minutes").c_str());
  h += "</select>";
  h += "<div class='row'><input type='checkbox' id='batt' name='batt'";
  h += settings.batteryInstalled ? " checked" : "";
  h += "><label for='batt'>Battery monitor installed</label></div>";

  h += "<button type='submit'>Save and update the display</button></form>";

  h += "<form method='post' action='/refresh'><button type='submit'>Refresh display now</button></form>";

  // Saved Wi-Fi networks: the first is the default; the clock joins the highest one in range
  h += "<form method='post' action='/wifi'><b>Wi-Fi networks</b><br><small>The clock joins the "
       "first network in this list that's in range. The top one is the default.</small>";
  for (int i = 0; i < wifiNetCount(); i++) {
    const String &ssid = wifiNet(i).ssid;
    bool current = WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid;
    h += "<div class='row'><span style='flex:1'>" + htmlEscape(ssid) + (i == 0 ? " <small>(default)</small>" : "") +
         (current ? " <small>&#10003; connected</small>" : "") + "</span>";
    if (i > 0) h += "<button class='mini' name='top' value='" + String(i) + "'>Make default</button>";
    h += "<button class='mini' name='remove' value='" + String(i) + "'>Remove</button></div>";
  }
  h += "<label>Add a network</label><input type='text' name='ssid' placeholder='Network name' autocapitalize='off'>";
  h += "<input type='password' name='pass' placeholder='Password' style='margin-top:8px'>";
  h += "<small>Phone hotspots must be 2.4 GHz (on iPhone, turn on Maximize Compatibility).</small>";
  h += "<button type='submit' name='add' value='1'>Add network</button></form>";

  h += "<div class='box'><small>IP " + WiFi.localIP().toString() + " &middot; Wi-Fi signal " +
       String(WiFi.RSSI()) + " dBm &middot; Free memory " + String(ESP.getFreeHeap() / 1024) +
       " KB</small></div></body></html>";

  server.send(200, "text/html; charset=utf-8", h);
}

static void handleSave() {
  String city = server.arg("city");
  city.trim();
  String msg = "Saved.";
  bool err = false;

  if (city.length() && (city != settings.city || !settings.hasLocation())) {
    String name;
    float lat, lon;
    if (geocodeCity(city, name, lat, lon)) {
      settings.city = name;
      settings.lat = lat;
      settings.lon = lon;
      msg = "Saved. Location set to " + name + ".";
    } else {
      msg = "Could not find \"" + city + "\". Try a nearby bigger city. Other settings were saved.";
      err = true;
    }
  }

  settings.metric = server.arg("units") != "i";
  settings.use24h = server.arg("clock") != "12";
  settings.weatherMinutes = constrain(server.arg("wx").toInt(), 15, 60);
  settings.batteryInstalled = server.hasArg("batt");
  settings.theme = constrain(server.arg("theme").toInt(), THEME_LIGHT, THEME_AUTO);
  settings.layout = constrain(server.arg("layout").toInt(), LAYOUT_WEATHER, LAYOUT_CLOCK);
  settings.clockStyle = constrain(server.arg("cstyle").toInt(), CLOCK_DIGITAL, CLOCK_FACE);
  settings.show = constrain(server.arg("show").toInt(), SHOW_CLOCK, SHOW_COUNT - 1);
  settings.solarView = constrain(server.arg("solar").toInt(), SOLAR_EARTH_MOON, SOLAR_PLANETS);
  settings.solarEllipses = server.arg("ellipse") == "1";
  if (settings.show == SHOW_PHOTO && !photoVersion()) {
    settings.show = SHOW_CLOCK;
    msg = "Saved. There's no photo yet, so the clock stays on. Upload one first.";
  }

  settingsSave();
  webWantsWeather = true;
  webWantsFullRedraw = true;
  sendPage(msg, err);
}

static void handleStatus() {
  JsonDocument doc;
  doc["city"] = settings.city;
  doc["lat"] = settings.lat;
  doc["lon"] = settings.lon;
  doc["use24h"] = settings.use24h;
  doc["metric"] = settings.metric;
  doc["weatherMinutes"] = settings.weatherMinutes;
  doc["batteryInstalled"] = settings.batteryInstalled;
  doc["theme"] = settings.theme;
  doc["layout"] = settings.layout;
  doc["clockStyle"] = settings.clockStyle;
  doc["show"] = settings.show;
  doc["solarView"] = settings.solarView;
  doc["solarEllipses"] = settings.solarEllipses;
  doc["photoVersion"] = photoVersion();
  doc["rssi"] = WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  if (weather.valid) {
    doc["weather"]["temp"] = weather.temp;
    doc["weather"]["code"] = weather.code;
    doc["weather"]["fetchedAt"] = (long)weather.fetchedAt;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleWifi() {
  String msg;
  if (server.hasArg("remove")) {
    int i = server.arg("remove").toInt();
    if (i >= 0 && i < wifiNetCount()) {
      msg = "Removed " + wifiNet(i).ssid + ".";
      wifiNetRemove(i);
    }
  } else if (server.hasArg("top")) {
    int i = server.arg("top").toInt();
    if (i > 0 && i < wifiNetCount()) {
      msg = wifiNet(i).ssid + " is now the default.";
      wifiNetMakeDefault(i);
    }
  } else {
    String ssid = server.arg("ssid");
    ssid.trim();
    if (ssid.isEmpty()) {
      sendPage("Enter the network name to add it.", true);
      return;
    }
    wifiNetAdd(ssid, server.arg("pass"));
    msg = "Added " + ssid + ". The clock will use it when your default network isn't in range.";
  }
  sendPage(msg);
}

// Wiring probe: a jumper from A1 (IO5) touched to any point shows what that point is connected
// to. The pin only ever reads (weak internal pulls), so probing anything here is safe.
static String probeReading() {
  pinMode(PIN_PROBE, INPUT_PULLUP);
  delay(3);
  bool highWithPullUp = digitalRead(PIN_PROBE);
  pinMode(PIN_PROBE, INPUT_PULLDOWN);
  delay(3);
  bool highWithPullDown = digitalRead(PIN_PROBE);
  int mv = 0;
  const char *what;
  if (highWithPullDown) {
    mv = analogReadMilliVolts(PIN_PROBE);
    what = mv > 3000 ? "power (3.3 V)" : "a signal that is high";
  } else if (!highWithPullUp) {
    what = "GND (or a signal that is low)";
  } else {
    what = "nothing (not connected)";
  }
  pinMode(PIN_PROBE, INPUT);
  return String("{\"reading\":\"") + what + "\",\"mv\":" + mv + "}";
}

static const char PROBE_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wiring probe</title>
<style>body{font-family:system-ui,sans-serif;max-width:480px;margin:0 auto;padding:16px;background:#f4f4f4;color:#222}
.box{background:#fff;padding:20px;border-radius:10px}#r{font-size:1.8em;font-weight:700;margin:12px 0}small{color:#666}</style>
</head><body><h1>Wiring probe</h1><div class="box">
<p>Connect a jumper to <b>A1 (IO5)</b> on the FireBeetle and touch its other end to the point you want to test.</p>
<div id="r">...</div><small id="mv"></small></div>
<p><a href="/">&larr; Back to settings</a></p>
<script>
async function poll(){try{const j=await (await fetch('/api/probe')).json();
document.getElementById('r').textContent=j.reading;document.getElementById('mv').textContent=j.mv?j.mv+' mV':'';}catch(e){}
setTimeout(poll,500);}poll();
</script></body></html>
)HTML";

// Photo upload: the browser sends the finished panel image (96000 bytes) as a file.
static bool photoOk = false;

static void handlePhotoUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    photoOk = photoUploadBegin();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (photoOk) photoOk = photoUploadWrite(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (photoOk) photoOk = photoUploadFinish();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    photoOk = false;
  }
}

static void handlePhotoDone() {
  if (!photoOk) {
    server.send(400, "text/plain", "the photo didn't arrive complete");
    return;
  }
  settings.show = SHOW_PHOTO;
  settingsSave();
  webWantsFullRedraw = true;
  server.send(200, "text/plain", "ok");
}

void webBegin() {
  server.on("/", HTTP_GET, [] { sendPage(); });
  server.on("/save", HTTP_POST, handleSave);
  server.on("/refresh", HTTP_POST, [] {
    webWantsWeather = true;
    webWantsFullRedraw = true;
    uiRepaintNext();
    sendPage("Refreshing the display...");
  });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/probe", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", PROBE_PAGE); });
  server.on("/api/probe", HTTP_GET, [] { server.send(200, "application/json", probeReading()); });
  server.on("/wifi", HTTP_POST, handleWifi);
  server.on("/photo", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", PHOTO_PAGE); });
  server.on("/photo", HTTP_POST, handlePhotoDone, handlePhotoUpload);
  server.on("/screen.bmp", HTTP_GET, [] {
    server.setContentLength(UI_BMP_SIZE);
    server.send(200, "image/bmp", "");
    uiScreenshot([](const uint8_t *data, size_t len) { server.sendContent((const char *)data, len); });
  });
  server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

void webLoop() { server.handleClient(); }
