#pragma once
#include <Arduino.h>
#include <vector>

struct DetectedNetwork {
    uint8_t bssid[6];
    char ssid[33];
    int8_t rssi;
    int8_t rssiAvg;
    uint8_t channel;
    bool isHidden;
    bool hasPMF;
};

namespace NetworkRecon {
inline void init() {}
inline void start() {}
inline void stop() {}
inline void pause() {}
inline void resume() {}
inline void freeNetworks() {}
inline bool isRunning() { return false; }
inline bool isPaused() { return false; }
inline std::vector<DetectedNetwork>& getNetworks() {
    static std::vector<DetectedNetwork> empty;
    return empty;
}
inline uint8_t estimateClientCount(const DetectedNetwork&) { return 0; }
}
