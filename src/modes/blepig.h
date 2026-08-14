// BLE advertise lab — raw Apple / Windows / Android pairing frames.
// Own devices only.
#pragma once

#include <Arduino.h>
#include <M5Unified.h>

class BlePigMode {
public:
    enum class Family : uint8_t { APPLE = 0, WIN = 1, DROID = 2, MIX = 3 };

    static void start();
    static void stop();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isRunning() { return running; }
    static uint32_t getBursts() { return bursts; }
    static void getStatusLine(char* out, size_t len);
    static char lastName[24];
    static Family family;

private:
    static bool running;
    static uint32_t bursts;
    static uint32_t lastBurstMs;
    static uint32_t sessionStart;
};
