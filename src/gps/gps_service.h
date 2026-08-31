#pragma once

#include <stdint.h>
#include <limits.h>

struct GpsSnapshot {
    bool enabled;
    bool fix;
    bool timeValid;
    bool courseValid;
    uint32_t baud;
    uint32_t fixAgeMs;
    uint32_t charsProcessed;
    double latitude;
    double longitude;
    double altitudeM;
    double speedKph;
    double courseDeg;
    double hdop;
    uint32_t satellites;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

namespace GpsService {

static constexpr int RX_PIN = 15;  // Cardputer Mesh Cap GPS_TX -> host RX
static constexpr int TX_PIN = 13;  // Cardputer Mesh Cap GPS_RX -> host TX
static constexpr uint32_t BAUD_FAST = 115200;
static constexpr uint32_t BAUD_SLOW = 9600;

void begin();
void loop();
void restart();
GpsSnapshot snapshot();
const char* cardinal(double courseDeg);
void requestClockSync();
bool clockSynced();
const char* logPath();

}  // namespace GpsService
