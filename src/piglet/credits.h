#pragma once
// Level-50 thank-you roll — cannot be skipped, ~10 seconds.
#include <Arduino.h>
#include <M5Unified.h>

namespace Credits {

void begin();                 // no-op
void start();                 // arm 10s sequence
bool isPlaying();
void update();                // advance lines
void draw(M5Canvas& canvas);  // full-screen overlay

}  // namespace Credits
