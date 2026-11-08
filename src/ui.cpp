#include "ui.h"
#include "epd.h"
#include "ui_draw.h"
#include "photo_store.h"
#include "plugin_solar.h"
#include "settings.h"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "weather.h"

// "Editorial" layout (Claude Design handoff, design 1b), 800 x 480:
//   left:  date line / big clock / current weather, separated by thick rules
//   right: next days list, sunrise and sunset, status footer
// Red is only used for the current temperature, which changes slowly: the every-minute
// refresh is black/white only and covers just the clock area.

static Canvas3C canvas;
Canvas3C &uiCanvas() { return canvas; }
static Epd7in5b epd;

// Layout grid (px)
constexpr int TOP = 20, BOTTOM = 468;
constexpr int L_X0 = 28, L_X1 = 497;      // left column
constexpr int RULE_X = 523;               // 3 px vertical rule
constexpr int R_X0 = 552, R_X1 = 772;     // right column
// Area redrawn every minute: between the two thick rules, black only (x and w multiples of 8)
constexpr int CLOCK_X = 24, CLOCK_Y = 56, CLOCK_W = 480, CLOCK_H = 322;

static const char *DAYS3[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *DAYS_SHORT[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *MONTHS3[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};


// ---------------------------------------------------------------------------
// Text and icons
// ---------------------------------------------------------------------------
// Decodes one UTF-8 character (only 1- and 2-byte sequences are used here).
static uint16_t nextChar(const char *&s) {
  uint8_t c = *s++;
  if (c < 0x80 || !*s) return c;
  return ((c & 0x1F) << 6) | (*s++ & 0x3F);
}

static const UiGlyph *findGlyph(const UiFont &f, uint16_t code) {
  for (int i = 0; i < f.count; i++) {
    if (f.codes[i] == code) return &f.glyphs[i];
  }
  return nullptr;
}

// Draws text with its baseline at y. spacing adds extra px between characters (letter-spacing).
// Returns the width drawn.
int text(const UiFont &f, int x, int y, const char *s, uint16_t color, int spacing) {
  int start = x;
  bool first = true;
  while (*s) {
    const UiGlyph *g = findGlyph(f, nextChar(s));
    if (!g) continue;
    if (!first) x += spacing;
    first = false;
    if (g->width) {
      canvas.drawBitmap(x + g->xOffset, y + g->yOffset, f.bitmap + g->offset, g->width, g->height, color);
    }
    x += g->xAdvance;
  }
  return x - start;
}

int textWidth(const UiFont &f, const char *s, int spacing) {
  int w = 0;
  bool first = true;
  while (*s) {
    const UiGlyph *g = findGlyph(f, nextChar(s));
    if (!g) continue;
    if (!first) w += spacing;
    first = false;
    w += g->xAdvance;
  }
  return w;
}

void textRight(const UiFont &f, int xRight, int y, const char *s, uint16_t color, int spacing) {
  text(f, xRight - textWidth(f, s, spacing), y, s, color, spacing);
}

void textCenter(const UiFont &f, int cx, int y, const char *s, int spacing) {
  text(f, cx - textWidth(f, s, spacing) / 2, y, s, EPD_BLACK, spacing);
}

void icon(const UiIcon &ic, int cx, int cy, uint16_t color) {
  canvas.drawBitmap(cx - ic.size / 2, cy - ic.size / 2, ic.bitmap, ic.size, ic.size, color);
}

// Material Symbols icon for a WMO weather code, at 64 px (current) or 30 px (forecast rows).
static const UiIcon &weatherIcon(int code, bool isDay, bool big) {
#define PICK(name) return big ? ICON_##name##_64 : ICON_##name##_30
  if (code <= 1) { if (isDay) PICK(SUNNY); else PICK(BEDTIME); }
  if (code == 2) { if (isDay) PICK(PARTLY_CLOUDY_DAY); else PICK(PARTLY_CLOUDY_NIGHT); }
  if (code == 3) PICK(CLOUD);
  if (code == 45 || code == 48) PICK(FOGGY);
  if (code >= 51 && code <= 57) PICK(RAINY_LIGHT);
  if (code == 65 || code == 66 || code == 67 || code == 82) PICK(RAINY_HEAVY);
  if ((code >= 61 && code <= 64) || code == 80 || code == 81) PICK(RAINY);
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) PICK(WEATHER_SNOWY);
  if (code >= 95) PICK(THUNDERSTORM);
  PICK(CLOUD);
#undef PICK
}

void rule(int x, int y, int w, int h) { canvas.fillRect(x, y, w, h, EPD_BLACK); }

// ---------------------------------------------------------------------------
// Sections
// ---------------------------------------------------------------------------
static void drawDateLine(const tm &now, const Status &st) {
  char date[32], right[40];
  snprintf(date, sizeof(date), "%s " DOT " %d %s %d", DAYS3[now.tm_wday], now.tm_mday, MONTHS3[now.tm_mon],
           now.tm_year + 1900);
  int w = text(FONT_MONO, L_X0, TOP + 17, date, EPD_BLACK, 2);

  if (st.hasIndoor) {
    float t = settings.metric ? st.indoorC : st.indoorC * 9 / 5 + 32;
    snprintf(right, sizeof(right), "IN %.0f" DEG " %.0f%%", t, st.indoorHum);
  } else {
    // City in capitals, shortened if it would run into the date
    int n = 0;
    for (const char *c = settings.city.c_str(); *c && n < (int)sizeof(right) - 1; c++) right[n++] = toupper(*c);
    right[n] = '\0';
    while (n > 0 && textWidth(FONT_MONO, right, 2) > (L_X1 - L_X0) - w - 24) right[--n] = '\0';
  }
  textRight(FONT_MONO, L_X1, TOP + 17, right, EPD_BLACK, 2);
  rule(L_X0, 49, L_X1 - L_X0, 3);
}

// ---------------------------------------------------------------------------
// Analog dial: ring, minute ticks, bold hour marks, 12/3/6/9 numerals, black hands.
// Kept black-only so it can use the fast every-minute refresh.
// ---------------------------------------------------------------------------
// Thick line with rounded ends, from radius r0 to r1 along angle a (radians, 0 = 12 o'clock).
void spoke(int cx, int cy, float a, float r0, float r1, float width, uint16_t color) {
  float dx = sinf(a), dy = -cosf(a), px = -dy * width / 2, py = dx * width / 2;
  float x0 = cx + dx * r0, y0 = cy + dy * r0, x1 = cx + dx * r1, y1 = cy + dy * r1;
  canvas.fillTriangle(x0 + px, y0 + py, x0 - px, y0 - py, x1 + px, y1 + py, color);
  canvas.fillTriangle(x0 - px, y0 - py, x1 - px, y1 - py, x1 + px, y1 + py, color);
  canvas.fillCircle(x1, y1, width / 2, color);
}

static void drawAnalog(const tm &now, int cx, int cy, int r) {
  canvas.fillCircle(cx, cy, r, EPD_BLACK);
  canvas.fillCircle(cx, cy, r - 3, EPD_WHITE);
  for (int i = 0; i < 60; i++) {
    float a = i * PI / 30;
    if (i % 15 == 0) continue;  // numerals instead
    if (i % 5 == 0) spoke(cx, cy, a, r - 28, r - 10, 6);
    else spoke(cx, cy, a, r - 14, r - 9, 2);
  }
  // Numerals, centred on a circle inside the marks
  const char *nums[] = {"12", "3", "6", "9"};
  for (int q = 0; q < 4; q++) {
    float a = q * PI / 2, nr = r - 34;
    int x = cx + sinf(a) * nr, y = cy - cosf(a) * nr;
    textCenter(FONT_DIAL, x, y + FONT_DIAL.capHeight / 2, nums[q]);
  }

  float minA = now.tm_min * PI / 30;
  float hourA = ((now.tm_hour % 12) + now.tm_min / 60.0f) * PI / 6;
  spoke(cx, cy, hourA + PI, 0, r * 0.10f, 12);  // short tails behind the centre
  spoke(cx, cy, minA + PI, 0, r * 0.12f, 8);
  spoke(cx, cy, hourA, 0, r * 0.52f, 12);
  spoke(cx, cy, minA, 0, r - 24, 8);
  canvas.fillCircle(cx, cy, 10, EPD_BLACK);
  canvas.fillCircle(cx, cy, 3, EPD_WHITE);
}

static void drawClock(const tm &now) {
  if (settings.clockStyle == CLOCK_ANALOG) {
    // Centred in the left column, between the two thick rules
    drawAnalog(now, (L_X0 + L_X1) / 2, 217, 150);
    return;
  }
  char buf[8];
  int hour = now.tm_hour;
  if (!settings.use24h) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  snprintf(buf, sizeof(buf), settings.use24h ? "%02d:%02d" : "%d:%02d", hour, now.tm_min);
  int w = text(FONT_CLOCK, CLOCK_X, 300, buf, EPD_BLACK, -6);
  if (!settings.use24h) text(FONT_MONO_AMPM, CLOCK_X + w + 8, 150, now.tm_hour < 12 ? "AM" : "PM");
}

static void drawCurrentWeather() {
  rule(L_X0, 382, L_X1 - L_X0, 3);
  if (!settings.hasLocation()) {
    text(FONT_TITLE, L_X0, 432, "Set your city at http://eink-clock.local");
    return;
  }
  if (!weather.valid) {
    text(FONT_TITLE, L_X0, 432, "Waiting for weather...");
    return;
  }
  char buf[48];
  icon(weatherIcon(weather.code, weather.isDay, true), L_X0 + 32, 432);
  snprintf(buf, sizeof(buf), "%.0f" DEG, weather.temp);
  int tx = L_X0 + 80 + text(FONT_TEMP, L_X0 + 80, 461, buf, EPD_RED) + 16;
  text(FONT_TITLE, tx, 418, weatherText(weather.code));
  snprintf(buf, sizeof(buf), "Feels %.0f" DEG " " DOT " Humidity %d%%", weather.feelsLike, weather.humidity);
  text(FONT_BODY, tx, 440, buf);
  snprintf(buf, sizeof(buf), "Wind %.0f %s", weather.wind, settings.metric ? "km/h" : "mph");
  text(FONT_BODY, tx, 461, buf);
}

static void drawDayRow(int y, const char *label, int code, bool isDay, float hi, float lo) {
  char loTxt[12], hiTxt[12];
  rule(R_X0, y, R_X1 - R_X0, 1);
  int cy = y + 26;
  text(FONT_LABEL, R_X0, cy + 6, label);
  icon(weatherIcon(code, isDay, false), R_X0 + 72, cy);
  snprintf(loTxt, sizeof(loTxt), "%.0f" DEG, lo);
  snprintf(hiTxt, sizeof(hiTxt), "%.0f" DEG " ", hi);
  int loW = textWidth(FONT_DAY_LO, loTxt);
  text(FONT_DAY_LO, R_X1 - loW, cy + 9, loTxt);
  textRight(FONT_DAY_HI, R_X1 - loW, cy + 9, hiTxt);
}

static void drawRightColumn(const Status &st) {
  rule(RULE_X, TOP, 3, BOTTOM - TOP);
  text(FONT_MONO_SMALL, R_X0, TOP + 17, "NEXT DAYS", EPD_BLACK, 2);

  int y = 50;
  if (weather.valid) {
    drawDayRow(y, "Today", weather.code, weather.isDay, weather.todayMax, weather.todayMin);
    y += 52;
    for (const DayForecast &d : weather.days) {
      char label[8];
      snprintf(label, sizeof(label), "%c%c%c", d.weekday[0], d.weekday[1], d.weekday[2]);
      drawDayRow(y, label, d.code, true, d.tMax, d.tMin);
      y += 52;
    }
    rule(R_X0, y, R_X1 - R_X0, 1);
    icon(ICON_WB_TWILIGHT_20, R_X0 + 10, y + 26);
    text(FONT_SMALL, R_X0 + 24, y + 32, weather.sunrise);
    icon(ICON_BEDTIME_20, R_X1 - 58, y + 26);
    textRight(FONT_SMALL, R_X1, y + 32, weather.sunset);
  } else {
    rule(R_X0, y, R_X1 - R_X0, 1);
  }

  // Footer
  char upd[16] = "UPD --:--";
  if (weather.valid) {
    tm t;
    localtime_r(&weather.fetchedAt, &t);
    snprintf(upd, sizeof(upd), "UPD %02d:%02d", t.tm_hour, t.tm_min);
  }
  text(FONT_MONO_FOOT, R_X0, BOTTOM - 3, upd, EPD_BLACK, 1);
  if (st.hasBattery) {
    char bat[12];
    snprintf(bat, sizeof(bat), "BAT %d%%", st.batteryPct);
    textCenter(FONT_MONO_FOOT, (R_X0 + R_X1) / 2 + 6, BOTTOM - 3, bat, 1);
  }
  textRight(FONT_MONO_FOOT, R_X1, BOTTOM - 3, st.wifiOk ? "WI-FI OK" : "OFFLINE", EPD_BLACK, 1);
}

// "HH:MM" -> minutes since midnight, or -1 if unknown.
static int minutesOf(const char *hhmm) {
  if (!hhmm || hhmm[2] != ':') return -1;
  return atoi(hhmm) * 60 + atoi(hhmm + 3);
}

static bool isDark(const tm &now) {
  if (settings.theme != THEME_AUTO) return settings.theme == THEME_DARK;
  int rise = weather.valid ? minutesOf(weather.sunrise) : -1;
  int set = weather.valid ? minutesOf(weather.sunset) : -1;
  if (rise < 0 || set < 0) return false;
  int m = now.tm_hour * 60 + now.tm_min;
  return m < rise || m >= set;
}

// ---------------------------------------------------------------------------
// Clock-only layout (design 2a light / 2b dark): weekday and AM/PM on top, a huge centred
// clock, date and year along the bottom.
// ---------------------------------------------------------------------------
constexpr int XL_X0 = 36, XL_X1 = 764;
// Area redrawn every minute: between the two rules (x and w multiples of 8)
constexpr int XL_CLOCK_X = 32, XL_CLOCK_Y = 64, XL_CLOCK_W = 736, XL_CLOCK_H = 336;
static const char *DAYS_FULL[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
static const char *MONTHS_FULL[] = {"January", "February", "March",     "April",   "May",      "June",
                                    "July",    "August",   "September", "October", "November", "December"};

static void renderClockOnly(const tm &now) {
  char buf[24];
  text(FONT_MONO, XL_X0, 45, DAYS_FULL[now.tm_wday], EPD_RED, 2);
  // AM/PM only with the digital clock; the analog dial shows neither
  if (!settings.use24h && settings.clockStyle == CLOCK_DIGITAL) {
    textRight(FONT_MONO, XL_X1, 45, now.tm_hour < 12 ? "AM" : "PM", EPD_BLACK, 2);
  }
  rule(XL_X0, 58, XL_X1 - XL_X0, 3);

  if (settings.clockStyle == CLOCK_ANALOG) {
    drawAnalog(now, (XL_X0 + XL_X1) / 2, 232, 160);
  } else {
    int hour = now.tm_hour;
    if (!settings.use24h) {
      hour %= 12;
      if (hour == 0) hour = 12;
    }
    snprintf(buf, sizeof(buf), settings.use24h ? "%02d:%02d" : "%d:%02d", hour, now.tm_min);
    int w = textWidth(FONT_CLOCK_XL, buf, -8);
    text(FONT_CLOCK_XL, (XL_X0 + XL_X1) / 2 - w / 2, 347, buf, EPD_BLACK, -8);
  }

  rule(XL_X0, 403, XL_X1 - XL_X0, 3);
  snprintf(buf, sizeof(buf), "%d %s", now.tm_mday, MONTHS_FULL[now.tm_mon]);
  text(FONT_DATE_XL, XL_X0, 448, buf);
  snprintf(buf, sizeof(buf), "%d", now.tm_year + 1900);
  textRight(FONT_YEAR_XL, XL_X1, 448, buf);
}

// ---------------------------------------------------------------------------
// Full-screen analog face: a track of ticks around the screen edges, thin 12 and 6, the date
// under the 12 and the weather above the 6. All black, because the every-minute refresh covers
// the whole screen (the hands sweep across it).
// ---------------------------------------------------------------------------
constexpr int FACE_CX = 400, FACE_CY = 240;
constexpr float FACE_HW = 386, FACE_HH = 226;  // outer edge of the ticks (half width/height)

// Distance from the centre to a rectangle of half size hw x hh along angle a (0 = up).
static float rectDist(float a, float hw, float hh) {
  float dx = fabsf(sinf(a)), dy = fabsf(cosf(a));
  return fminf(dx > 0.001f ? hw / dx : 1e6f, dy > 0.001f ? hh / dy : 1e6f);
}

// Hand with a background-coloured rim, so it stays readable where it crosses the text.
static void faceHand(float a, float len, float tail, float width) {
  spoke(FACE_CX, FACE_CY, a + PI, 0, tail, width + 6, EPD_WHITE);
  spoke(FACE_CX, FACE_CY, a, 0, len, width + 6, EPD_WHITE);
  spoke(FACE_CX, FACE_CY, a + PI, 0, tail, width, EPD_BLACK);
  spoke(FACE_CX, FACE_CY, a, 0, len, width, EPD_BLACK);
}

static void renderFace(const tm &now) {
  // Ticks run from the outer rectangle to an inner one, so they grow longer toward the corners
  for (int i = 0; i < 60; i++) {
    if (i <= 2 || i >= 58 || (i >= 28 && i <= 32)) continue;  // gaps for the 12 and 6
    float a = i * PI / 30;
    bool hour = i % 5 == 0;
    float depth = hour ? 34 : 16;
    float r1 = rectDist(a, FACE_HW, FACE_HH);
    float r0 = rectDist(a, FACE_HW - depth, FACE_HH - depth);
    spoke(FACE_CX, FACE_CY, a, r0, r1, hour ? 6 : 2, EPD_BLACK);
  }

  textCenter(FONT_FACE_NUM, FACE_CX, 26 + FONT_FACE_NUM.capHeight, "12");
  textCenter(FONT_FACE_NUM, FACE_CX, 480 - 26, "6");

  // Date: calendar icon + "WED 07"
  char buf[24];
  snprintf(buf, sizeof(buf), "%s %02d", DAYS3[now.tm_wday], now.tm_mday);
  int w = 30 + 10 + textWidth(FONT_FACE_INFO, buf, 1);
  int x = FACE_CX - w / 2, y = 160;
  icon(ICON_CALENDAR_TODAY_30, x + 15, y - FONT_FACE_INFO.capHeight / 2);
  text(FONT_FACE_INFO, x + 40, y, buf, EPD_BLACK, 1);

  // Weather: icon + temperature
  if (weather.valid) {
    snprintf(buf, sizeof(buf), "%.0f" DEG, weather.temp);
    w = 30 + 10 + textWidth(FONT_FACE_INFO, buf, 1);
    x = FACE_CX - w / 2, y = 480 - 26 - FONT_FACE_NUM.capHeight - 34;
    icon(weatherIcon(weather.code, weather.isDay, false), x + 15, y - FONT_FACE_INFO.capHeight / 2);
    text(FONT_FACE_INFO, x + 40, y, buf, EPD_BLACK, 1);
  }

  float minA = now.tm_min * PI / 30;
  float hourA = ((now.tm_hour % 12) + now.tm_min / 60.0f) * PI / 6;
  faceHand(hourA, 130, 18, 18);
  faceHand(minA, 196, 22, 10);
  canvas.fillCircle(FACE_CX, FACE_CY, 14, EPD_BLACK);
  canvas.fillCircle(FACE_CX, FACE_CY, 4, EPD_WHITE);
}

static uint8_t activeShow();

static void render(const tm &now, const Status &st, bool dark) {
  canvas.fillScreen(EPD_WHITE);
  if (activeShow() == SHOW_SOLAR) {
    solarRender(now);
  } else if (settings.clockStyle == CLOCK_FACE) {
    renderFace(now);
  } else if (settings.layout == LAYOUT_CLOCK) {
    renderClockOnly(now);
  } else {
    drawDateLine(now, st);
    drawClock(now);
    drawCurrentWeather();
    drawRightColumn(st);
  }
  if (dark) {
    // White on black: flip the black plane; red pixels keep their bit set so they stay red
    for (size_t i = 0; i < sizeof(canvas.black); i++) canvas.black[i] = ~canvas.black[i] | canvas.red[i];
  }
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
// What the last full refresh drew, to know when a fast clock update isn't enough
static bool shownDark = false, shownPm = false;
static uint8_t shownLayout = LAYOUT_WEATHER, shownStyle = CLOCK_DIGITAL;
static uint8_t shownShow = 0xFF;  // plugin of the last full refresh (0xFF: nothing drawn yet)
static uint32_t shownPhoto = 0;   // version of the photo on screen
static bool repaintPhoto = false;

void uiBegin() { epd.begin(); }

void uiRepaintNext() { repaintPhoto = true; }

// The plugin to show. Photo falls back to the clock until a photo has been uploaded.
static uint8_t activeShow() {
  if (settings.show >= SHOW_COUNT) return SHOW_CLOCK;
  if (settings.show == SHOW_PHOTO && photoVersion() == 0) return SHOW_CLOCK;
  return settings.show;
}

void uiShowFull(const tm &now, const Status &st) {
  uint8_t show = activeShow();
  if (show == SHOW_PHOTO) {
    // The panel keeps the photo without power, so only redraw when it changed or was asked for
    uint32_t v = photoVersion();
    if (shownShow == SHOW_PHOTO && v == shownPhoto && !repaintPhoto) return;
    repaintPhoto = false;
    if (photoLoad(canvas.black, canvas.red)) {
      shownShow = SHOW_PHOTO;
      shownPhoto = v;
      epd.showFull(canvas);
      return;
    }
    show = SHOW_CLOCK;
  }
  shownShow = show;
  shownPhoto = 0;
  shownDark = isDark(now);
  shownPm = now.tm_hour >= 12;
  shownLayout = settings.layout;
  shownStyle = settings.clockStyle;
  render(now, st, shownDark);
  epd.showFull(canvas);
}

void uiShowClock(const tm &now, const Status &st) {
  uint8_t show = activeShow();
  if (show != shownShow) {  // switched plugin
    uiShowFull(now, st);
    return;
  }
  if (show == SHOW_PHOTO) {
    if (photoVersion() != shownPhoto) uiShowFull(now, st);
    return;
  }
  if (show == SHOW_SOLAR) {
    // Positions move slowly: they're redrawn with the regular colour refresh (every 30 min)
    if (isDark(now) != shownDark) uiShowFull(now, st);
    return;
  }
  // Clock: repaint everything when the theme flips (Auto at sunrise/sunset), the layout
  // changed, or, in the clock-only layout, AM/PM changed (it's outside the fast-refresh area)
  bool pmChanged = settings.layout == LAYOUT_CLOCK && settings.clockStyle == CLOCK_DIGITAL &&
                   !settings.use24h && (now.tm_hour >= 12) != shownPm;
  if (isDark(now) != shownDark || settings.layout != shownLayout || settings.clockStyle != shownStyle ||
      pmChanged) {
    uiShowFull(now, st);
    return;
  }
  render(now, st, shownDark);  // only the clock area is sent to the panel
  if (shownStyle == CLOCK_FACE) {
    epd.showPartial(canvas, 0, 0, Canvas3C::W, Canvas3C::H);
  } else if (shownLayout == LAYOUT_CLOCK) {
    epd.showPartial(canvas, XL_CLOCK_X, XL_CLOCK_Y, XL_CLOCK_W, XL_CLOCK_H);
  } else {
    epd.showPartial(canvas, CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H);
  }
}

void uiShowSetup(const char *apName) {
  canvas.fillScreen(EPD_WHITE);
  text(FONT_MONO, 40, 70, "WI-FI SETUP", EPD_RED, 2);
  rule(40, 84, 720, 3);
  text(FONT_TITLE, 40, 140, "1. On your phone, join the Wi-Fi network:");
  text(FONT_TITLE, 70, 176, apName);
  text(FONT_TITLE, 40, 226, "2. A setup page opens (or visit http://192.168.4.1)");
  text(FONT_TITLE, 40, 266, "3. Pick your home Wi-Fi and enter its password");
  text(FONT_TITLE, 40, 326, "Then set your city at http://eink-clock.local");
  epd.showFull(canvas);
}

void uiScreenshot(const std::function<void(const uint8_t *, size_t)> &write) {
  constexpr int ROW = Canvas3C::W / 2;  // 4 bits per pixel; 400 bytes is already a multiple of 4
  uint8_t hdr[14 + 40 + 16 * 4] = {0};
  auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) hdr[at + i] = v >> (8 * i); };
  hdr[0] = 'B'; hdr[1] = 'M';
  put32(2, UI_BMP_SIZE);
  put32(10, sizeof(hdr));                 // pixel data offset
  put32(14, 40);                          // info header size
  put32(18, Canvas3C::W);
  put32(22, Canvas3C::H);                 // positive = rows stored bottom-up
  hdr[26] = 1; hdr[28] = 4;               // 1 plane, 4 bits per pixel
  put32(34, ROW * Canvas3C::H);
  // colours used: 0 = all 16, so readers find the pixels right after the full palette
  // Palette (B, G, R, 0): paper, ink, red as they look on the panel
  const uint8_t pal[3][4] = {{221, 227, 228, 0}, {30, 28, 28, 0}, {34, 40, 178, 0}};
  memcpy(hdr + 54, pal, sizeof(pal));
  write(hdr, sizeof(hdr));

  uint8_t row[ROW];
  for (int y = Canvas3C::H - 1; y >= 0; y--) {
    for (int x = 0; x < Canvas3C::W; x += 2) {
      uint8_t px[2];
      for (int k = 0; k < 2; k++) {
        size_t i = y * Canvas3C::ROW + (x + k) / 8;
        uint8_t bit = 0x80 >> ((x + k) & 7);
        px[k] = (canvas.red[i] & bit) ? 2 : (canvas.black[i] & bit) ? 0 : 1;
      }
      row[x / 2] = (px[0] << 4) | px[1];
    }
    write(row, ROW);
  }
}
