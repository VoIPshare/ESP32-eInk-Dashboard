#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>

struct SparseGFXfont {
  const uint8_t *bitmap;
  const GFXglyph *glyph;
  const uint32_t *charMap;
  uint16_t glyphCount;
  uint8_t yAdvance;
};
