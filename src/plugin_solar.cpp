// Solar system plugin: real-time positions, computed on the clock (see astro.cpp).
//   Earth & Moon: Sun in the middle, Earth on its orbit with month marks, the Moon around
//                 Earth, plus Moon phase, next new/full moon, Sun distance and next season.
//   Planets:      all eight planets around the Sun, top-down, plus their distances from Earth.
// Orbits are drawn as circles and the planets' spacing is compressed so all eight fit; the
// directions (where each body is around the Sun) are real.

#include "plugin_solar.h"
#include "astro.h"
#include "settings.h"
#include "ui_draw.h"
#include <math.h>

// Diagram centre: beside the info panel, or mid-screen when true ellipses use the whole screen
static int CX = 246;
static constexpr int CY = 240;
static constexpr int P_X0 = 500, P_X1 = 772;  // info panel
static const char *DAYS_UP[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
static const char *MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                               "July",    "August",   "September", "October", "November", "December"};
static const char *MON_UP[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// Screen point at distance r along ecliptic angle a (radians, counter-clockwise from the right)
static int px(float r, float a) { return CX + lroundf(r * cosf(a)); }
static int py(float r, float a) { return CY - lroundf(r * sinf(a)); }

// Disc lit on the side facing (lx, ly) (screen directions), dark on the other, outlined.
static void shadedDisc(int cx, int cy, int r, float lx, float ly) {
  Canvas3C &c = uiCanvas();
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      float d2 = dx * dx + dy * dy;
      if (d2 > r * r + r) continue;
      bool edge = d2 > (r - 1.5f) * (r - 1.5f);
      bool lit = dx * lx + dy * ly >= 0;
      c.drawPixel(cx + dx, cy + dy, edge || !lit ? EPD_BLACK : EPD_WHITE);
    }
  }
}

// The Moon as seen from Earth for a given elongation (0 new, 180 full), lit side right when waxing.
static void moonPhaseDisc(int cx, int cy, int r, double elong) {
  Canvas3C &c = uiCanvas();
  float k = cosf(elong * M_PI / 180);
  for (int dy = -r; dy <= r; dy++) {
    float w = sqrtf(fmaxf(0, r * r - dy * dy));
    for (int dx = -r; dx <= r; dx++) {
      float d2 = dx * dx + dy * dy;
      if (d2 > r * r + r) continue;
      bool edge = d2 > (r - 1.5f) * (r - 1.5f);
      bool lit = elong <= 180 ? dx > k * w : dx < -k * w;
      c.drawPixel(cx + dx, cy + dy, edge || !lit ? EPD_BLACK : EPD_WHITE);
    }
  }
}

static void dottedCircle(int cx, int cy, int r, int stepDeg) {
  for (int d = 0; d < 360; d += stepDeg) {
    float a = d * M_PI / 180;
    uiCanvas().fillCircle(cx + lroundf(r * cosf(a)), cy - lroundf(r * sinf(a)), 1, EPD_BLACK);
  }
}

static float angleOf(Vec2 v) { return atan2f(v.y, v.x); }

// Where a heliocentric position (AU) lands on screen for an orbit drawn `radius` px across.
// Circles: on the circle, in the planet's real direction. True ellipses: the real orbit scaled
// so its semi-major axis is `radius`, which keeps its shape and puts the Sun at the focus.
static void toScreen(Planet p, double jd, Vec2 v, float radius, int &x, int &y) {
  if (settings.solarEllipses) {
    float s = radius / semiMajorAxis(p, jd);
    x = CX + lroundf(v.x * s);
    y = CY - lroundf(v.y * s);
  } else {
    float a = angleOf(v);
    x = px(radius, a);
    y = py(radius, a);
  }
}

static void drawOrbit(Planet p, double jd, float radius) {
  Canvas3C &c = uiCanvas();
  if (!settings.solarEllipses) {
    c.drawCircle(CX, CY, radius, EPD_BLACK);
    return;
  }
  int x0, y0, x1, y1;
  toScreen(p, jd, orbitPoint(p, jd, 0), radius, x0, y0);
  for (int i = 1; i <= 180; i++) {
    toScreen(p, jd, orbitPoint(p, jd, i * M_PI / 90), radius, x1, y1);
    c.drawLine(x0, y0, x1, y1, EPD_BLACK);
    x0 = x1;
    y0 = y1;
  }
}

// Local calendar date of a Julian day, as "10 Oct".
static void shortDate(double jd, char *buf, size_t n) {
  time_t t = (time_t)((jd - 2440587.5) * 86400);
  tm d;
  localtime_r(&t, &d);
  snprintf(buf, n, "%d %.3s", d.tm_mday, MONTHS[d.tm_mon]);
}

static void panelHeader(const tm &now, const char *title) {
  char buf[32];
  rule(P_X0 - 26, 20, 3, 448);
  text(FONT_MONO, P_X0, 37, title, EPD_RED, 2);
  rule(P_X0, 49, P_X1 - P_X0, 3);
  text(FONT_MONO_SMALL, P_X0, 78, DAYS_UP[now.tm_wday], EPD_BLACK, 2);
  snprintf(buf, sizeof(buf), "%d %s", now.tm_mday, MONTHS[now.tm_mon]);
  text(FONT_DATE_XL, P_X0, 117, buf);
}

// True-ellipse mode uses the whole screen for the diagram, with just the date in a corner.
static bool fullScreen() { return settings.solarEllipses; }

static void cornerDate(const tm &now) {
  char buf[40];
  snprintf(buf, sizeof(buf), "%s %d %s %d", DAYS_UP[now.tm_wday], now.tm_mday, MON_UP[now.tm_mon], now.tm_year + 1900);
  text(FONT_MONO_SMALL, 20, 30, buf, EPD_BLACK, 2);
}

static void panelFooter(const tm &now) {
  char buf[24];
  snprintf(buf, sizeof(buf), "UPD %02d:%02d", now.tm_hour, now.tm_min);
  text(FONT_MONO_FOOT, P_X0, 465, buf, EPD_BLACK, 1);
}

// ---------------------------------------------------------------------------
// Earth & Moon
// ---------------------------------------------------------------------------
static void drawEarthMoon(const tm &now, double jd) {
  const int R_ORBIT = fullScreen() ? 200 : 168, R_MOON = fullScreen() ? 52 : 44;
  Canvas3C &c = uiCanvas();

  // Earth's orbit with a mark where Earth is on the 1st of each month
  drawOrbit(EARTH, jd, R_ORBIT);
  for (int m = 0; m < 12; m++) {
    tm first = {};
    first.tm_year = now.tm_year;
    first.tm_mon = m;
    first.tm_mday = 1;
    first.tm_hour = 12;
    double jdm = julianDay(mktime(&first));
    int x, y;
    toScreen(EARTH, jd, planetPosition(EARTH, jdm), R_ORBIT, x, y);
    float a = atan2f(CY - y, x - CX), d = hypotf(x - CX, y - CY);
    spoke(CX, CY, -a + M_PI / 2, d - 6, d + 6, 2);
    textCenter(FONT_MONO_SMALL, px(d + 22, a), py(d + 22, a) + 5, MON_UP[m]);
  }

  // Sun
  c.fillCircle(CX, CY, 26, EPD_RED);
  textCenter(FONT_MONO_SMALL, CX, CY + 50, "SUN", 2);

  // Earth, lit on the side facing the Sun
  int ex, ey;
  toScreen(EARTH, jd, planetPosition(EARTH, jd), R_ORBIT, ex, ey);
  float ed = hypotf(ex - CX, ey - CY);
  float lx = (CX - ex) / ed, ly = (CY - ey) / ed;
  c.fillCircle(ex, ey, R_MOON + 9, EPD_WHITE);  // keep the orbit line out of the Earth-Moon system
  dottedCircle(ex, ey, R_MOON, 10);
  shadedDisc(ex, ey, 13, lx, ly);

  // Moon around Earth (geocentric longitude), also lit towards the Sun
  float ma = moonLongitude(jd) * M_PI / 180;
  int mx = ex + lroundf(R_MOON * cosf(ma)), my = ey - lroundf(R_MOON * sinf(ma));
  shadedDisc(mx, my, 7, lx, ly);

  if (fullScreen()) {
    cornerDate(now);
    return;
  }

  // Info panel
  panelHeader(now, "EARTH & MOON");
  rule(P_X0, 138, P_X1 - P_X0, 1);

  double elong = moonElongation(jd);
  char buf[48];
  moonPhaseDisc(P_X0 + 34, 188, 34, elong);
  text(FONT_TITLE, P_X0 + 82, 182, moonPhaseName(elong));
  snprintf(buf, sizeof(buf), "%.0f%% lit", moonIllumination(jd) * 100);
  text(FONT_BODY, P_X0 + 82, 206, buf);

  char d[16];
  shortDate(jd + daysUntilElongation(jd, 0), d, sizeof(d));
  text(FONT_BODY, P_X0, 254, "New moon");
  textRight(FONT_BODY, P_X1, 254, d);
  shortDate(jd + daysUntilElongation(jd, 180), d, sizeof(d));
  text(FONT_BODY, P_X0, 278, "Full moon");
  textRight(FONT_BODY, P_X1, 278, d);
  rule(P_X0, 296, P_X1 - P_X0, 1);

  Vec2 e = planetPosition(EARTH, jd);
  text(FONT_MONO_SMALL, P_X0, 322, "SUN TO EARTH", EPD_BLACK, 2);
  snprintf(buf, sizeof(buf), "%.1f million km", hypot(e.x, e.y) * 149.598);
  text(FONT_TITLE, P_X0, 346, buf);

  // Next equinox or solstice: the Sun's longitude reaching the next multiple of 90 degrees
  static const char *SEASONS[] = {"March equinox", "June solstice", "September equinox", "December solstice"};
  double sl = sunLongitude(jd);
  int next = ((int)(sl / 90) + 1) % 4;
  double days = fmod(next * 90 - sl + 360, 360) / 0.9856;
  text(FONT_MONO_SMALL, P_X0, 380, "NEXT SEASON", EPD_BLACK, 2);
  text(FONT_TITLE, P_X0, 404, SEASONS[next]);
  snprintf(buf, sizeof(buf), "in %.0f days", days);
  text(FONT_BODY, P_X0, 428, buf);

  panelFooter(now);
}

// ---------------------------------------------------------------------------
// Planets
// ---------------------------------------------------------------------------
static void drawPlanets(const tm &now, double jd) {
  // Compressed spacing (real orbits span 0.4 to 30 AU); the outermost stays clear of the panel
  static const int ORBIT[PLANET_COUNT] = {36, 58, 82, 106, 138, 164, 190, 212};
  const int LABEL_MAX_X = fullScreen() ? 796 : P_X0 - 34;
  const float scale = fullScreen() ? 226.0f / 212 : 1;  // bigger when it has the whole screen
  static const int SIZE[PLANET_COUNT] = {4, 6, 6, 5, 11, 9, 8, 8};
  Canvas3C &c = uiCanvas();

  for (int p = 0; p < PLANET_COUNT; p++) drawOrbit((Planet)p, jd, ORBIT[p] * scale);
  c.fillCircle(CX, CY, 17, EPD_RED);

  Vec2 earth = planetPosition(EARTH, jd);
  int X[PLANET_COUNT], Y[PLANET_COUNT];
  for (int p = 0; p < PLANET_COUNT; p++) {
    toScreen((Planet)p, jd, planetPosition((Planet)p, jd), ORBIT[p] * scale, X[p], Y[p]);
    int x = X[p], y = Y[p];
    c.fillCircle(x, y, SIZE[p] + 3, EPD_WHITE);  // gap in the orbit line around the planet
    if (p == EARTH) {
      float d = hypotf(x - CX, y - CY);
      float lx = (CX - x) / d, ly = (CY - y) / d;
      shadedDisc(x, y, SIZE[p], lx, ly);
    } else {
      c.fillCircle(x, y, SIZE[p], EPD_BLACK);
    }
    if (p == SATURN) c.drawFastHLine(x - SIZE[p] - 5, y, 2 * SIZE[p] + 11, EPD_BLACK);
  }

  // Names: try above, below, right, then left of each planet, and take the first spot that
  // covers no planet and no name already placed. Each sits on a white patch over the orbits.
  struct Box { int x0, y0, x1, y1; };
  Box placed[PLANET_COUNT];
  int nPlaced = 0;
  auto clear = [&](const Box &b) {
    if (b.x0 < 4 || b.x1 > LABEL_MAX_X || b.y0 < 4 || b.y1 > 476) return false;
    for (int q = 0; q < PLANET_COUNT; q++) {
      int r = SIZE[q] + 3;
      if (X[q] + r > b.x0 && X[q] - r < b.x1 && Y[q] + r > b.y0 && Y[q] - r < b.y1) return false;
    }
    for (int i = 0; i < nPlaced; i++) {
      if (placed[i].x1 > b.x0 && placed[i].x0 < b.x1 && placed[i].y1 > b.y0 && placed[i].y0 < b.y1) return false;
    }
    return true;
  };
  for (int p = 0; p < PLANET_COUNT; p++) {
    char name[12];
    int n = 0;
    for (const char *s = PLANET_NAMES[p]; *s && n < 11; s++) name[n++] = toupper(*s);
    name[n] = '\0';
    int w = textWidth(FONT_MONO_FOOT, name, 1) + 4, h = 14, r = SIZE[p] + 4;
    const int cand[4][2] = {{X[p] - w / 2, Y[p] - r - h}, {X[p] - w / 2, Y[p] + r},
                            {X[p] + r + 2, Y[p] - h / 2}, {X[p] - r - 2 - w, Y[p] - h / 2}};
    Box b = {cand[0][0], cand[0][1], cand[0][0] + w, cand[0][1] + h};
    for (const auto &k : cand) {
      Box t = {k[0], k[1], k[0] + w, k[1] + h};
      if (clear(t)) { b = t; break; }
    }
    placed[nPlaced++] = b;
    c.fillRect(b.x0, b.y0, w, h, EPD_WHITE);
    text(FONT_MONO_FOOT, b.x0 + 2, b.y0 + 11, name, EPD_BLACK, 1);
  }

  if (fullScreen()) {
    cornerDate(now);
    return;
  }

  // Info panel: distance of each planet from Earth
  panelHeader(now, "SOLAR SYSTEM");
  rule(P_X0, 138, P_X1 - P_X0, 1);
  text(FONT_MONO_SMALL, P_X0, 164, "DISTANCE FROM EARTH", EPD_BLACK, 2);
  int y = 196;
  char buf[32];
  for (int p = 0; p < PLANET_COUNT; p++) {
    if (p == EARTH) continue;
    Vec2 v = planetPosition((Planet)p, jd);
    double au = hypot(v.x - earth.x, v.y - earth.y);
    text(FONT_LABEL, P_X0, y, PLANET_NAMES[p]);
    if (au < 6.68) snprintf(buf, sizeof(buf), "%.0f million km", au * 149.598);  // up to ~1000
    else snprintf(buf, sizeof(buf), "%.2f billion km", au * 0.149598);
    textRight(FONT_BODY, P_X1, y, buf);
    y += 34;
  }
  double elong = moonElongation(jd);
  snprintf(buf, sizeof(buf), "Moon: %s", moonPhaseName(elong));
  rule(P_X0, y - 18, P_X1 - P_X0, 1);
  moonPhaseDisc(P_X0 + 10, y + 4, 10, elong);
  text(FONT_BODY, P_X0 + 28, y + 10, buf);

  panelFooter(now);
}

void solarRender(const tm &now) {
  double jd = julianDay(time(nullptr));
  CX = fullScreen() ? 400 : 246;
  if (settings.solarView == SOLAR_PLANETS) drawPlanets(now, jd);
  else drawEarthMoon(now, jd);
}
