#include "capture_name.h"
#include "../sync/pot_parse.h"
#include <SD.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>

namespace CapName {

static bool isAllHex(const char* s, size_t n) {
    if (!s) return false;
    for (size_t i = 0; i < n; i++) {
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return n > 0;
}

static const char* fileBase(const char* path) {
    if (!path) return "";
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static size_t stemLen(const char* name) {
    const char* base = fileBase(name);
    const char* dot = strrchr(base, '.');
    size_t n = dot ? (size_t)(dot - base) : strlen(base);
    if (n > 3 && strncmp(base + n - 3, "_hs", 3) == 0) n -= 3;
    else if (n > 6 && strncmp(base + n - 6, "_pmkid", 6) == 0) n -= 6;
    return n;
}

void sanitizeSsid(const char* ssid, char* out, size_t outLen) {
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (!ssid || !ssid[0]) {
        strncpy(out, "HIDDEN", outLen - 1);
        out[outLen - 1] = '\0';
        return;
    }
    size_t j = 0;
    const size_t maxChars = (outLen - 1 < 20) ? outLen - 1 : 20;
    for (size_t i = 0; ssid[i] && j < maxChars; i++) {
        char c = ssid[i];
        if ((unsigned char)c < 0x20) continue;
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[j++] = c;
    }
    while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '_')) j--;
    if (j == 0) {
        strncpy(out, "HIDDEN", outLen - 1);
        out[outLen - 1] = '\0';
        return;
    }
    out[j] = '\0';
}

void buildStem(const char* ssid, const uint8_t bssid[6], char* out, size_t outLen) {
    if (!out || outLen == 0) return;
    char san[21];
    sanitizeSsid(ssid, san, sizeof(san));
    if (!bssid) {
        strncpy(out, san, outLen - 1);
        out[outLen - 1] = '\0';
        return;
    }
    snprintf(out, outLen, "%s_%02X%02X%02X%02X%02X%02X",
             san,
             bssid[0], bssid[1], bssid[2],
             bssid[3], bssid[4], bssid[5]);
}

void prettyMac(const char* hex12, char out[18]) {
    if (!out) return;
    out[0] = '\0';
    if (!hex12 || strlen(hex12) < 12) return;
    snprintf(out, 18, "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
             hex12[0], hex12[1], hex12[2], hex12[3], hex12[4], hex12[5],
             hex12[6], hex12[7], hex12[8], hex12[9], hex12[10], hex12[11]);
}

bool extractBssidHex(const char* name, char hex13[13]) {
    if (!hex13) return false;
    hex13[0] = '\0';
    if (!name || !name[0]) return false;
    const char* base = fileBase(name);
    size_t n = stemLen(base);
    if (n == 0) return false;

    // New: SSID_AABBCCDDEEFF
    if (n > 13 && base[n - 13] == '_' && isAllHex(base + n - 12, 12)) {
        for (int i = 0; i < 12; i++)
            hex13[i] = (char)toupper((unsigned char)base[n - 12 + i]);
        hex13[12] = '\0';
        return true;
    }

    // Legacy dashed / colon / raw hex from the start
    size_t h = 0;
    char hex[13];
    for (size_t i = 0; i < n && h < 12; i++) {
        char c = base[i];
        if (c == '-' || c == ':') continue;
        if (!isxdigit((unsigned char)c)) {
            h = 0;
            break;
        }
        hex[h++] = (char)toupper((unsigned char)c);
    }
    if (h == 12) {
        memcpy(hex13, hex, 12);
        hex13[12] = '\0';
        return true;
    }

    // Last-resort: last 12 hex digits in the stem
    h = 0;
    for (size_t i = 0; i < n; i++) {
        char c = base[i];
        if (isxdigit((unsigned char)c)) {
            if (h < 12) hex[h++] = (char)toupper((unsigned char)c);
            else {
                memmove(hex, hex + 1, 11);
                hex[11] = (char)toupper((unsigned char)c);
            }
        } else if (c != '-' && c != ':') {
            h = 0;
        }
    }
    if (h == 12) {
        memcpy(hex13, hex, 12);
        hex13[12] = '\0';
        return true;
    }
    return false;
}

bool extractSsidFromName(const char* name, char ssid[33]) {
    if (!ssid) return false;
    ssid[0] = '\0';
    if (!name) return false;
    const char* base = fileBase(name);
    size_t n = stemLen(base);
    if (n > 13 && base[n - 13] == '_' && isAllHex(base + n - 12, 12)) {
        size_t sl = n - 13;
        if (sl == 0) return false;
        if (sl > 32) sl = 32;
        memcpy(ssid, base, sl);
        ssid[sl] = '\0';
        return ssid[0] != '\0';
    }
    return false;
}

bool readCompanionSsid(const char* dir, const char* captureName, char ssid[33]) {
    if (!ssid) return false;
    ssid[0] = '\0';
    if (!dir || !captureName) return false;

    const char* base = fileBase(captureName);
    size_t n = stemLen(base);
    char path[96];

    auto tryRead = [&](const char* p) -> bool {
        if (!SD.exists(p)) return false;
        File f = SD.open(p, "r");
        if (!f) return false;
        char buf[34];
        int got = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
        f.close();
        if (got <= 0) return false;
        buf[got] = '\0';
        while (got > 0 && (buf[got - 1] == '\r' || buf[got - 1] == ' ' || buf[got - 1] == '\t'))
            buf[--got] = '\0';
        if (got <= 0) return false;
        strncpy(ssid, buf, 32);
        ssid[32] = '\0';
        return true;
    };

    snprintf(path, sizeof(path), "%s/%.*s.txt", dir, (int)n, base);
    if (tryRead(path)) return true;

    char hex[13];
    if (extractBssidHex(base, hex)) {
        snprintf(path, sizeof(path), "%s/%s.txt", dir, hex);
        if (tryRead(path)) return true;
    }
    return false;
}

void writeCompanionSsid(const char* dir, const char* stemOrName, const char* ssid) {
    (void)dir;
    (void)stemOrName;
    (void)ssid;
}

bool ssidFromMgmt(const uint8_t* frame, uint16_t len, char ssid[33]) {
    if (!ssid) return false;
    ssid[0] = '\0';
    if (!frame || len < 38) return false;
    uint8_t fc = frame[0] & 0xFC;
    if (fc != 0x80 && fc != 0x50) return false; // beacon / probe resp
    size_t i = 36;
    while (i + 2 <= len) {
        uint8_t tag = frame[i];
        uint8_t tlen = frame[i + 1];
        if (i + 2 + tlen > len) break;
        if (tag == 0) {
            if (tlen == 0) return false;
            size_t n = tlen < 32 ? tlen : 32;
            size_t w = 0;
            for (size_t k = 0; k < n; k++) {
                char c = (char)frame[i + 2 + k];
                if ((unsigned char)c < 0x20) continue;
                ssid[w++] = c;
            }
            ssid[w] = '\0';
            return w > 0;
        }
        i += 2 + tlen;
    }
    return false;
}

bool hexToMac(const char* hex12, uint8_t out[6]) {
    if (!hex12 || !out || strlen(hex12) < 12) return false;
    auto v = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
        if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
        return (uint8_t)(c - 'A' + 10);
    };
    for (int i = 0; i < 6; i++) {
        char a = hex12[i * 2], b = hex12[i * 2 + 1];
        if (!isxdigit((unsigned char)a) || !isxdigit((unsigned char)b)) return false;
        out[i] = (uint8_t)((v(a) << 4) | v(b));
    }
    return true;
}

bool sameSsid(const char* a, const char* b) {
    if (!a || !b || !a[0] || !b[0]) return false;
    char fa[33] = {0}, fb[33] = {0};
    auto fold = [](const char* in, char* out) {
        size_t j = 0;
        for (; *in && j + 1 < 33; in++) {
            char c = *in;
            if (c == ' ' || c == '_' || c == '-') continue;
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            out[j++] = c;
        }
        out[j] = '\0';
    };
    fold(a, fa);
    fold(b, fb);
    return fa[0] && strcasecmp(fa, fb) == 0;
}

bool metaFrom22000Line(const char* line, char hex13[13], char ssid[33]) {
    if (hex13) hex13[0] = '\0';
    if (ssid) ssid[0] = '\0';
    if (!line || strncmp(line, "WPA*", 4) != 0) return false;
    char tmp[200];
    strncpy(tmp, line, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char* colon = strchr(tmp, ':');
    if (colon) *colon = '\0';
    char* parts[8];
    int pc = 0;
    char* p = tmp;
    while (pc < 8) {
        parts[pc++] = p;
        char* s = strchr(p, '*');
        if (!s) break;
        *s = '\0';
        p = s + 1;
    }
    bool ok = false;
    if (pc >= 4 && hex13 && extractBssidHex(parts[3], hex13)) ok = true;
    if (pc >= 6 && ssid && parts[5][0] && Pot::decodeHexStr(parts[5], ssid, 33)) ok = true;
    return ok;
}

bool metaFrom22000File(const char* dir, const char* name, char hex13[13], char ssid[33]) {
    if (hex13) hex13[0] = '\0';
    if (ssid) ssid[0] = '\0';
    if (!dir || !name) return false;
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", dir, fileBase(name));
    File f = SD.open(path, "r");
    if (!f) return false;
    char line[200];
    int n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    f.close();
    if (n < 10) return false;
    line[n] = '\0';
    return metaFrom22000Line(line, hex13, ssid);
}

} // namespace CapName
