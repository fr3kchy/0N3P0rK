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
    c.setInsecure();
    c.setTimeout(15000);
    Storage::brewHeap();
    // One hostname connect like M5PORKCHOP. IP-first + 3x12s is what hung the UI.
    if (c.connect(host, port, 10000)) return true;
    c.stop();
    delay(250);
    yield();
    return c.connect(host, port, 8000);
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

// Streamed from SD — not held in RAM. 890 KB+ handshakes are normal.
static constexpr size_t kHsUploadMax = 8u * 1024u * 1024u;

struct IoXfer {
    char phase[20];
    uint16_t file;
    uint16_t files;
    uint16_t ok;
    uint16_t fail;
    uint32_t sent;
    uint32_t size;
    void (*paint)();
    uint32_t lastPaint;
};

inline IoXfer& ioXfer() {
    static IoXfer s{};
    return s;
}

inline void ioXferClear() {
    IoXfer& x = ioXfer();
    x.phase[0] = '\0';
    x.file = x.files = x.ok = x.fail = 0;
    x.sent = x.size = 0;
    x.lastPaint = 0;
}

inline void ioXferPaint(bool force = false) {
    IoXfer& x = ioXfer();
    if (!x.paint) return;
    uint32_t now = millis();
    if (!force && x.lastPaint && (uint32_t)(now - x.lastPaint) < 120) return;
    x.lastPaint = now;
    x.paint();
}

inline void ioXferPhase(const char* phase, uint16_t file, uint16_t files) {
    IoXfer& x = ioXfer();
    if (phase) {
        strncpy(x.phase, phase, sizeof(x.phase) - 1);
        x.phase[sizeof(x.phase) - 1] = '\0';
    }
    x.file = file;
    x.files = files;
    x.sent = 0;
    x.size = 0;
    ioXferPaint(true);
}

inline bool ioWriteAll(WiFiClient& c, const uint8_t* p, size_t n) {
    size_t off = 0;
    uint8_t spins = 0;
    while (off < n) {
        size_t w = c.write(p + off, n - off);
        if (w == 0) {
            if (!c.connected() || ++spins > 50) return false;
            delay(10);
            yield();
            continue;
        }
        spins = 0;
        off += w;
        yield();
    }
    return true;
}

inline bool ioWriteAll(WiFiClient& c, const char* s) {
    if (!s) return true;
    return ioWriteAll(c, reinterpret_cast<const uint8_t*>(s), strlen(s));
}

// Drain headers+body so the server can finish before we RST the socket.
inline void ioDrain(WiFiClient& c, uint32_t timeoutMs = 25000) {
    unsigned long t0 = millis();
    uint8_t junk[64];
    while ((uint32_t)(millis() - t0) < timeoutMs) {
        int n = c.available();
        if (n > 0) {
            if (n > (int)sizeof(junk)) n = (int)sizeof(junk);
            c.read(junk, (size_t)n);
            t0 = millis();
            yield();
            continue;
        }
        if (!c.connected()) break;
        delay(8);
        yield();
    }
}

inline bool ioReadStatusLine(WiFiClient& c, char* status, size_t statusLen,
                             uint32_t timeoutMs = 45000) {
    if (status && statusLen) status[0] = '\0';
    unsigned long t0 = millis();
    size_t si = 0;
    while ((uint32_t)(millis() - t0) < timeoutMs) {
        int avail = c.available();
        if (avail <= 0) {
            if (!c.connected()) break;
            delay(10);
            yield();
            continue;
        }
        int ch = c.read();
        if (ch < 0) continue;
        if (ch == '\n') {
            if (status && statusLen) status[si] = '\0';
            return status && status[0];
        }
        if (ch != '\r' && status && si + 1 < statusLen) status[si++] = (char)ch;
    }
    if (status && statusLen) status[si] = '\0';
    return status && status[0];
}
