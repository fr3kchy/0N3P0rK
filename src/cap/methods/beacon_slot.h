// One tracked AP, shared between the orchestrator (cap/sniffer.cpp, which
// owns the array) and every capture method in cap/methods/ (which only read
// it). Moved out of sniffer.cpp so methods don't need to reach into its
// internals to see what they're aiming at.
#pragma once

#include <stdint.h>

namespace Cap {

static const uint16_t BEACON_MAX = 400;

struct BeaconSlot {
    uint8_t  bssid[6];
    uint8_t  channel;
    int8_t   rssi;
    uint16_t len;
    char     ssid[33];
    uint8_t  clients[4][6];
    uint8_t  clientN;
    bool     pmfCapable;   // MFPC/MFPR bit seen in RSN IE -> deauth/disassoc ignored
    uint8_t  frame[BEACON_MAX];
};

} // namespace Cap
