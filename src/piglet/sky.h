#pragma once
// Sky layer: day/night gradient, moon, stars. Weather clouds stay in weather.cpp.
#include <Arduino.h>
#include <M5Unified.h>
#include <stdint.h>
#include <stddef.h>

namespace Sky {

void begin();
void drawBackdrop(M5Canvas& canvas);  // gradient + moon + static twinkles
void updateStars();
void drawStars(M5Canvas& canvas);

uint16_t topColor();                  // for top bar / blends
bool isNight();
void getHud(char* out, size_t len);

}  // namespace Sky
