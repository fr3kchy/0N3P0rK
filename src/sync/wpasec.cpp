// sync/wpasec.cpp
#include "wpasec.h"
#include "../storage/littlefs_ops.h"
#include "../cap/capture_name.h"
#include "pot_parse.h"
#include "../net/ap_sta.h"
#include "net_io.h"
#include "tls.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <esp_heap_caps.h>

static const char* WPASEC_HOST = "wpa-sec.stanev.org";
static const uint16_t WPASEC_PORT = 443;
static const char* WPASEC_UPLOAD_PATH = "/";
static const char* WPASEC_POTFILE_PATH = "/?api&dl=1";
static const size_t WPASEC_MAX_CACHE = 100;
static const uint8_t WPASEC_MAX_PENDING = 16;

bool WPASec::cacheLoaded = false;
char WPASec::lastError[64] = "";
std::vector<WPASec::CrackedEntry> WPASec::crackedCache;
std::vector<WPASec::UploadedEntry> WPASec::uploadedCache;
volatile bool WPASec::busy = false;

bool WPASec::isBusy() { return busy; }
const char* WPASec::getLastError() { return lastError; }

static bool writeAll(WiFiClient& c, const uint8_t* p, size_t n) {
    size_t off = 0;
    while (off < n) {
        size_t w = c.write(p + off, n - off);
        if (w == 0) return false;
        off += w;
        yield();
    }
    return true;
}

static bool writeStr(WiFiClient& c, const char* s) {
    return writeAll(c, reinterpret_cast<const uint8_t*>(s), strlen(s));
}

void WPASec::normalizeBSSID(const char* input, char* output, size_t outLen) {
    if (!output || outLen < 1) return;
    output[0] = '\0';
    if (!input || !input[0]) return;
    char hex[13];
    if (CapName::extractBssidHex(input, hex) && outLen >= 13) {
        memcpy(output, hex, 13);
        return;
    }
    size_t outIdx = 0;
    for (int i = 0; input[i] && outIdx < outLen - 1; i++) {
        char c = input[i];
        if (c != ':' && c != '-') {
            output[outIdx++] = (char)toupper((unsigned char)c);
        }
    }
    output[outIdx] = '\0';
}

static bool bssidFromFilename(const char* name, char out[13]) {
    return CapName::extractBssidHex(name, out);
}

static bool isPcapName(const char* name) {
    size_t n = strlen(name);
    if (n > 5 && strcasecmp(name + n - 5, ".pcap") == 0) return true;
    if (n > 7 && strcasecmp(name + n - 7, ".pcapng") == 0) return true;
    return false;
}

bool WPASec::hasApiKey(const char* key) {
    if (!key || strlen(key) != 32) return false;
    for (int i = 0; i < 32; i++) {
        if (!isxdigit((unsigned char)key[i])) return false;
    }
    return true;
}

bool WPASec::hasApiKey() {
    return hasApiKey(Net::cfg().wpaSecKey);
}

bool WPASec::canSync() {
    uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    uint32_t freeH = ESP.getFreeHeap();
    if (largest < 14000 || freeH < 22000) {
        snprintf(lastError, sizeof(lastError), "low heap %u/%uK",
                 (unsigned)(largest / 1024), (unsigned)(freeH / 1024));
        return false;
    }
    lastError[0] = '\0';
    return true;
}

void WPASec::freeCacheMemory() {
    // Never shrink_to_fit — ESP32 has no C++ exceptions; a failed realloc aborts.
    crackedCache.clear();
    uploadedCache.clear();
    cacheLoaded = false;
}

bool WPASec::loadUploadedList() {
    uploadedCache.clear();
    const char* upPath = Storage::FILE_WPASEC_UPLOADED;
    if (!Storage::fileExists(upPath) &&
        Storage::fileExists("/0N3P0rK/wpa-sec/wpasec_uploaded.txt"))
        upPath = "/0N3P0rK/wpa-sec/wpasec_uploaded.txt";
    if (!Storage::fileExists(upPath)) return true;
    File f = SD.open(upPath, "r");
    if (!f) return false;
    char line[64];
    while (f.available() && uploadedCache.size() < WPASEC_MAX_CACHE) {
        size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = '\0';
        if (n == 0) continue;
        UploadedEntry e{};
        normalizeBSSID(line, e.bssid, sizeof(e.bssid));
        if (e.bssid[0]) uploadedCache.push_back(e);
    }
    f.close();
    return true;
}

bool WPASec::saveUploadedList() {
    Storage::ensureDir(Storage::DIR_RESULTS);
    File f = SD.open(Storage::FILE_WPASEC_UPLOADED, "w");
    if (!f) return false;
    for (const auto& e : uploadedCache) {
        f.println(e.bssid);
    }
    f.close();
    return true;
}

bool WPASec::loadCache() {
    if (cacheLoaded) return true;
    crackedCache.clear();
    uploadedCache.clear();

    const char* resPath = Storage::FILE_WPASEC_RESULTS;
    if (!Storage::fileExists(resPath) &&
        Storage::fileExists("/0N3P0rK/wpa-sec/wpasec_results.txt"))
        resPath = "/0N3P0rK/wpa-sec/wpasec_results.txt";
    if (Storage::fileExists(resPath)) {
        File f = SD.open(resPath, "r");
        if (f) {
            char line[320];
            while (f.available() && crackedCache.size() < WPASEC_MAX_CACHE) {
                size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
                line[n] = '\0';
                while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = '\0';
                if (n == 0) continue;
                char bssidP[18], ssid[33], pass[64];
                if (!Pot::parseLine(line, bssidP, ssid, pass)) continue;
                CrackedEntry e{};
                normalizeBSSID(bssidP, e.bssid, sizeof(e.bssid));
                strncpy(e.ssid, ssid, sizeof(e.ssid) - 1);
                strncpy(e.password, pass, sizeof(e.password) - 1);
                if (e.password[0]) crackedCache.push_back(e);
            }
            f.close();
        }
    }
    loadUploadedList();
    cacheLoaded = true;
    return true;
}

const WPASec::CrackedEntry* WPASec::findCracked(const char* normalizedBssid) {
    for (const auto& e : crackedCache) {
        if (strcmp(e.bssid, normalizedBssid) == 0) return &e;
    }
    return nullptr;
}

bool WPASec::findUploaded(const char* normalizedBssid) {
    for (const auto& e : uploadedCache) {
        if (strcmp(e.bssid, normalizedBssid) == 0) return true;
    }
    return false;
}

bool WPASec::isCracked(const char* bssid) {
    if (!cacheLoaded) loadCache();
    if (!bssid) return false;
    char key[13];
    normalizeBSSID(bssid, key, sizeof(key));
    return findCracked(key) != nullptr;
}

const char* WPASec::getPassword(const char* bssid) {
    if (!cacheLoaded) loadCache();
    if (!bssid || !bssid[0]) return "";
    char key[13];
    normalizeBSSID(bssid, key, sizeof(key));
    const CrackedEntry* e = findCracked(key);
    if (e) return e->password;
    for (const auto& c : crackedCache) {
        if (c.ssid[0] && (strcasecmp(c.ssid, bssid) == 0 || CapName::sameSsid(c.ssid, bssid)))
            return c.password;
    }
    return "";
}

const char* WPASec::getSSID(const char* bssid) {
    if (!cacheLoaded) loadCache();
    if (!bssid) return "";
    char key[13];
    normalizeBSSID(bssid, key, sizeof(key));
    const CrackedEntry* e = findCracked(key);
    return e ? e->ssid : "";
}

uint16_t WPASec::getCrackedCount() {
    if (!cacheLoaded) loadCache();
    return (uint16_t)crackedCache.size();
}

bool WPASec::isUploaded(const char* bssid) {
    if (!cacheLoaded) loadCache();
    if (!bssid) return false;
    char key[13];
    normalizeBSSID(bssid, key, sizeof(key));
    if (findCracked(key)) return true;
    return findUploaded(key);
}

void WPASec::markAsUploaded(const char* bssid) {
    if (!bssid) return;
    char key[13];
    normalizeBSSID(bssid, key, sizeof(key));
    if (key[0] == '\0' || findUploaded(key)) return;
    if (uploadedCache.size() >= WPASEC_MAX_CACHE) return;
    UploadedEntry e{};
    strncpy(e.bssid, key, sizeof(e.bssid) - 1);
    uploadedCache.push_back(e);
}

bool WPASec::uploadSingleCapture(const char* filepath, const char* bssid, const char* apiKey) {
    if (!filepath || !bssid || !apiKey) return false;
    File capFile = SD.open(filepath, "r");
    if (!capFile) {
        snprintf(lastError, sizeof(lastError), "open fail");
        return false;
    }
    size_t fileSize = capFile.size();
    if (fileSize == 0 || fileSize > 250000) {
        capFile.close();
        snprintf(lastError, sizeof(lastError), "bad size");
        return false;
    }

    const char* filename = Storage::baseName(filepath);
    Serial.printf("[WPASEC] upload %s (%u B)\n", filename, (unsigned)fileSize);

    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(WPASEC_HOST, WPASEC_PORT, 10000)) {
        capFile.close();
        snprintf(lastError, sizeof(lastError), "tls connect");
        return false;
    }

    char boundary[32];
    snprintf(boundary, sizeof(boundary), "----WPASec%08lX", (unsigned long)millis());
    char disposition[128];
    snprintf(disposition, sizeof(disposition),
             "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"",
             filename);
    size_t contentLength = 2 + strlen(boundary) + 2 +
                           strlen(disposition) + 2 +
                           38 + 4 +
                           fileSize +
                           2 + 2 + strlen(boundary) + 4;

    client.printf("POST %s HTTP/1.1\r\n", WPASEC_UPLOAD_PATH);
    client.printf("Host: %s\r\n", WPASEC_HOST);
    client.printf("Cookie: key=%s\r\n", apiKey);
    client.printf("User-Agent: 0N3P0rK/" ON3PORK_VERSION "\r\n");
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
    client.printf("Content-Length: %u\r\n", (unsigned)contentLength);
    client.print("Connection: close\r\n\r\n");
    client.printf("--%s\r\n%s\r\nContent-Type: application/octet-stream\r\n\r\n",
                  boundary, disposition);

    client.setTimeout(30000);
    if (!Tls::streamFile(client, capFile, fileSize, lastError, sizeof(lastError)))
        return false;

    if (!client.connected()) {
        snprintf(lastError, sizeof(lastError), "lost after body");
        return false;
    }
    client.flush();
    client.printf("\r\n--%s--\r\n", boundary);

    unsigned long t0 = millis();
    while (client.connected() && !client.available() && millis() - t0 < 10000) {
        delay(10);
        yield();
    }

    bool ok = false;
    if (client.available()) {
        char resp[80];
        size_t n = client.readBytesUntil('\n', resp, sizeof(resp) - 1);
        resp[n] = '\0';
        Serial.printf("[WPASEC] %s\n", resp);
        if (strstr(resp, "200") || strstr(resp, "201") || strstr(resp, "409")) ok = true;
        else snprintf(lastError, sizeof(lastError), "http reject");
    } else {
        snprintf(lastError, sizeof(lastError), "no reply");
    }
    client.stop();
    if (ok) lastError[0] = '\0';
    return ok;
}

bool WPASec::downloadPotfile(const char* apiKey, uint16_t& newCracks) {
    newCracks = 0;
    if (!apiKey) return false;

    WiFiClientSecure client;
    if (!ioTlsOpen(client, WPASEC_HOST, WPASEC_PORT)) {
        client.stop();
        snprintf(lastError, sizeof(lastError), "pot tls");
        return false;
    }

    char req[192];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\nHost: %s\r\nCookie: key=%s\r\n"
             "User-Agent: 0N3P0rK/" ON3PORK_VERSION "\r\nConnection: close\r\n\r\n",
             WPASEC_POTFILE_PATH, WPASEC_HOST, apiKey);
    if (!writeStr(client, req)) {
        client.stop();
        snprintf(lastError, sizeof(lastError), "pot send");
        return false;
    }

    unsigned long t0 = millis();
    while (client.connected() && !client.available() && millis() - t0 < 15000) {
        delay(10);
        yield();
    }
    if (!client.available()) {
        client.stop();
        snprintf(lastError, sizeof(lastError), "pot timeout");
        return false;
    }

    bool headersDone = false;
    bool statusOk = false;
    char line[320];
    bool first = true;
    while (client.connected() && client.available() && !headersDone) {
        size_t n = client.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        if (first) {
            first = false;
            statusOk = (strstr(line, "200") != nullptr);
            Serial.printf("[WPASEC] pot %s\n", line);
        }
        if (n == 0 || (n == 1 && line[0] == '\r')) headersDone = true;
    }
    if (!headersDone || !statusOk) {
        client.stop();
        snprintf(lastError, sizeof(lastError), "pot http");
        return false;
    }

    Storage::ensureDir(Storage::DIR_RESULTS);
    File out = SD.open(Storage::FILE_WPASEC_RESULTS, "w");
    if (!out) {
        client.stop();
        snprintf(lastError, sizeof(lastError), "pot save");
        return false;
    }

    uint16_t lines = 0;
    bool looksHtml = false;
    while (client.connected() || client.available()) {
        if (!client.available()) {
            if (millis() - t0 > 45000) break;
            delay(5);
            yield();
            continue;
        }
        size_t n = client.readBytesUntil('\n', line, sizeof(line) - 1);
        if (n == 0) continue;
        line[n] = '\0';
        if (n > 0 && line[n - 1] == '\r') line[n - 1] = '\0';
        if (line[0] == '<' || strncmp(line, "<!", 2) == 0) {
            looksHtml = true;
            continue;
        }
        char bssidP[18], ssid[33], pass[64];
        bool keep = Pot::parseLine(line, bssidP, ssid, pass);
        if (!keep && line[0] && line[0] != '#' && strlen(line) > 8) {
            // keep unknown but non-HTML lines so we can inspect later
            keep = (strchr(line, ':') != nullptr || strncmp(line, "WPA*", 4) == 0);
        }
        if (keep) {
            out.println(line);
            lines++;
            if (lines == 1) Serial.printf("[WPASEC] pot sample: %.80s\n", line);
        }
        yield();
    }
    out.close();
    client.stop();

    // Server returned HTML (bad key / login page) — keep the old results.
    if (looksHtml && lines == 0) {
        File trunc = SD.open(Storage::FILE_WPASEC_RESULTS, "w");
        if (trunc) trunc.close();
        snprintf(lastError, sizeof(lastError), "pot html");
        Serial.println("[WPASEC] pot rejected: html body");
        return false;
    }

    newCracks = lines;
    Serial.printf("[WPASEC] potfile %u lines\n", (unsigned)lines);
    lastError[0] = '\0';
    return true;
}

struct WpaScanCtx {
    struct Item {
        char path[80];
        char bssid[13];
    };
    Item items[WPASEC_MAX_PENDING];
    uint8_t count;
    uint8_t skipped;
};
static WpaScanCtx s_wpaScan;

static void wpaIdFromName(const char* name, char bssid[13]) {
    bssid[0] = '\0';
    if (bssidFromFilename(name, bssid) && bssid[0]) return;
    char hex[13] = {0};
    if (CapName::extractBssidHex(name, hex) && hex[0]) {
        memcpy(bssid, hex, 13);
        return;
    }
    // SSID-only names still upload — keep a stable 12-char id from the stem.
    const char* base = name;
    const char* slash = strrchr(name, '/');
    if (slash) base = slash + 1;
    char stem[16];
    size_t n = 0;
    for (const char* p = base; *p && *p != '.' && n < 12; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) stem[n++] = c;
    }
    while (n < 12) stem[n++] = '0';
    memcpy(bssid, stem, 12);
    bssid[12] = '\0';
}

static void wpaCollect(const char* name, size_t size, void* raw) {
    WpaScanCtx* ctx = (WpaScanCtx*)raw;
    if (ctx->count >= WPASEC_MAX_PENDING) return;
    if (size == 0 || !isPcapName(name)) return;
    char bssid[13];
    wpaIdFromName(name, bssid);
    if ((bssid[0] && WPASec::isUploaded(bssid)) || WPASec::isUploaded(name)) {
        ctx->skipped++;
        return;
    }
    snprintf(ctx->items[ctx->count].path, sizeof(ctx->items[0].path),
             "%s/%s", Storage::DIR_HANDSHAKES, name);
    memcpy(ctx->items[ctx->count].bssid, bssid, 13);
    ctx->count++;
}

WPASecSyncResult WPASec::syncCaptures(const char* apiKey, WPASecProgressCallback cb) {
    WPASecSyncResult result{};
    result.success = false;
    result.error[0] = '\0';

    if (busy) {
        strncpy(result.error, "already syncing", sizeof(result.error) - 1);
        return result;
    }
    if (!hasApiKey(apiKey)) {
        strncpy(result.error, "invalid API key", sizeof(result.error) - 1);
        return result;
    }
    if (WiFi.status() != WL_CONNECTED) {
        strncpy(result.error, "wifi not connected", sizeof(result.error) - 1);
        return result;
    }

    busy = true;
    freeCacheMemory();
    if (!canSync()) {
        strncpy(result.error, lastError[0] ? lastError : "low heap", sizeof(result.error) - 1);
        busy = false;
        return result;
    }

    loadCache();
    memset(&s_wpaScan, 0, sizeof(s_wpaScan));
    Storage::forEachHandshake(wpaCollect, &s_wpaScan);
    result.skipped = s_wpaScan.skipped;
    Serial.printf("[WPASEC] pending=%u skipped=%u\n", s_wpaScan.count, s_wpaScan.skipped);

    freeCacheMemory();
    uint16_t successMask = 0;

    if (cb) cb("Uploading", 0, s_wpaScan.count);
    for (uint8_t i = 0; i < s_wpaScan.count; i++) {
        if (cb) cb("Uploading", i + 1, s_wpaScan.count);
        if (!canSync()) {
            result.failed = (uint8_t)(result.failed + (s_wpaScan.count - i));
            break;
        }
        if (uploadSingleCapture(s_wpaScan.items[i].path, s_wpaScan.items[i].bssid, apiKey)) {
            result.uploaded++;
            successMask |= (uint16_t)(1u << i);
        } else {
            result.failed++;
        }
        delay(80);
        yield();
    }

    if (result.uploaded > 0) {
        loadCache();
        for (uint8_t i = 0; i < s_wpaScan.count; i++) {
            if (successMask & (1u << i)) markAsUploaded(s_wpaScan.items[i].bssid);
        }
        saveUploadedList();
        freeCacheMemory();
    }

    if (cb) cb("Potfile", s_wpaScan.count, s_wpaScan.count);
    uint16_t newCracks = 0;
    bool potOk = false;
    if (canSync()) {
        potOk = downloadPotfile(apiKey, newCracks);
        if (potOk) {
            result.newCracked = newCracks;
            loadCache();
            result.cracked = getCrackedCount();
        }
    }

    if (potOk || result.uploaded > 0 || result.skipped > 0) {
        result.success = true;
        if (!potOk && lastError[0]) {
            snprintf(result.error, sizeof(result.error), "pot: %s", lastError);
        }
    } else {
        strncpy(result.error, lastError[0] ? lastError : "sync failed", sizeof(result.error) - 1);
    }

    busy = false;
    Serial.printf("[WPASEC] done up=%u fail=%u skip=%u cracked=%u\n",
                  result.uploaded, result.failed, result.skipped, result.cracked);
    return result;
}

bool WPASec::pullPotfile(const char* apiKey, uint16_t& lines) {
    lines = 0;
    if (!hasApiKey(apiKey)) {
        strncpy(lastError, "invalid API key", sizeof(lastError) - 1);
        return false;
    }
    bool ok = downloadPotfile(apiKey, lines);
    if (ok) {
        cacheLoaded = false;
        loadCache();
        lines = getCrackedCount();
    }
    return ok;
}
