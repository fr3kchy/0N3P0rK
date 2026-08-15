#pragma once
#include "../net/ap_sta.h"
#include "../storage/littlefs_ops.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

// Shared STA openers — same idea as OnePork (DNS first, retry, HTTP then TLS).

inline bool ioTlsOpen(WiFiClientSecure& c, const char* host, uint16_t port = 443) {
    if (!host) return false;
    IPAddress ip;
    Net::resolveHost(host, ip, 3);
    c.setInsecure();
    c.setTimeout(20000);
    Storage::brewHeap();
    for (uint8_t i = 0; i < 3; i++) {
        if (c.connect(host, port, 12000)) return true;
        c.stop();
        delay(250);
        yield();
    }
    return false;
}

// Prefer cheap HTTP; if 80 is dead or the host wants TLS, use 443.
inline bool ioPwnOpen(WiFiClientSecure& tls, WiFiClient& plain, bool& useTls,
                      const char* host, bool forceTls = false) {
    useTls = false;
    IPAddress ip;
    Net::resolveHost(host, ip, 3);
    if (!forceTls) {
        plain.setTimeout(8000);
        Storage::brewHeap();
        if (plain.connect(host, 80, 8000)) return true;
        plain.stop();
    }
    useTls = true;
    tls.setInsecure();
    tls.setTimeout(12000);
    Storage::brewHeap();
    for (uint8_t i = 0; i < 2; i++) {
        if (tls.connect(host, 443, 10000)) return true;
        tls.stop();
        delay(200);
    }
    return false;
}

inline bool ioHttpOk(const char* status) {
    if (!status || !status[0]) return false;
    if (strstr(status, "200") || strstr(status, "201") || strstr(status, "409"))
        return true;
    return false;
}

inline bool ioHttpRedirect(const char* status) {
    if (!status) return false;
    return strstr(status, "301") || strstr(status, "302") ||
           strstr(status, "307") || strstr(status, "308");
}
