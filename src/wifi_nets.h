#pragma once
#include <Arduino.h>

// Saved Wi-Fi networks, in order of preference: the first one is the default (home) network.
// Stored in flash, so they survive power cuts and firmware updates.

constexpr int WIFI_MAX_NETS = 6;

struct WifiNet {
  String ssid;
  String pass;
};

void wifiNetsLoad();
int wifiNetCount();
const WifiNet &wifiNet(int i);
// Adds a network, or updates its password if it's already saved. New networks go last.
void wifiNetAdd(const String &ssid, const String &pass);
void wifiNetRemove(int i);
// Moves a network to the top, making it the default.
void wifiNetMakeDefault(int i);

// Scans, then joins the most preferred saved network that's in range.
// Returns true once connected.
bool wifiConnectKnown();
