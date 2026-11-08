#pragma once
// Drawing helpers shared by the screen plugins (clock, photo, solar system...).
// They draw into the one 800 x 480 canvas that ui.cpp sends to the panel.
#include "epd.h"
#include "ui_fonts.h"
#include "ui_icons.h"

Canvas3C &uiCanvas();

// Text with its baseline at y; spacing adds px between characters. Returns the width drawn.
int text(const UiFont &f, int x, int y, const char *s, uint16_t color = EPD_BLACK, int spacing = 0);
int textWidth(const UiFont &f, const char *s, int spacing = 0);
void textRight(const UiFont &f, int xRight, int y, const char *s, uint16_t color = EPD_BLACK, int spacing = 0);
void textCenter(const UiFont &f, int cx, int y, const char *s, int spacing = 0);
void icon(const UiIcon &ic, int cx, int cy, uint16_t color = EPD_BLACK);
void rule(int x, int y, int w, int h);
// Thick line with a rounded end, from radius r0 to r1 along angle a (radians, 0 = up).
void spoke(int cx, int cy, float a, float r0, float r1, float width, uint16_t color = EPD_BLACK);

// UTF-8 degree sign and middle dot, written as escapes so file encoding can't break them
#define DEG "\xC2\xB0"
#define DOT "\xC2\xB7"
