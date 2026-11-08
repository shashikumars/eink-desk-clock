#pragma once

// Settings page at http://eink-clock.local (or the device IP), usable from a PC or phone.
// JSON at /api/status for a future Android app.

void webBegin();
void webLoop();

// Set by the web page; main loop clears them after acting.
extern bool webWantsWeather;
extern bool webWantsFullRedraw;
