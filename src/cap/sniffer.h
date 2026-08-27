// cap/sniffer.h
// Light: stay on current channel, web UI stays up, no deauth.
// Aggressive (board button only): hop 1-13, kick clients, catch handshakes.

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace Cap {

enum class RunMode : uint8_t {
    Off = 0,
    Light,
    Aggressive,
    Pinned,
};

void begin();
void startLight();
void startAggressive();
void startPinned(uint8_t ch, const uint8_t* bssid, const char* ssid = nullptr);
void stop();
bool isRunning();
RunMode runMode();
bool isLocked();

// Session-only: stop attacking this BSSID until Cap::stop()/start.
// Prefer locked target, else last kicked. Returns true if a BSSID was skipped.
bool skipCurrent();
bool isSkipped(const uint8_t* bssid);

void loop();

struct Counters {
    uint32_t framesSeen;
    uint32_t framesEapol;
    uint32_t framesQueued;
    uint32_t framesDropped;
    uint32_t framesWritten;
    uint32_t framesDeauth;
    uint32_t filesOpened;
    uint8_t  currentChannel;
    char     currentBssid[18];
    char     currentSsid[33];
    char     lastHsSsid[33];
    char     methodTag[8];
};
const Counters& counters();

} // namespace Cap
