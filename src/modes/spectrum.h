#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include <stdint.h>

// 2.4 GHz analyzer: lobes + waterfall + lock + clients + kick.
// Own / authorized networks only.

namespace SpectrumMode {

void start();
void stop();
void update();
void draw(M5Canvas& canvas);
bool isRunning();
void getStatusLine(char* out, size_t n);

}  // namespace SpectrumMode
