#include "gps_service.h"

#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>

#include "../core/config.h"
#include "../storage/littlefs_ops.h"
#include "../ui/display.h"

namespace GpsService {

static HardwareSerial s_uart(1);
static TinyGPSPlus s_parser;
static uint32_t s_baud = BAUD_FAST;
static uint32_t s_baudStartedMs = 0;
static uint32_t s_passedAtBaudStart = 0;
static uint32_t s_lastLogMs = 0;
static uint32_t s_lastClockSyncMs = 0;
// fR3k v3.0.4: trip odometer state. Distance accumulated in metres
// from the operator's last reset. Uses haversine on successive
// valid fixes in loop().
static bool s_tripHasPrev = false;
static double s_tripPrevLat = 0.0;
static double s_tripPrevLon = 0.0;
static double s_tripDistM = 0.0;
static bool s_started = false;
static bool s_hadFix = false;
static bool s_clockSynced = false;
static bool s_clockSyncRequested = false;

static void startUart(uint32_t baud) {
    if (s_started) s_uart.end();
    s_baud = baud;
    s_uart.begin(baud, SERIAL_8N1, RX_PIN, TX_PIN);
    s_started = true;
    s_baudStartedMs = millis();
    s_passedAtBaudStart = s_parser.passedChecksum();
    Serial.printf("[GPS] UART RX=%d TX=%d baud=%lu\n", RX_PIN, TX_PIN,
                  (unsigned long)baud);
}

static uint32_t configuredBaud() {
    const uint8_t mode = Config::gps().baudMode;
    if (mode == 1) return BAUD_FAST;
    if (mode == 2) return BAUD_SLOW;
    return 0;
}

void begin() {
    s_hadFix = false;
    s_clockSynced = false;
    s_clockSyncRequested = false;
    if (!Config::gps().enabled) return;
    const uint32_t forced = configuredBaud();
    startUart(forced ? forced : BAUD_FAST);
}

void restart() {
    if (s_started) {
        s_uart.end();
        s_started = false;
    }
    if (Config::gps().enabled) begin();
}

static int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    // Proleptic Gregorian calendar, days relative to Unix epoch (1970-01-01).
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
    const unsigned doy = (153 * shiftedMonth + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static time_t utcEpoch(const GpsSnapshot& s) {
    const int64_t days = daysFromCivil((int)s.year, s.month, s.day);
    return (time_t)(days * 86400LL + (int64_t)s.hour * 3600 +
                    (int64_t)s.minute * 60 + s.second);
}

static void maybeSyncClock(const GpsSnapshot& s) {
    if (!s.timeValid || (!Config::gps().syncUtc && !s_clockSyncRequested)) return;
    const uint32_t now = millis();
    if (!s_clockSyncRequested && s_clockSynced && now - s_lastClockSyncMs < 3600000UL) return;
    const time_t epoch = utcEpoch(s);
    if (epoch < 1700000000) return;
    struct timeval tv{epoch, 0};
    if (settimeofday(&tv, nullptr) == 0) {
        s_clockSynced = true;
        s_clockSyncRequested = false;
        s_lastClockSyncMs = now;
        Display::showToast("GPS UTC SYNC", 1300);
        Serial.printf("[GPS] UTC clock synced epoch=%lld\n", (long long)epoch);
    }
}

static void maybeLog(const GpsSnapshot& s) {
    if (!Config::gps().logging || !s.fix || s.fixAgeMs > 3000) return;
    if (!Storage::available()) {
        Config::gps().logging = false;
        Config::save();
        Display::showToast("GPS LOG NEEDS SD", 1800);
        return;
    }
    const uint32_t now = millis();
    if (now - s_lastLogMs < 2000) return;
    s_lastLogMs = now;

    char line[192];
    if (s.timeValid) {
        snprintf(line, sizeof(line),
                 "%04u-%02u-%02uT%02u:%02u:%02uZ,%.6f,%.6f,%.2f,%lu,%.2f,%.2f,%.2f\n",
                 (unsigned)s.year, (unsigned)s.month, (unsigned)s.day,
                 (unsigned)s.hour, (unsigned)s.minute, (unsigned)s.second,
                 s.latitude, s.longitude, s.altitudeM,
                 (unsigned long)s.satellites, s.speedKph, s.courseDeg, s.hdop);
    } else {
        snprintf(line, sizeof(line),
                 "uptime-%lu,%.6f,%.6f,%.2f,%lu,%.2f,%.2f,%.2f\n",
                 (unsigned long)now, s.latitude, s.longitude, s.altitudeM,
                 (unsigned long)s.satellites, s.speedKph, s.courseDeg, s.hdop);
    }
    if (!Storage::appendGpsCsv(line)) {
        Config::gps().logging = false;
        Config::save();
        Display::showToast("GPS LOG SD ERROR", 1800);
    }
}

void loop() {
    if (!Config::gps().enabled) {
        if (s_started) {
            s_uart.end();
            s_started = false;
        }
        return;
    }
    if (!s_started) begin();

    uint16_t budget = 256;
    while (budget-- && s_uart.available() > 0) {
        const int c = s_uart.read();
        if (c >= 0) s_parser.encode((char)c);
    }

    if (configuredBaud() == 0 &&
        s_parser.passedChecksum() == s_passedAtBaudStart &&
        millis() - s_baudStartedMs >= 4000) {
        startUart(s_baud == BAUD_FAST ? BAUD_SLOW : BAUD_FAST);
    }

    const GpsSnapshot s = snapshot();
    if (s.fix != s_hadFix) {
        s_hadFix = s.fix;
        Display::showToast(s.fix ? "GPS FIX ACQUIRED" : "GPS FIX LOST", 1800);
    }
    // fR3k v3.0.4: trip odometer accumulator. Skips huge jumps
    // (>5 km between fixes) to avoid bad fixes inflating the
    // total; restarts the baseline when a fix is re-acquired.
    if (s.fix) {
        if (!s_tripHasPrev) {
            s_tripHasPrev = true;
            s_tripPrevLat = s.latitude;
            s_tripPrevLon = s.longitude;
        } else {
            const double R = 6371000.0;  // metres
            const double lat1 = s_tripPrevLat * (M_PI / 180.0);
            const double lat2 = s.latitude * (M_PI / 180.0);
            const double dLat = (s.latitude - s_tripPrevLat) * (M_PI / 180.0);
            const double dLon = (s.longitude - s_tripPrevLon) * (M_PI / 180.0);
            const double a = sin(dLat/2) * sin(dLat/2)
                           + cos(lat1) * cos(lat2) * sin(dLon/2) * sin(dLon/2);
            const double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
            const double dM = R * c;
            if (dM < 5000.0) s_tripDistM += dM;  // skip >5 km jumps
            s_tripPrevLat = s.latitude;
            s_tripPrevLon = s.longitude;
        }
    } else {
        s_tripHasPrev = false;
    }
    maybeSyncClock(s);
    maybeLog(s);
}

GpsSnapshot snapshot() {
    GpsSnapshot s{};
    s.enabled = Config::gps().enabled;
    s.baud = s_baud;
    s.charsProcessed = s_parser.charsProcessed();
    s.fixAgeMs = s_parser.location.isValid() ? s_parser.location.age() : UINT32_MAX;
    s.fix = s_parser.location.isValid() && s.fixAgeMs <= 5000;
    s.latitude = s_parser.location.isValid() ? s_parser.location.lat() : 0.0;
    s.longitude = s_parser.location.isValid() ? s_parser.location.lng() : 0.0;
    s.altitudeM = s_parser.altitude.isValid() ? s_parser.altitude.meters() : 0.0;
    s.speedKph = s_parser.speed.isValid() ? s_parser.speed.kmph() : 0.0;
    s.courseValid = s_parser.course.isValid();
    s.courseDeg = s.courseValid ? s_parser.course.deg() : 0.0;
    s.satellites = s_parser.satellites.isValid() ? s_parser.satellites.value() : 0;
    // fR3k v3.0.4: derive per-band sat counts from the global count.
    // TinyGPSPlus here doesn't expose per-sat SNR; we bucket the
    // total into quality bands so the signal panel has something
    // useful. >=8 sats = mostly high SNR; 5-7 = mix; <5 = low SNR.
    {
        const uint8_t n = (uint8_t)(s.satellites > 31 ? 31 : s.satellites);
        if (n >= 8) { s.satHigh = n; s.satMid = 0; s.satLow = 0; }
        else if (n >= 5) { s.satHigh = n - 4; s.satMid = 3; s.satLow = n - 7; }
        else if (n >= 1) { s.satHigh = 0; s.satMid = n > 1 ? 1 : 0; s.satLow = n; }
        else { s.satHigh = s.satMid = s.satLow = 0; }
    }
    s.hdop = s_parser.hdop.isValid() ? s_parser.hdop.hdop() : 0.0;
    s.tripDistM = s_tripDistM;  // fR3k v3.0.4: odometer copy-out
    s.timeValid = s_parser.date.isValid() && s_parser.time.isValid();
    if (s.timeValid) {
        s.year = s_parser.date.year();
        s.month = s_parser.date.month();
        s.day = s_parser.date.day();
        s.hour = s_parser.time.hour();
        s.minute = s_parser.time.minute();
        s.second = s_parser.time.second();
    }
    return s;
}

const char* cardinal(double deg) {
    static const char* const dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int idx = (int)((deg + 22.5) / 45.0) & 7;
    return dirs[idx];
}

void requestClockSync() { s_clockSyncRequested = true; }
bool clockSynced() { return s_clockSynced; }
const char* logPath() { return Storage::FILE_GPS_TRACK; }
// fR3k v3.0.4: trip odometer accessors.
void resetTrip() { s_tripDistM = 0.0; s_tripHasPrev = false; }
double getTripDistM() { return s_tripDistM; }

}  // namespace GpsService
