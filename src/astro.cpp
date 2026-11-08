#include "astro.h"
#include <math.h>

const char *const PLANET_NAMES[PLANET_COUNT] = {"Mercury", "Venus",  "Earth",  "Mars",
                                                "Jupiter", "Saturn", "Uranus", "Neptune"};

static constexpr double DEG = M_PI / 180;

static double norm360(double a) {
  a = fmod(a, 360);
  return a < 0 ? a + 360 : a;
}

double julianDay(time_t utc) { return utc / 86400.0 + 2440587.5; }

// a (AU), e, I, L, long. perihelion, long. ascending node (degrees), and their rates per century
struct Elements { double a, e, I, L, peri, node, da, de, dI, dL, dperi, dnode; };
static const Elements ELEMENTS[PLANET_COUNT] = {
    {0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593,
     0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081},
    {0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255,
     0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418},
    {1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0,
     0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0},
    {1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891,
     0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343},
    {5.20288700, 0.04838624, 1.30439695, 34.39644051, 14.72847983, 100.47390909,
     -0.00011607, -0.00013253, -0.00183714, 3034.74612775, 0.21252668, 0.20469106},
    {9.53667594, 0.05386179, 2.48599187, 49.95424423, 92.59887831, 113.66242448,
     -0.00125060, -0.00050991, 0.00193609, 1222.49362201, -0.41897216, -0.28867794},
    {19.18916464, 0.04725744, 0.77263783, 313.23810451, 170.95427630, 74.01692503,
     -0.00196176, -0.00004397, -0.00242939, 428.48202785, 0.40805281, 0.04240589},
    {30.06992276, 0.00859048, 1.77004347, -55.12002969, 44.96476227, 131.78422574,
     0.00026291, 0.00005105, 0.00035372, 218.45945325, -0.32241464, -0.00508664},
};

// The elements at a given date, with angles in radians.
struct Orbit { double a, e, I, w, N, M; };
static Orbit orbitAt(Planet p, double jd) {
  const Elements &k = ELEMENTS[p];
  double T = (jd - 2451545.0) / 36525;
  double L = k.L + k.dL * T, peri = k.peri + k.dperi * T, node = k.node + k.dnode * T;
  double M = norm360(L - peri);
  if (M > 180) M -= 360;
  return {k.a + k.da * T, k.e + k.de * T, (k.I + k.dI * T) * DEG, (peri - node) * DEG, node * DEG, M * DEG};
}

// Position on the orbit at eccentric anomaly E, rotated into the ecliptic plane.
static Vec2 onOrbit(const Orbit &o, double E) {
  double xp = o.a * (cos(E) - o.e), yp = o.a * sqrt(1 - o.e * o.e) * sin(E);
  double cw = cos(o.w), sw = sin(o.w), cN = cos(o.N), sN = sin(o.N), cI = cos(o.I);
  return {(cw * cN - sw * sN * cI) * xp + (-sw * cN - cw * sN * cI) * yp,
          (cw * sN + sw * cN * cI) * xp + (-sw * sN + cw * cN * cI) * yp};
}

Vec2 planetPosition(Planet p, double jd) {
  Orbit o = orbitAt(p, jd);
  // Kepler's equation
  double E = o.M + o.e * sin(o.M);
  for (int i = 0; i < 8; i++) E -= (E - o.e * sin(E) - o.M) / (1 - o.e * cos(E));
  return onOrbit(o, E);
}

Vec2 orbitPoint(Planet p, double jd, double E) { return onOrbit(orbitAt(p, jd), E); }

double semiMajorAxis(Planet p, double jd) { return orbitAt(p, jd).a; }

double sunLongitude(double jd) {
  Vec2 e = planetPosition(EARTH, jd);
  return norm360(atan2(-e.y, -e.x) / DEG);
}

double moonLongitude(double jd) {
  double d = jd - 2451545.0;
  double L = 218.316 + 13.176396 * d;       // mean longitude
  double Mm = (134.963 + 13.064993 * d) * DEG;  // Moon's mean anomaly
  double Ms = (357.529 + 0.98560028 * d) * DEG; // Sun's mean anomaly
  double D = (297.850 + 12.190749 * d) * DEG;   // mean elongation
  double F = (93.272 + 13.229350 * d) * DEG;    // argument of latitude
  return norm360(L + 6.289 * sin(Mm) + 1.274 * sin(2 * D - Mm) + 0.658 * sin(2 * D) +
                 0.214 * sin(2 * Mm) - 0.186 * sin(Ms) - 0.114 * sin(2 * F));
}

double moonElongation(double jd) { return norm360(moonLongitude(jd) - sunLongitude(jd)); }

double moonIllumination(double jd) { return (1 - cos(moonElongation(jd) * DEG)) / 2; }

const char *moonPhaseName(double e) {
  if (e < 6 || e >= 354) return "New moon";
  if (e < 84) return "Waxing crescent";
  if (e < 96) return "First quarter";
  if (e < 174) return "Waxing gibbous";
  if (e < 186) return "Full moon";
  if (e < 264) return "Waning gibbous";
  if (e < 276) return "Last quarter";
  return "Waning crescent";
}

double daysUntilElongation(double jd, double target) {
  // The elongation grows about 12.2 degrees a day; step in 1-hour slices until it passes target
  double prev = norm360(moonElongation(jd) - target);
  for (int h = 1; h <= 31 * 24; h++) {
    double cur = norm360(moonElongation(jd + h / 24.0) - target);
    if (cur < prev) return h / 24.0;  // wrapped through 0: just passed the target
    prev = cur;
  }
  return 0;
}
