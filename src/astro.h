#pragma once
#include <time.h>

// Approximate positions of the planets and the Moon, good to a fraction of a degree, which is
// far finer than the display can show. Planets: JPL "Keplerian elements for approximate
// positions of the major planets" (E M Standish), valid 1800-2050. Moon: the main terms of
// the lunar theory (as in Meeus, Astronomical Algorithms), about 0.3 degrees.

enum Planet { MERCURY, VENUS, EARTH, MARS, JUPITER, SATURN, URANUS, NEPTUNE, PLANET_COUNT };
extern const char *const PLANET_NAMES[PLANET_COUNT];

struct Vec2 { double x, y; };

double julianDay(time_t utc);
// Heliocentric position in the ecliptic plane, in AU (x towards the March equinox).
Vec2 planetPosition(Planet p, double jd);
// A point on the planet's elliptical orbit (Sun at the focus) at eccentric anomaly E (radians),
// in AU; stepping E from 0 to 2 pi traces the whole orbit.
Vec2 orbitPoint(Planet p, double jd, double E);
// Semi-major axis (the orbit's average radius), in AU.
double semiMajorAxis(Planet p, double jd);
// Geocentric ecliptic longitudes in degrees (0..360).
double sunLongitude(double jd);
double moonLongitude(double jd);
// Moon's angle from the Sun as seen from Earth: 0 new, 90 first quarter, 180 full, 270 last quarter.
double moonElongation(double jd);
// Fraction of the Moon's disc that is lit, 0..1.
double moonIllumination(double jd);
const char *moonPhaseName(double elongation);
// Days until the Moon's elongation next reaches the given angle (e.g. 180 for full moon).
double daysUntilElongation(double jd, double target);
