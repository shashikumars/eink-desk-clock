#pragma once
#include <Adafruit_GFX.h>
#include <initializer_list>

// Waveshare 7.5" e-Paper (B) V2: 800x480, black/white/red, UC8179 controller.
// Command sequences follow Waveshare's official EPD_7in5b_V2.c (2024-08-07).

constexpr uint16_t EPD_WHITE = 0xFFFF;
constexpr uint16_t EPD_BLACK = 0x0000;
constexpr uint16_t EPD_RED   = 0xF800;

// Drawing surface with a black plane and a red plane, like the panel's own memory.
class Canvas3C : public Adafruit_GFX {
 public:
  static constexpr int W = 800, H = 480, ROW = W / 8;

  Canvas3C() : Adafruit_GFX(W, H) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;

  // When true, red is drawn as black (for battery mode, where only fast B/W refreshes are used).
  bool mono = false;

  uint8_t black[ROW * H];  // bit 1 = white, 0 = black
  uint8_t red[ROW * H];    // bit 1 = red
};

class Epd7in5b {
 public:
  void begin();
  // Full colour refresh of the whole screen. Slow (16-26 s) and flashes.
  void showFull(const Canvas3C &c);
  // Fast black/white partial refresh (~1.5 s) of one area. x and w are rounded to multiples of 8.
  // Red pixels inside the area are not updated, so keep red out of partial areas.
  void showPartial(const Canvas3C &c, int x, int y, int w, int h);

 private:
  void selfTest();
  void pauseUpdates();
  void reset();
  void command(uint8_t c);
  void data(uint8_t d);
  void dataBytes(const uint8_t *buf, size_t len);
  void dataList(std::initializer_list<uint8_t> bytes);
  void waitIdle();
  void turnOn();
  void sleep();
  void initFull();
  void initPartial();

  uint8_t shown[Canvas3C::ROW * Canvas3C::H];  // black plane currently on the panel
};
