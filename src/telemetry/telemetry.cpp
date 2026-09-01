// fR3k v3 telemetry ring implementation.
// In-memory ring of 96 records + daily-rotated 16 KiB file on the SD.
// SD write happens once per new sample (16 bytes per 5 s = ~30 KiB/day,
// well under any SD wear budget).
#include "telemetry.h"
#include "../core/config.h"
#include "../gps/gps_service.h"
#include "../storage/littlefs_ops.h"
#include "../cap/hc22000.h"
#include "../piglet/avatar.h"
#include <SD.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

namespace Telemetry {

static Record s_ring[RING_N] = {};
static uint8_t s_head = 0;   // next write slot
static uint8_t s_count = 0;  // records currently valid (caps at RING_N)
static uint32_t s_lastSampleMs = 0;
static char s_todayPath[48] = "";
static bool s_wroteToday = false;

void todayPath(char* out, size_t len) {
    if (!out || len == 0) return;
    if (s_todayPath[0]) {
        strncpy(out, s_todayPath, len - 1);
        out[len - 1] = '\0';
        return;
    }
    // No valid RTC yet - fall back to "no-filename" so the STATUS page
    // can still show "TELEMETRY" without crashing on a 0-byte buffer.
    snprintf(out, len, "%s/unknown.bin", DIR_TELEMETRY);
}

static void recomputePath() {
    struct timeval tv;
    if (gettimeofday(&tv, nullptr) != 0) {
        s_todayPath[0] = '\0';
        return;
    }
    time_t t = (time_t)tv.tv_sec;
    struct tm tm;
    if (!localtime_r(&t, &tm)) {
        s_todayPath[0] = '\0';
        return;
    }
    snprintf(s_todayPath, sizeof(s_todayPath),
             "%s/%04d-%02d-%02d.bin", DIR_TELEMETRY,
             (int)(tm.tm_year + 1900), (int)(tm.tm_mon + 1), (int)tm.tm_mday);
}

void begin() {
    s_head = 0;
    s_count = 0;
    s_lastSampleMs = 0;
    s_wroteToday = false;
    if (Storage::available()) {
        Storage::ensureDir(DIR_TELEMETRY);
    }
    recomputePath();
}

static uint8_t classifyDayNight() {
    // Avatar::isNightTime() returns true after the dusk midpoint, false
    // before the dawn midpoint. Map: 1=day, 0=night, 2=dusk-or-dawn
    // (anything within ~30 min of the midpoints is "twilight"). For the
    // STATUS sparkline we only care about light-vs-dark so 1=day vs
    // 0=night is enough; twilight counts as night so the trend is
    // biased toward " we're burning RAM in the dark ".
    return Avatar::isNightTime() ? 0 : 1;
}

static uint8_t fixAgeSeconds() {
    const GpsSnapshot s = GpsService::snapshot();
    if (!s.enabled || !s.fix) return 255;
    if (s.fixAgeMs == UINT32_MAX) return 255;
    uint32_t sec = s.fixAgeMs / 1000;
    if (sec > 254) sec = 254;
    return (uint8_t)sec;
}

static uint16_t pairCount16() {
    uint32_t n = Hc22000::pairCount();
    if (n > 0xFFFF) n = 0xFFFF;
    return (uint16_t)n;
}

void sample() {
    const uint32_t now = millis();
    if (s_lastSampleMs != 0 && (now - s_lastSampleMs) < SAMPLE_MS) return;
    s_lastSampleMs = now;

    recomputePath();
    Record r;
    r.tMs = now;
    r.heap = (uint16_t)(ESP.getFreeHeap() & 0xFFFF);
    r.largest = (uint16_t)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) & 0xFFFF);
    r.sat = (uint8_t)(GpsService::snapshot().satellites & 0xFF);
    r.fixAge = fixAgeSeconds();
    r.captures = (uint8_t)(pairCount16() & 0xFF);
    r.dayNight = classifyDayNight();
    r.reserved = 0;

    s_ring[s_head] = r;
    s_head = (uint8_t)((s_head + 1) % RING_N);
    if (s_count < RING_N) s_count++;

    // Append to today's file - no atomicity concern, SD card power-loss
    // tolerance is at least 512 B so a half-written 16 B record either
    // landed or didn't. The next sample picks up where this one stopped.
    if (!Storage::available() || !s_todayPath[0]) return;
    if (!SD.exists(s_todayPath)) {
        // First write today - open in CREATE mode so SD library handles
        // truncation. Subsequent writes use FILE_APPEND for efficiency.
        File f = SD.open(s_todayPath, FILE_WRITE);
        if (!f) return;
        f.write((const uint8_t*)&r, sizeof(r));
        f.close();
        s_wroteToday = true;
        return;
    }
    File f = SD.open(s_todayPath, FILE_APPEND);
    if (!f) return;
    f.write((const uint8_t*)&r, sizeof(r));
    f.close();
    s_wroteToday = true;
}

bool getLast(uint8_t back, Record& out) {
    if (back >= s_count) return false;
    // Ring is most-recent at (s_head - 1 - back) mod RING_N.
    int idx = (int)s_head - 1 - (int)back;
    while (idx < 0) idx += RING_N;
    out = s_ring[idx];
    return true;
}

bool hasFile() { return s_wroteToday; }

}  // namespace Telemetry