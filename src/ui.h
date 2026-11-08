#pragma once
#include <time.h>

struct Status {
  bool wifiOk = false;
  bool hasIndoor = false;
  float indoorC = 0, indoorHum = 0;
  bool hasBattery = false;
  int batteryPct = 0;
};

void uiBegin();
// Redraws everything with a full colour refresh (16-26 s, flashes). Clears ghosting.
void uiShowFull(const tm &now, const Status &status);
// Updates only the clock digits with a fast black/white partial refresh (~1.5 s).
void uiShowClock(const tm &now, const Status &status);
// In photo mode the photo is only redrawn when it changes; this forces the next full refresh.
void uiRepaintNext();
// Shown on first boot while the Wi-Fi setup hotspot is active.
void uiShowSetup(const char *apName);

// Streams the last drawn screen as a 4-bit BMP image (white/black/red), in pieces via write().
#include <functional>
constexpr size_t UI_BMP_SIZE = 14 + 40 + 16 * 4 + 400 * 480;
void uiScreenshot(const std::function<void(const uint8_t *, size_t)> &write);
