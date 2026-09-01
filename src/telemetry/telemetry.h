// fR3k v3 telemetry ring.
//
// 5-second samples of heap / largest block / sats / fix age / captures /
// day-night flag. 96 records per ring = 5 minutes. Rotated daily to a
// fixed 16 KiB file under /0N3P0rK/telemetry/YYYY-MM-DD.bin. Read in
// place from the STATUS page. No dynamic allocation - the ring is a
// static struct array so we don't trade telemetry for heap pressure.
#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace Telemetry {

struct Record {
    uint32_t tMs;       // millis() at sample time (low 32 bits)
    uint16_t heap;      // ESP.getFreeHeap()
    uint16_t largest;   // heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)
    uint8_t  sat;       // GpsService::snapshot().satellites (0..255)
    uint8_t  fixAge;    // fix age in seconds (cap 255 -> "stale")
    uint8_t  captures;  // Hc22000::pairCount() low byte
    uint8_t  dayNight;  // 0=night, 1=day, 2=dusk
    uint8_t  reserved;  // explicit padding so sizeof == 16 on all arches
};
static_assert(sizeof(Record) == 16, "Telemetry::Record must be 16 bytes");

static constexpr uint8_t  RING_N        = 96;     // 5 min @ 5 s sample
static constexpr uint32_t SAMPLE_MS    = 5000UL;
static constexpr size_t   DAILY_BYTES  = (size_t)RING_N * sizeof(Record);
static constexpr const char* DIR_TELEMETRY = "/0N3P0rK/telemetry";

// Begin: allocate the ring, mount the directory, set first sample.
void begin();

// Pull one record at the back of the ring. back=0 is the most recent,
// back=1 is the sample before that, etc. out is filled with a copy of
// the record (the on-disk byte is unchanged). Returns false if back >=
// the number of valid records in the ring (i.e. ring not yet warm).
bool getLast(uint8_t back, Record& out);

// Hook for the main loop. Records one sample every SAMPLE_MS. SD writes
// are batched per-record and gated by Storage::available(); if the SD
// is missing the sample still lives in the in-memory ring.
void sample();

// True when at least one record has been written to disk for today.
bool hasFile();

// Absolute path to today's telemetry file (e.g. "/0N3P0rK/telemetry/2026-09-01.bin").
// Output buf must hold at least 40 bytes.
void todayPath(char* out, size_t len);

}  // namespace Telemetry