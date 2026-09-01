// sync/wpasec.h
// WPA-SEC client: upload .pcap, download potfile.
// https://wpa-sec.stanev.org/

#pragma once

#include <Arduino.h>
#include <vector>

struct WPASecSyncResult {
    bool success;
    uint16_t uploaded;
    uint16_t failed;
    uint16_t skipped;
    uint16_t cracked;
    uint16_t newCracked;
    char error[48];
};

typedef void (*WPASecProgressCallback)(const char* status, uint16_t progress, uint16_t total);

class WPASec {
public:
    static bool hasApiKey();
    static bool hasApiKey(const char* key);
    static bool canSync();
    static WPASecSyncResult syncCaptures(const char* apiKey, WPASecProgressCallback cb = nullptr);
    static bool uploadOneFile(const char* filepath, const char* bssidHint, const char* apiKey);
    static bool pullPotfile(const char* apiKey, uint16_t& lines);
    // fR3k v3.0.4: warm the cracked+uploaded cache on boot so the
    // first LootMenu open does not block on wpasec_results.txt.
    // Idempotent and thread-safe (no work if cacheLoaded already).
    static void preload();

    static bool loadCache();
    static void freeCacheMemory();
    static bool isCracked(const char* bssid);
    static const char* getPassword(const char* bssid);
    static const char* getSSID(const char* bssid);
    static uint16_t getCrackedCount();
    static bool isUploaded(const char* bssid);
    static void markAsUploaded(const char* bssid);
    // fR3k v3.0.4: debounced auto-pull. Returns true if a pull was
    // started (caller should NOT block), false if skipped because
    // (a) the operator disabled auto-sync, (b) the debounce window
    // hasn't elapsed, (c) the API key isn't set, or (d) WiFi is down.
    // 'minIntervalMs' = 0 to force a pull.
    static bool pullPotfileIfStale(uint32_t minIntervalMs);

    static const char* getLastError();
    static bool isBusy();

private:
    static bool cacheLoaded;
    static char lastError[64];
    static volatile bool busy;

    struct CrackedEntry {
        char bssid[13];
        char ssid[33];
        char password[64];
    };
    struct UploadedEntry {
        char bssid[13];
    };
    static std::vector<CrackedEntry> crackedCache;
    static std::vector<UploadedEntry> uploadedCache;

    static void normalizeBSSID(const char* input, char* output, size_t outLen);
    static const CrackedEntry* findCracked(const char* normalizedBssid);
    static bool findUploaded(const char* normalizedBssid);
    static bool loadUploadedList();
    static bool saveUploadedList();
    static bool uploadSingleCapture(const char* filepath, const char* bssid, const char* apiKey);
    static bool downloadPotfile(const char* apiKey, uint16_t& newCracks);
};
