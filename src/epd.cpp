#include "epd.h"
#include "config.h"
#include <SPI.h>

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------
void Canvas3C::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || y < 0 || x >= W || y >= H) return;
  size_t i = y * ROW + x / 8;
  uint8_t bit = 0x80 >> (x & 7);
  if (color == EPD_RED && mono) color = EPD_BLACK;
  if (color == EPD_WHITE) {
    black[i] |= bit;
    red[i] &= ~bit;
  } else if (color == EPD_RED) {
    black[i] |= bit;
    red[i] |= bit;
  } else {
    black[i] &= ~bit;
    red[i] &= ~bit;
  }
}

void Canvas3C::fillScreen(uint16_t color) {
  if (color == EPD_RED && mono) color = EPD_BLACK;
  memset(black, color == EPD_BLACK ? 0x00 : 0xFF, sizeof(black));
  memset(red, color == EPD_RED ? 0xFF : 0x00, sizeof(red));
}

// ---------------------------------------------------------------------------
// Panel driver
// ---------------------------------------------------------------------------
static const SPISettings EPD_SPI(4000000, MSBFIRST, SPI_MODE0);

void Epd7in5b::begin() {
  pinMode(PIN_EPD_PWR, OUTPUT);
  // Weakest drive (~5 mA) is plenty for the HAT's power switch, and keeps the pin safe
  // if the PWR wire lands on a wrong pin such as GND.
  gpio_set_drive_capability((gpio_num_t)PIN_EPD_PWR, GPIO_DRIVE_CAP_0);
  digitalWrite(PIN_EPD_PWR, HIGH);  // switches the HAT on (Rev 2.3+ needs PWR high)
  delay(50);
  pinMode(PIN_EPD_CS, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  // Pull-up so an unconnected display reads "idle" instead of stalling on busy timeouts
  // (lets the board be tested on its own). The HAT drives this pin when connected.
  pinMode(PIN_EPD_BUSY, INPUT_PULLUP);
  digitalWrite(PIN_EPD_CS, HIGH);
  SPI.begin(PIN_EPD_SCK, PIN_EPD_MISO, PIN_EPD_MOSI, PIN_EPD_CS);
  memset(shown, 0xFF, sizeof(shown));
  selfTest();
}

// Logs whether the panel is wired up and answering, to tell wiring faults from software ones.
void Epd7in5b::selfTest() {
  // A connected BUSY line is driven by the HAT, so it reads the same with either pull.
  pinMode(PIN_EPD_BUSY, INPUT_PULLDOWN);
  delay(2);
  int withDown = digitalRead(PIN_EPD_BUSY);
  pinMode(PIN_EPD_BUSY, INPUT_PULLUP);
  delay(2);
  int withUp = digitalRead(PIN_EPD_BUSY);
  if (withDown == LOW && withUp == HIGH) {
    Serial.println("Display check: BUSY wire is floating (not connected, or HAT has no power: "
                   "check BUSY, VCC and GND)");
  } else {
    Serial.printf("Display check: BUSY wire connected (reads %s)\n", withUp ? "HIGH" : "LOW");
  }

  // Powering the panel on makes BUSY go LOW for a moment if the panel is listening.
  reset();
  command(0x04);
  uint32_t start = millis();
  delay(1);
  bool sawBusy = false, released = false;
  while (millis() - start < 2000) {
    if (digitalRead(PIN_EPD_BUSY) == LOW) {
      sawBusy = true;
    } else if (sawBusy) {
      released = true;
      break;
    }
    delay(1);
  }
  if (sawBusy && !released) {
    Serial.println("Display check: BUSY stays LOW (BUSY wire on a GND pin, or the panel is stuck); "
                   "pausing display updates so the website stays available");
    pauseUpdates();
    return;
  }
  if (released) {
    Serial.printf("Display check: panel answered power-on in %lu ms (OK)\n",
                  (unsigned long)(millis() - start));
  } else {
    Serial.println("Display check: panel did not answer power-on (check CS, CLK, DIN, DC, RST "
                   "wires and the ribbon cables)");
  }
  command(0x02);  // power off again
  waitIdle();
}

// After the panel stops responding, skip drawing for a while: every refresh would otherwise
// block for the full BUSY timeout and the website couldn't answer.
static uint32_t pausedUntil = 0;
static bool paused() { return pausedUntil && (int32_t)(pausedUntil - millis()) > 0; }

void Epd7in5b::pauseUpdates() { pausedUntil = (millis() + 5 * 60 * 1000) | 1; }

void Epd7in5b::reset() {
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(200);
  digitalWrite(PIN_EPD_RST, LOW);
  delay(5);
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(200);
}

void Epd7in5b::command(uint8_t c) {
  SPI.beginTransaction(EPD_SPI);
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(c);
  digitalWrite(PIN_EPD_CS, HIGH);
  SPI.endTransaction();
}

void Epd7in5b::data(uint8_t d) { dataBytes(&d, 1); }

void Epd7in5b::dataList(std::initializer_list<uint8_t> bytes) {
  for (uint8_t b : bytes) data(b);
}

void Epd7in5b::dataBytes(const uint8_t *buf, size_t len) {
  SPI.beginTransaction(EPD_SPI);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.writeBytes(buf, len);
  digitalWrite(PIN_EPD_CS, HIGH);
  SPI.endTransaction();
}

// BUSY is LOW while the panel is working.
void Epd7in5b::waitIdle() {
  uint32_t start = millis();
  while (digitalRead(PIN_EPD_BUSY) == LOW) {
    if (millis() - start > 40000) {  // a full colour refresh takes up to ~26 s
      Serial.println("Display busy timeout: check wiring. Pausing display updates for 5 minutes");
      pauseUpdates();
      break;
    }
    delay(10);
  }
  delay(20);
}

void Epd7in5b::turnOn() {
  uint32_t start = millis();
  command(0x12);  // display refresh
  delay(10);
  waitIdle();
  // A real refresh takes ~1.5 s (partial) or 16-26 s (colour). Near 0 ms means the panel
  // isn't answering: check wiring and the ribbon cables.
  Serial.printf("Display refresh took %lu ms\n", (unsigned long)(millis() - start));
}

void Epd7in5b::sleep() {
  command(0x50);
  data(0xF7);
  command(0x02);  // power off
  waitIdle();
  command(0x07);  // deep sleep; the next init's reset wakes it
  data(0xA5);
}

void Epd7in5b::initFull() {
  reset();
  command(0x01);  // power setting
  dataList({0x07, 0x07, 0x3F, 0x3F});
  command(0x06);  // booster soft start
  dataList({0x17, 0x17, 0x28, 0x17});
  command(0x04);  // power on
  delay(100);
  waitIdle();
  command(0x00);  // panel setting: black/white/red, waveform from OTP
  data(0x0F);
  command(0x61);  // resolution 800 x 480
  dataList({0x03, 0x20, 0x01, 0xE0});
  command(0x15);
  data(0x00);
  command(0x50);  // VCOM and data interval
  dataList({0x11, 0x07});
  command(0x60);  // TCON
  data(0x22);
}

void Epd7in5b::initPartial() {
  reset();
  command(0x00);  // panel setting: black/white only, waveform from OTP
  data(0x1F);
  command(0x04);  // power on
  delay(100);
  waitIdle();
  command(0xE0);  // select the fast waveform
  data(0x02);
  command(0xE5);
  data(0x6E);
  command(0x50);
  dataList({0xA9, 0x07});
}

void Epd7in5b::showFull(const Canvas3C &c) {
  if (paused()) return;
  Serial.println("Full colour refresh...");
  initFull();
  command(0x10);
  dataBytes(c.black, sizeof(c.black));
  command(0x13);
  dataBytes(c.red, sizeof(c.red));
  turnOn();
  sleep();
  memcpy(shown, c.black, sizeof(shown));
}

void Epd7in5b::showPartial(const Canvas3C &c, int x, int y, int w, int h) {
  if (paused()) return;
  x &= ~7;
  w = (w + 7) & ~7;
  if (x + w > Canvas3C::W) w = Canvas3C::W - x;
  if (y + h > Canvas3C::H) h = Canvas3C::H - y;
  const int xb = x / 8, wb = w / 8;
  const int xEnd = x + w - 1, yEnd = y + h - 1;

  Serial.println("Clock partial refresh...");
  initPartial();
  command(0x91);  // partial mode in
  command(0x90);  // partial window
  const uint8_t win[] = {uint8_t(x >> 8), uint8_t(x),    uint8_t(xEnd >> 8), uint8_t(xEnd),
                         uint8_t(y >> 8), uint8_t(y),    uint8_t(yEnd >> 8), uint8_t(yEnd), 0x01};
  dataBytes(win, sizeof(win));

  // Old image = what is on the panel now, new image = the canvas. The controller
  // only moves pixels that differ, which is what makes this refresh fast.
  command(0x10);
  for (int row = y; row <= yEnd; row++) dataBytes(shown + row * Canvas3C::ROW + xb, wb);
  command(0x13);
  for (int row = y; row <= yEnd; row++) dataBytes(c.black + row * Canvas3C::ROW + xb, wb);
  turnOn();
  command(0x92);  // partial mode out
  sleep();

  for (int row = y; row <= yEnd; row++) {
    memcpy(shown + row * Canvas3C::ROW + xb, c.black + row * Canvas3C::ROW + xb, wb);
  }
}
