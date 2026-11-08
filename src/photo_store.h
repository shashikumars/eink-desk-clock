#pragma once
#include <Arduino.h>

// Keeps one uploaded photo in the flash data partition (128 KB, survives power cuts and
// firmware updates). The photo is the panel's two planes, exactly as Canvas3C holds them:
// 48000 bytes black (1 = white) then 48000 bytes red (1 = red).

constexpr size_t PHOTO_PLANE = 800 / 8 * 480;
constexpr size_t PHOTO_BYTES = 2 * PHOTO_PLANE;

// Version of the stored photo (changes on every upload), or 0 if there is none.
uint32_t photoVersion();
// Copies the stored photo into the two planes. Returns false if there is none.
bool photoLoad(uint8_t *black, uint8_t *red);

// Upload in pieces: begin, write the bytes in order, then finish (which checks the size and
// makes the photo current). A failed or partial upload leaves no photo stored.
bool photoUploadBegin();
bool photoUploadWrite(const uint8_t *data, size_t len);
bool photoUploadFinish();
