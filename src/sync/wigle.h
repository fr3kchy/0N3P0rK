// sync/wigle.h
// fR3k v3.0.4: Wigle.net upload client.
//
// Posts a CSV of BSSID + lat/lon observations to
//   https://api.wigle.net/api/v2/upload
// using HTTP Basic auth with the operator's API name + API token.
//
// Free-tier Wigle allows roughly 10 uploads/day. We:
//   - Cap each call at 2k BSSID rows
//   - Dedup against a local NVS cache (BSSIDs already submitted)
//   - Only mark recommend-upload rows (not cracked, not uploaded,
//     clean BSSID, file size < 1 MB) as candidates
//
// The API is invoked from the LootMenu with the `W` hotkey.

#pragma once

#include <Arduino.h>
#include <stdint.h>

struct WigleResult {
    bool success;
    uint16_t uploaded;
    uint16_t deduped;
    uint16_t failed;
    char error[48];
    char message[64];
};

class Wigle {
public:
    // fR3k v3.0.4: NVS-backed credentials. `setCredentials` overwrites
    // both fields atomically. `clearCredentials` wipes the namespace.
    static void setCredentials(const char* apiName, const char* apiToken);
    static void clearCredentials();
    static bool hasCredentials();
    static const char* getApiName();   // returns "" if not set
    // fR3k v3.0.4: returns the stored API token masked to its last
    // 4 characters (e.g. "******abcd"). Returns nullptr if not set.
    static const char* getMaskedToken();

    // Synchronous recommended upload. Builds a CSV of every BSSID
    // currently flagged as recommend-upload by the loot menu, dedups
    // against the local cache, and posts to Wigle. Returns a result
    // struct with the outcome. The function blocks; call from the
    // main loop, not from an ISR.
    static WigleResult uploadRecommended();

    // Count of BSSIDs the loot menu currently flags as recommend.
    // Computed on demand (re-walks the SD card).
    static uint16_t recommendCount();

    // Total BSSIDs already submitted to Wigle in this device's
    // lifetime (read from the persistent NVS cache, no SD walk).
    // fR3k v3.0.4: surfaces the running total in the WIGLE
    // settings page so the operator can see uploads are landing.
    static uint32_t submittedCount();

    // True while a call is in flight.
    static bool isBusy();
    static const char* getLastError();
};
