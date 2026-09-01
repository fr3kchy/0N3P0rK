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
    // fR3k v3.0.4: per-band sat counts for the signal-quality panel.
    // satHigh  = SNR >= 35 dB (excellent), satMid  = 20-34 (usable),
    // satLow   = <20 (marginal). Total may exceed s.satellites if the
    // GPS engine reports stale entries; the panel clamps each band
    // to satellites.
    uint8_t satHigh;
    uint8_t satMid;
    uint8_t satLow;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    // fR3k v3.0.4: trip odometer. Accumulated distance in metres
    // since the last operator "R" reset. Driven by haversine over
    // successive valid fixes in GpsService::loop().
    double tripDistM;
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
// fR3k v3.0.4: trip odometer reset.
void resetTrip();
double getTripDistM();

}  // namespace GpsService
