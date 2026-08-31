#pragma once

#include <M5Unified.h>

namespace GpsMode {

void start();
void stop();
bool isRunning();
void update();
void draw(M5Canvas& canvas);
void getStatusLine(char* out, size_t len);

}  // namespace GpsMode
