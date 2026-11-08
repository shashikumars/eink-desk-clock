#include "wifi_nets.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>

static WifiNet nets[WIFI_MAX_NETS];
static int count = 0;
static Preferences prefs;

static void save() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.putUChar("n", count);
  for (int i = 0; i < count; i++) {
    prefs.putString(("s" + String(i)).c_str(), nets[i].ssid);
    prefs.putString(("p" + String(i)).c_str(), nets[i].pass);
  }
  prefs.end();
}

void wifiNetsLoad() {
  prefs.begin("wifi", false);  // read-write, so the namespace is created on first boot
  count = min<int>(prefs.getUChar("n", 0), WIFI_MAX_NETS);
  for (int i = 0; i < count; i++) {
    nets[i].ssid = prefs.getString(("s" + String(i)).c_str(), "");
    nets[i].pass = prefs.getString(("p" + String(i)).c_str(), "");
  }
  prefs.end();

  // First boot with this firmware: adopt the network set up earlier (kept by the ESP32 itself)
  if (count == 0) {
    WiFi.mode(WIFI_STA);
    wifi_config_t conf;
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && conf.sta.ssid[0]) {
      wifiNetAdd((const char *)conf.sta.ssid, (const char *)conf.sta.password);
    }
  }
}

int wifiNetCount() { return count; }
const WifiNet &wifiNet(int i) { return nets[i]; }

void wifiNetAdd(const String &ssid, const String &pass) {
  if (ssid.isEmpty()) return;
  for (int i = 0; i < count; i++) {
    if (nets[i].ssid == ssid) {
      nets[i].pass = pass;
      save();
      return;
    }
  }
  if (count == WIFI_MAX_NETS) count--;  // full: the least preferred one makes room
  nets[count++] = {ssid, pass};
  save();
}

void wifiNetRemove(int i) {
  if (i < 0 || i >= count) return;
  for (int j = i; j < count - 1; j++) nets[j] = nets[j + 1];
  count--;
  save();
}

void wifiNetMakeDefault(int i) {
  if (i <= 0 || i >= count) return;
  WifiNet n = nets[i];
  for (int j = i; j > 0; j--) nets[j] = nets[j - 1];
  nets[0] = n;
  save();
}

bool wifiConnectKnown() {
  if (count == 0) return false;
  WiFi.mode(WIFI_STA);
  int found = WiFi.scanNetworks();
  for (int i = 0; i < count; i++) {
    bool inRange = false;
    for (int k = 0; k < found && !inRange; k++) inRange = WiFi.SSID(k) == nets[i].ssid;
    if (!inRange) continue;
    Serial.printf("Joining %s\n", nets[i].ssid.c_str());
    WiFi.begin(nets[i].ssid.c_str(), nets[i].pass.c_str());
    for (int t = 0; t < 150 && WiFi.status() != WL_CONNECTED; t++) delay(100);  // up to 15 s
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.scanDelete();
      return true;
    }
    Serial.printf("Could not join %s (wrong password?)\n", nets[i].ssid.c_str());
    WiFi.disconnect();
  }
  WiFi.scanDelete();
  return false;
}
