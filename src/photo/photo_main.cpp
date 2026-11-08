// Photo test: shows src/photo/photo_data.h on the display once, then does nothing.
// Built only by the "photo" environment; the clock firmware doesn't include this file.
// Make a new image with tools/make-photo.cjs. Go back to the clock with the "usb" env.

#include <Arduino.h>
#include "../epd.h"
#include "photo_data.h"

static Canvas3C canvas;
static Epd7in5b epd;

void setup() {
  Serial.begin(115200);
  epd.begin();
  memcpy_P(canvas.black, PHOTO_BLACK, sizeof(canvas.black));
  memcpy_P(canvas.red, PHOTO_RED, sizeof(canvas.red));
  epd.showFull(canvas);
  Serial.println("Photo shown");
}

void loop() { delay(1000); }
