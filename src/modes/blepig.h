// Thin BLE advertise burst — uses TUNE BLE knobs.
// Lab / own devices only.
#pragma once

#include <Arduino.h>
#include <M5Unified.h>

class BlePigMode {
public:
    static void start();
    static void stop();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isRunning() { return running; }
    static uint32_t getBursts() { return bursts; }
    static void getStatusLine(char* out, size_t len);
    static char lastName[20];

private:
    static bool running;
    static uint32_t bursts;
    static uint32_t lastBurstMs;
    static uint32_t sessionStart;
};
