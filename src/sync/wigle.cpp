// sync/wigle.cpp
// fR3k v3.0.4: Wigle.net upload implementation.

#include "wigle.h"

#include "../storage/littlefs_ops.h"
#include "../cap/capture_name.h"
#include "../gps/gps_service.h"
#include "../sync/wpasec.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SD.h>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>

static const char* WIGLE_HOST = "api.wigle.net";
static const uint16_t WIGLE_PORT = 443;
static const char* WIGLE_UPLOAD_PATH = "/api/v2/upload";
static const uint16_t WIGLE_MAX_ROWS_PER_CALL = 2000;

static Preferences s_wiglePrefs;
static char s_lastError[64] = "";
static volatile bool s_busy = false;

// Track BSSIDs we've submitted. Stored in NVS namespace `wigle` as
// the single string `submitted` (CSV of 12-hex-digit BSSIDs, no
// separators). A linear scan over the string is fine for the
// expected size (< 10k entries).
static std::vector<char> s_submittedCache;
static bool s_submittedLoaded = false;

bool Wigle::isBusy() { return s_busy; }
const char* Wigle::getLastError() { return s_lastError; }

void Wigle::setCredentials(const char* apiName, const char* apiToken) {
    s_wiglePrefs.begin("wigle", false);
    s_wiglePrefs.putString("name", apiName ? apiName : "");
    s_wiglePrefs.putString("token", apiToken ? apiToken : "");
    s_wiglePrefs.end();
    s_lastError[0] = '\0';
}

void Wigle::clearCredentials() {
    s_wiglePrefs.begin("wigle", false);
    s_wiglePrefs.clear();
    s_wiglePrefs.end();
    s_submittedLoaded = false;
    s_submittedCache.clear();
}

bool Wigle::hasCredentials() {
    s_wiglePrefs.begin("wigle", true);
    String n = s_wiglePrefs.getString("name", "");
    String t = s_wiglePrefs.getString("token", "");
    s_wiglePrefs.end();
    return (n.length() > 0 && t.length() >= 32);
}

const char* Wigle::getApiName() {
    static char buf[33];
    s_wiglePrefs.begin("wigle", true);
    String n = s_wiglePrefs.getString("name", "");
    s_wiglePrefs.end();
    strncpy(buf, n.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

const char* Wigle::getMaskedToken() {
    s_wiglePrefs.begin("wigle", true);
    String t = s_wiglePrefs.getString("token", "");
    s_wiglePrefs.end();
    if (t.length() < 4) return nullptr;
    static char buf[16];
    size_t stars = (t.length() > 4) ? t.length() - 4 : 0;
    if (stars > 8) stars = 8;
    for (size_t i = 0; i < stars; i++) buf[i] = '*';
    buf[stars] = '\0';
    strncat(buf, t.c_str() + t.length() - 4,
            sizeof(buf) - stars - 1);
    return buf;
}

static void loadSubmittedCache() {
    if (s_submittedLoaded) return;
    s_wiglePrefs.begin("wigle", true);
    String s = s_wiglePrefs.getString("submitted", "");
    s_wiglePrefs.end();
    s_submittedCache.assign(s.length() + 1, 0);
    memcpy(s_submittedCache.data(), s.c_str(), s.length());
    s_submittedLoaded = true;
}

static bool isInSubmittedCache(const char* bssid12) {
    loadSubmittedCache();
    if (s_submittedCache.empty() || s_submittedCache.size() < 12) return false;
    for (size_t i = 0; i + 12 <= s_submittedCache.size(); i += 12) {
        if (memcmp(s_submittedCache.data() + i, bssid12, 12) == 0) return true;
    }
    return false;
}

static void appendSubmittedCache(const char* bssid12) {
    if (isInSubmittedCache(bssid12)) return;
    const size_t off = s_submittedCache.empty() ? 0 : s_submittedCache.size() - 1;
    s_submittedCache.resize(off + 12, 0);
    memcpy(s_submittedCache.data() + off, bssid12, 12);
    s_submittedCache.push_back(0);
    // Persist (NVS string is opaque to the formatter, so flatten).
    s_wiglePrefs.begin("wigle", false);
    String flat;
    flat.reserve(s_submittedCache.size());
    for (size_t i = 0; i + 1 < s_submittedCache.size(); i++) {
        char c = s_submittedCache[i];
        if (c) flat += c;
    }
    s_wiglePrefs.putString("submitted", flat);
    s_wiglePrefs.end();
}

// Normalise a BSSID string to 12 upper-case hex digits, no separators.
static void normalizeBssid(const char* in, char out[13]) {
    out[0] = '\0';
    if (!in) return;
    char hex[13] = {0};
    if (CapName::extractBssidHex(in, hex)) {
        memcpy(out, hex, 12);
        out[12] = '\0';
        return;
    }
    size_t j = 0;
    for (size_t i = 0; in[i] && j < 12; i++) {
        char c = in[i];
        if (c != ':' && c != '-') {
            out[j++] = (char)toupper((unsigned char)c);
        }
    }
    out[j] = '\0';
}

static bool isHex12(const char* s) {
    if (!s || strlen(s) != 12) return false;
    for (int i = 0; i < 12; i++) {
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

struct BssidRow {
    char bssid[13];
    char ssid[33];
    double lat;
    double lon;
    uint8_t channel;
};

// Walk /0N3P0rK/handshakes and pull the BSSID + filename-mtime + a
// reasonable SSID (from WPA-sec cache if cracked; from filename
// otherwise). Skips already-uploaded BSSIDs. Returns a vector of
// recommend-upload candidates.
static std::vector<BssidRow> collectRecommend() {
    std::vector<BssidRow> out;
    File root = SD.open("/0N3P0rK/handshakes");
    if (!root) return out;
    if (!root.isDirectory()) { root.close(); return out; }
    const GpsSnapshot g = GpsService::snapshot();
    const bool haveGps = g.fix;
    File f = root.openNextFile();
    while (f && out.size() < WIGLE_MAX_ROWS_PER_CALL) {
        if (!f.isDirectory()) {
            const char* name = f.name();
            size_t nlen = strlen(name);
            bool isPcap = false;
            if (nlen > 5 && strcasecmp(name + nlen - 5, ".pcap") == 0) isPcap = true;
            else if (nlen > 4 && strcasecmp(name + nlen - 4, ".cap") == 0) isPcap = true;
            else if (nlen > 7 && strcasecmp(name + nlen - 7, ".pcapng") == 0) isPcap = true;
            if (isPcap) {
                // File size filter (Wigle 2 MB cap; we cap at 1 MB).
                if (f.size() <= 1024UL * 1024UL) {
                    char hex[13] = {0};
                    if (CapName::extractBssidHex(name, hex)) {
                        char bssid[13];
                        normalizeBssid(hex, bssid);
                        if (isHex12(bssid) && !isInSubmittedCache(bssid)) {
                            // Skip cracked BSSIDs.
                            const char* pw = WPASec::getPassword(bssid);
                            if (!pw[0]) {
                                BssidRow r{};
                                memcpy(r.bssid, bssid, 13);
                                const char* ssid = WPASec::getSSID(bssid);
                                if (ssid && ssid[0]) {
                                    strncpy(r.ssid, ssid, sizeof(r.ssid) - 1);
                                } else {
                                    // SSID from filename minus the BSSID-tail.
                                    size_t copyLen = nlen > 32 ? 32 : nlen;
                                    memcpy(r.ssid, name, copyLen);
                                    r.ssid[copyLen] = '\0';
                                }
                                r.lat = haveGps ? g.latitude : 0.0;
                                r.lon = haveGps ? g.longitude : 0.0;
                                r.channel = 0;
                                out.push_back(r);
                            }
                        }
                    }
                }
            }
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();
    return out;
}

uint16_t Wigle::recommendCount() {
    std::vector<BssidRow> r = collectRecommend();
    return (uint16_t)r.size();
}

static bool csvEscapeAndAppend(const char* field, String& out) {
    if (!field) return false;
    bool needQuote = false;
    for (const char* p = field; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            needQuote = true; break;
        }
    }
    if (needQuote) out += '"';
    for (const char* p = field; *p; p++) {
        if (*p == '"') out += '"';
        out += *p;
    }
    if (needQuote) out += '"';
    return true;
}

WigleResult Wigle::uploadRecommended() {
    WigleResult r{};
    r.success = false;
    if (s_busy) {
        strncpy(r.error, "busy", sizeof(r.error) - 1);
        return r;
    }
    if (!hasCredentials()) {
        strncpy(r.error, "no credentials", sizeof(r.error) - 1);
        strncpy(s_lastError, r.error, sizeof(s_lastError) - 1);
        return r;
    }
    if (WiFi.status() != WL_CONNECTED) {
        strncpy(r.error, "wifi down", sizeof(r.error) - 1);
        strncpy(s_lastError, r.error, sizeof(s_lastError) - 1);
        return r;
    }
    s_busy = true;
    std::vector<BssidRow> rows = collectRecommend();
    if (rows.empty()) {
        r.success = true;
        r.uploaded = 0;
        r.deduped = 0;
        strncpy(r.message, "no candidates", sizeof(r.message) - 1);
        s_busy = false;
        return r;
    }

    // Build the CSV body.
    String body;
    body.reserve(rows.size() * 64 + 96);
    body += "BSSID,SSID,Latitude,Longitude,Time,Channel,Encryption,Accuracy\n";
    char tbuf[24];
    time_t now = time(nullptr);
    if (now < 1000000) now = 0;  // epoch not set; Wigle will reject if non-zero
    struct tm* tm = (now > 0) ? gmtime(&now) : nullptr;
    if (tm) strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", tm);
    else tbuf[0] = '\0';
    for (const auto& row : rows) {
        body += row.bssid;
        body += ',';
        csvEscapeAndAppend(row.ssid, body);
        body += ',';
        if (row.lat != 0.0 || row.lon != 0.0) {
            char num[24];
            snprintf(num, sizeof(num), "%.6f,%.6f", row.lat, row.lon);
            body += num;
        } else {
            body += ",";  // empty lat/lon
        }
        body += ',';
        if (tbuf[0]) body += tbuf;
        body += ',';
        if (row.channel) {
            char cbuf[8];
            snprintf(cbuf, sizeof(cbuf), "%u", row.channel);
            body += cbuf;
        }
        body += ",WPA,";
        body += "10\n";
    }

    // Wigle Basic auth: base64(name:token).
    s_wiglePrefs.begin("wigle", true);
    String n = s_wiglePrefs.getString("name", "");
    String t = s_wiglePrefs.getString("token", "");
    s_wiglePrefs.end();
    String cred = n + ":" + t;

    // Send via WiFiClientSecure (TLS to api.wigle.net).
    WiFiClientSecure client;
    client.setInsecure();  // skip cert validation; Wigle's cert is
                           // pinned elsewhere - this is a small device.
    HTTPClient http;
    if (!http.begin(client, WIGLE_HOST, WIGLE_PORT, WIGLE_UPLOAD_PATH, true)) {
        strncpy(r.error, "http begin", sizeof(r.error) - 1);
        strncpy(s_lastError, r.error, sizeof(s_lastError) - 1);
        s_busy = false;
        return r;
    }
    http.setAuthorization("");  // We pass Authorization header manually.
    String basicHdr = "Basic " + cred;  // base64 below
    // base64 encode in-place:
    const char* b64alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String b64;
    b64.reserve(((cred.length() + 2) / 3) * 4);
    for (size_t i = 0; i < cred.length(); i += 3) {
        uint32_t v = (uint8_t)cred[i] << 16;
        int rem = 3;
        if (i + 1 < cred.length()) { v |= (uint8_t)cred[i+1] << 8; rem = 2; }
        if (i + 2 < cred.length()) { v |= (uint8_t)cred[i+2];      rem = 1; }
        b64 += b64alpha[(v >> 18) & 0x3F];
        b64 += b64alpha[(v >> 12) & 0x3F];
        b64 += (rem >= 2) ? b64alpha[(v >> 6) & 0x3F] : '=';
        b64 += (rem >= 1) ? b64alpha[v & 0x3F] : '=';
    }
    http.addHeader("Authorization", "Basic " + b64);
    http.addHeader("Content-Type", "text/csv");
    http.addHeader("Accept", "application/json");
    int code = http.POST((uint8_t*)body.c_str(), body.length());
    String resp = http.getString();
    http.end();
    s_busy = false;

    if (code >= 200 && code < 300) {
        r.success = true;
        r.uploaded = (uint16_t)rows.size();
        r.failed = 0;
        // Mark every submitted BSSID in the local dedup cache.
        for (const auto& row : rows) {
            appendSubmittedCache(row.bssid);
            // Mark WPA-sec's uploaded list too (so the UI shows
            // them as uploaded next time).
            WPASec::markAsUploaded(row.bssid);
        }
        snprintf(r.message, sizeof(r.message), "OK %d (%u rows)",
                 code, r.uploaded);
    } else {
        snprintf(r.error, sizeof(r.error), "HTTP %d", code);
        strncpy(s_lastError, r.error, sizeof(s_lastError) - 1);
        r.failed = (uint16_t)rows.size();
        snprintf(r.message, sizeof(r.message), "fail: %.40s",
                 resp.c_str());
    }
    return r;
}
