#pragma once
#include <Arduino.h>
#include <M5Unified.h>

// USB Mass Storage: SD card as a PC disk. Idea from bmorcelli/Launcher.

namespace UsbSdMode {

void start();
void stop();
void update();
void draw(M5Canvas& canvas);
bool isRunning();
void getStatusLine(char* out, size_t n);

}  // namespace UsbSdMode
