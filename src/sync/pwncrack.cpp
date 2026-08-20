// sync/pwncrack.cpp
#include "pwncrack.h"
#include "../storage/littlefs_ops.h"
#include "../cap/hc22000.h"
#include "../cap/capture_name.h"
#include "pot_parse.h"
#include "../net/ap_sta.h"
#include "net_io.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <esp_heap_caps.h>

static const char* PWN_HOST = "pwncrack.org";
static const char* PWN_UPLOAD_PATH = "/upload_handshake";
static const char* PWN_POTFILE_PATH = "/download_potfile_script";
static const size_t PWN_MAX_CACHE = 100;
static const uint8_t PWN_MAX_PENDING = 16;

bool Pwncrack::cacheLoaded = false;
char Pwncrack::lastError[64] = "";
volatile bool Pwncrack::busy = false;
std::vector<Pwncrack::CrackedEntry> Pwncrack::crackedCache;
std::vector<Pwncrack::UploadedEntry> Pwncrack::uploadedCache;

bool Pwncrack::isBusy() { return busy; }
const char* Pwncrack::getLastError() { return lastError; }

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

// "AA-BB-CC-DD-EE-FF.22000" / "name.hc22000" / stem → comparable id
static void normPwnId(const char* in, char* out, size_t outLen) {
    if (!out || outLen < 2) return;
    out[0] = '\0';
    if (!in || !in[0]) return;
    const char* slash = strrchr(in, '/');
    const char* name = slash ? slash + 1 : in;
    char tmp[48];
    strncpy(tmp, name, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char* dot = strrchr(tmp, '.');
    if (dot) {
        if (strcasecmp(dot, ".22000") == 0 || strcasecmp(dot, ".hc22000") == 0 ||
            strcasecmp(dot, ".pcap") == 0 || strcasecmp(dot, ".pcapng") == 0 ||
            strcasecmp(dot, ".cap") == 0) {
            *dot = '\0';
        }
    }
    size_t n = 0;
    for (const char* p = tmp; *p && n + 1 < outLen; p++) {
        if (*p == '-' || *p == ':' || *p == ' ') continue;
        char c = *p;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[n++] = c;
    }
    out[n] = '\0';
}

bool Pwncrack::hasApiKey(const char* key) {
    if (!key || !key[0]) return false;
    size_t n = strlen(key);
    if (n < 4 || n > 64) return false;
    for (size_t i = 0; i < n; i++) {
        if (key[i] < 0x20 || key[i] > 0x7E) return false;
    }
    return true;
}

bool Pwncrack::hasApiKey() {
    return hasApiKey(Net::cfg().pwncrackKey);
}

bool Pwncrack::canSync() {
    uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    uint32_t freeH = ESP.getFreeHeap();
    if (largest < 16000 || freeH < 28000) {
        snprintf(lastError, sizeof(lastError), "low heap %u/%uK",
                 (unsigned)(largest / 1024), (unsigned)(freeH / 1024));
        return false;
    }
    lastError[0] = '\0';
    return true;
}

void Pwncrack::freeCacheMemory() {
    // Never shrink_to_fit — failed realloc on ESP32 is a hard reboot.
    crackedCache.clear();
    uploadedCache.clear();
    cacheLoaded = false;
}

bool Pwncrack::loadUploadedList() {
    uploadedCache.clear();
    if (!Storage::fileExists(Storage::FILE_PWNCRACK_UPLOADED)) return true;
    File f = SD.open(Storage::FILE_PWNCRACK_UPLOADED, "r");
    if (!f) return false;
    char line[64];
    while (f.available() && uploadedCache.size() < PWN_MAX_CACHE) {
        size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = '\0';
        if (n == 0) continue;
        UploadedEntry e{};
        normPwnId(line, e.id, sizeof(e.id));
        if (e.id[0]) uploadedCache.push_back(e);
    }
    f.close();
    return true;
}

bool Pwncrack::saveUploadedList() {
    Storage::ensureDir(Storage::DIR_PWNCRACK);
    File f = SD.open(Storage::FILE_PWNCRACK_UPLOADED, "w");
    if (!f) return false;
    for (const auto& e : uploadedCache) f.println(e.id);
    f.close();
    return true;
}

bool Pwncrack::loadCache() {
    if (cacheLoaded) return true;
    crackedCache.clear();
    loadUploadedList();

    if (Storage::fileExists(Storage::FILE_PWNCRACK_RESULTS)) {
        File f = SD.open(Storage::FILE_PWNCRACK_RESULTS, "r");
        if (f) {
            static char line[1024];
            while (f.available() && crackedCache.size() < PWN_MAX_CACHE) {
                size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
                line[n] = '\0';
                while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = '\0';
                if (n < 3) continue;

                CrackedEntry e{};
                if (strncmp(line, "WPA*", 4) == 0) {
                    char bssidP[18], ssid[33], pass[64];
                    if (!Pot::parseLine(line, bssidP, ssid, pass) || !pass[0]) continue;
                    strncpy(e.password, pass, sizeof(e.password) - 1);
                    if (ssid[0]) strncpy(e.label, ssid, sizeof(e.label) - 1);
                    if (!CapName::extractBssidHex(bssidP, e.id) && ssid[0])
                        strncpy(e.id, ssid, sizeof(e.id) - 1);
                } else {
                    char buf[200];
                    strncpy(buf, line, sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                    char* parts[8];
                    int pc = 0;
                    char* p = buf;
                    while (pc < 8) {
                        parts[pc++] = p;
                        char* c = strchr(p, ':');
                        if (!c) break;
                        *c = '\0';
                        p = c + 1;
                    }
                    if (pc >= 5) {
                        strncpy(e.label, parts[3], sizeof(e.label) - 1);
                        strncpy(e.password, parts[4], sizeof(e.password) - 1);
                        if (!CapName::extractBssidHex(parts[0], e.id))
                            strncpy(e.id, parts[3], sizeof(e.id) - 1);
                    } else if (pc >= 2) {
                        strncpy(e.label, parts[0], sizeof(e.label) - 1);
                        strncpy(e.password, parts[pc - 1], sizeof(e.password) - 1);
                        if (!CapName::extractBssidHex(parts[0], e.id))
                            strncpy(e.id, parts[0], sizeof(e.id) - 1);
                    } else {
                        continue;
                    }
                }
                if (e.password[0]) crackedCache.push_back(e);
            }
            f.close();
        }
    }
    cacheLoaded = true;
    Serial.printf("[PWNCRACK] cache cracked=%u uploaded=%u\n",
                  (unsigned)crackedCache.size(), (unsigned)uploadedCache.size());
    return true;
}

bool Pwncrack::findUploaded(const char* id) {
    if (!id || !id[0]) return false;
    char key[48];
    normPwnId(id, key, sizeof(key));
    if (!key[0]) return false;
    for (const auto& e : uploadedCache) {
        if (strcasecmp(e.id, key) == 0) return true;
    }
    return false;
}

bool Pwncrack::isCracked(const char* key) {
    if (!cacheLoaded) loadCache();
    if (!key) return false;
    for (const auto& e : crackedCache) {
        if (strcasecmp(e.id, key) == 0 || strcasecmp(e.label, key) == 0) return true;
    }
    return false;
}

const char* Pwncrack::getPassword(const char* key) {
    if (!cacheLoaded) loadCache();
    if (!key || !key[0]) return "";
    char norm[48];
    normPwnId(key, norm, sizeof(norm));
    char hex[13] = {0};
    CapName::extractBssidHex(key, hex);
    for (const auto& e : crackedCache) {
        if (strcasecmp(e.id, key) == 0 || strcasecmp(e.label, key) == 0) return e.password;
        if (norm[0] && (strcasecmp(e.id, norm) == 0 || strcasecmp(e.label, norm) == 0))
            return e.password;
        if (hex[0] && strcasecmp(e.id, hex) == 0) return e.password;
        if (CapName::sameSsid(e.label, key)) return e.password;
        char nid[48], nlab[48];
        normPwnId(e.id, nid, sizeof(nid));
        normPwnId(e.label, nlab, sizeof(nlab));
        if (norm[0] && (strcasecmp(nid, norm) == 0 || strcasecmp(nlab, norm) == 0))
            return e.password;
        char ehex[13] = {0};
        if (CapName::extractBssidHex(e.id, ehex) && hex[0] && strcasecmp(ehex, hex) == 0)
            return e.password;
    }
    return "";
}

uint16_t Pwncrack::getCrackedCount() {
    if (!cacheLoaded) loadCache();
    return (uint16_t)crackedCache.size();
}

bool Pwncrack::isUploaded(const char* filename) {
    if (!cacheLoaded) loadCache();
    return findUploaded(filename);
}

void Pwncrack::markAsUploaded(const char* filename) {
    if (!filename || !filename[0] || findUploaded(filename)) return;
    if (uploadedCache.size() >= PWN_MAX_CACHE) return;
    UploadedEntry e{};
    normPwnId(filename, e.id, sizeof(e.id));
    if (!e.id[0]) return;
    uploadedCache.push_back(e);
}

bool Pwncrack::uploadFile(const char* filepath, const char* apiKey) {
    if (!filepath || !apiKey) return false;
    File capFile = SD.open(filepath, "r");
    if (!capFile) {
        snprintf(lastError, sizeof(lastError), "open fail");
        return false;
    }
    size_t fileSize = capFile.size();
    if (fileSize == 0 || fileSize > 200000) {
        capFile.close();
        snprintf(lastError, sizeof(lastError), "bad size");
        return false;
    }

    const char* filename = Storage::baseName(filepath);
    // pwncrack.org only accepts names ending in .hc22000
    char uploadName[64];
    size_t fn = strlen(filename);
    if (fn > 8 && strcasecmp(filename + fn - 8, ".hc22000") == 0) {
        strncpy(uploadName, filename, sizeof(uploadName) - 1);
        uploadName[sizeof(uploadName) - 1] = '\0';
    } else if (fn > 6 && strcasecmp(filename + fn - 6, ".22000") == 0) {
        size_t stem = fn - 6;
        if (stem + 8 >= sizeof(uploadName)) stem = sizeof(uploadName) - 9;
        memcpy(uploadName, filename, stem);
        memcpy(uploadName + stem, ".hc22000", 8);
        uploadName[stem + 8] = '\0';
    } else {
        snprintf(uploadName, sizeof(uploadName), "%s.hc22000", filename);
    }
    Serial.printf("[PWNCRACK] upload %s as %s (%u B)\n",
                  filename, uploadName, (unsigned)fileSize);

    char boundary[32];
    snprintf(boundary, sizeof(boundary), "----Pwn%08lX", (unsigned long)millis());
    char keyPart[192];
    snprintf(keyPart, sizeof(keyPart),
             "--%s\r\nContent-Disposition: form-data; name=\"key\"\r\n\r\n%s\r\n",
             boundary, apiKey);
    char fileHead[256];
    snprintf(fileHead, sizeof(fileHead),
             "--%s\r\nContent-Disposition: form-data; name=\"handshake\"; filename=\"%s\"\r\n"
             "Content-Type: application/octet-stream\r\n\r\n",
             boundary, uploadName);
    char fileTail[48];
    snprintf(fileTail, sizeof(fileTail), "\r\n--%s--\r\n", boundary);
    size_t contentLength = strlen(keyPart) + strlen(fileHead) + fileSize + strlen(fileTail);

    static bool s_forceHttps = false;
    WiFiClientSecure tls;
    WiFiClient plain;
    bool useTls = false;
    if (!ioPwnOpen(tls, plain, useTls, PWN_HOST, s_forceHttps)) {
        capFile.close();
        snprintf(lastError, sizeof(lastError), "connect fail");
        return false;
    }
    Serial.printf("[PWNCRACK] via %s\n", useTls ? "HTTPS" : "HTTP");

    auto sendAll = [&](const uint8_t* data, size_t n) -> bool {
        size_t off = 0;
        while (off < n) {
            size_t w = useTls ? tls.write(data + off, n - off) : plain.write(data + off, n - off);
            if (w == 0) return false;
            off += w;
            yield();
        }
        return true;
    };
    auto sendStr = [&](const char* s) -> bool {
        return sendAll(reinterpret_cast<const uint8_t*>(s), strlen(s));
    };

    char hdr[320];
    snprintf(hdr, sizeof(hdr),
             "POST %s HTTP/1.1\r\nHost: %s\r\n"
             "User-Agent: 0N3P0rK/" ON3PORK_VERSION "\r\n"
             "Content-Type: multipart/form-data; boundary=%s\r\n"
             "Content-Length: %u\r\nConnection: close\r\n\r\n",
             PWN_UPLOAD_PATH, PWN_HOST, boundary, (unsigned)contentLength);
    if (!sendStr(hdr) || !sendStr(keyPart) || !sendStr(fileHead)) {
        capFile.close();
        if (useTls) tls.stop(); else plain.stop();
        snprintf(lastError, sizeof(lastError), "send hdr");
        return false;
    }

    uint8_t buf[512];
    size_t left = fileSize;
    while (left > 0) {
        size_t chunk = left > sizeof(buf) ? sizeof(buf) : left;
        size_t rd = capFile.read(buf, chunk);
        if (rd == 0) break;
        if (!sendAll(buf, rd)) {
            capFile.close();
            if (useTls) tls.stop(); else plain.stop();
            snprintf(lastError, sizeof(lastError), "send body");
            return false;
        }
        left -= rd;
        yield();
    }
    capFile.close();
    if (!sendStr(fileTail)) {
        if (useTls) tls.stop(); else plain.stop();
        snprintf(lastError, sizeof(lastError), "send tail");
        return false;
    }

    unsigned long t0 = millis();
    char status[80] = {0};
    size_t si = 0;
    while (millis() - t0 < 20000) {
        int avail = useTls ? tls.available() : plain.available();
        if (avail <= 0) {
            if ((useTls && !tls.connected()) || (!useTls && !plain.connected())) break;
            delay(10);
            yield();
            continue;
        }
        char ch = useTls ? (char)tls.read() : (char)plain.read();
        if (ch == '\n') break;
        if (ch != '\r' && si + 1 < sizeof(status)) status[si++] = ch;
    }
    status[si] = '\0';
    if (useTls) tls.stop(); else plain.stop();
    Serial.printf("[PWNCRACK] %s\n", status);

    if (ioHttpRedirect(status) && !useTls && !s_forceHttps) {
        Serial.println("[PWNCRACK] HTTP redirected, retry HTTPS");
        s_forceHttps = true;
        bool again = uploadFile(filepath, apiKey);
        s_forceHttps = false;
        return again;
    }

    bool ok = ioHttpOk(status);
    if (!ok) {
        if (strstr(status, "401") || strstr(status, "403")) {
            snprintf(lastError, sizeof(lastError), "bad key");
        } else if (strstr(status, "400")) {
            snprintf(lastError, sizeof(lastError), "rejected file");
        } else if (!status[0]) {
            snprintf(lastError, sizeof(lastError), "no reply");
        } else {
            snprintf(lastError, sizeof(lastError), "http fail");
        }
        return false;
    }
    lastError[0] = '\0';
    return true;
}

bool Pwncrack::downloadPotfile(const char* apiKey, uint16_t& newCracks) {
    newCracks = 0;
    if (!apiKey) return false;

    WiFiClientSecure tls;
    WiFiClient plain;
    bool useTls = false;
    if (!ioPwnOpen(tls, plain, useTls, PWN_HOST)) {
        snprintf(lastError, sizeof(lastError), "pot connect");
        return false;
    }

    char req[280];
    snprintf(req, sizeof(req),
             "GET %s?key=%s HTTP/1.1\r\nHost: %s\r\n"
             "User-Agent: 0N3P0rK/" ON3PORK_VERSION "\r\nConnection: close\r\n\r\n",
             PWN_POTFILE_PATH, apiKey, PWN_HOST);
    size_t reqN = strlen(req);
    size_t off = 0;
    while (off < reqN) {
        size_t w = useTls ? tls.write((const uint8_t*)req + off, reqN - off)
                          : plain.write((const uint8_t*)req + off, reqN - off);
        if (w == 0) {
            if (useTls) tls.stop(); else plain.stop();
            snprintf(lastError, sizeof(lastError), "pot send");
            return false;
        }
        off += w;
    }

    bool headersDone = false;
    bool statusOk = false;
    bool first = true;
    char line[200];
    size_t li = 0;
    unsigned long t0 = millis();

    Storage::ensureDir(Storage::DIR_PWNCRACK);
    File out = SD.open(Storage::FILE_PWNCRACK_RESULTS, "w");
    if (!out) {
        if (useTls) tls.stop(); else plain.stop();
        snprintf(lastError, sizeof(lastError), "pot save");
        return false;
    }

    size_t body = 0;
    bool looksHtml = false;
    bool firstNonWs = false;
    while (millis() - t0 < 25000) {
        int avail = useTls ? tls.available() : plain.available();
        if (avail <= 0) {
            if (headersDone &&
                ((useTls && !tls.connected()) || (!useTls && !plain.connected())))
                break;
            delay(5);
            yield();
            continue;
        }
        int ch = useTls ? tls.read() : plain.read();
        if (ch < 0) continue;
        if (!headersDone) {
            if (ch == '\n') {
                line[li] = '\0';
                if (first) {
                    first = false;
                    statusOk = (strstr(line, "200") != nullptr);
                    Serial.printf("[PWNCRACK] pot %s\n", line);
                }
                if (li == 0 || (li == 1 && line[0] == '\r')) headersDone = true;
                li = 0;
            } else if (ch != '\r' && li + 1 < sizeof(line)) {
                line[li++] = (char)ch;
            }
        } else if (statusOk) {
            // pwncrack.org /download_potfile_script serves an HTML page
            // when the key is bad or the session is wrong. Sniff the first
            // non-whitespace byte: a real potfile starts with "WPA*01*" /
            // hex BSSID, never with '<'.
            if (!firstNonWs && body < 64) {
                if (ch == '<') {
                    looksHtml = true;
                } else if (ch != '\r' && ch != '\n' && ch != ' ' && ch != '\t') {
                    firstNonWs = true;
                }
            }
            out.write((uint8_t)ch);
            body++;
            if (body > 80000) break;
        }
    }
    out.close();
    if (useTls) tls.stop(); else plain.stop();

    if (!headersDone || !statusOk) {
        snprintf(lastError, sizeof(lastError), "pot http");
        return false;
    }

    // Reject HTML responses — the site returned an error/login page, not
    // a potfile. Keep the previous results.txt intact.
    if (looksHtml || body < 4) {
        // rewind the file we just wrote by truncating it to 0
        File trunc = SD.open(Storage::FILE_PWNCRACK_RESULTS, "w");
        if (trunc) trunc.close();
        snprintf(lastError, sizeof(lastError), looksHtml ? "pot html" : "pot empty");
        Serial.printf("[PWNCRACK] pot rejected: %s (body=%u)\n",
                      lastError, (unsigned)body);
        return false;
    }

    uint16_t before = cacheLoaded ? (uint16_t)crackedCache.size() : 0;
    cacheLoaded = false;
    loadCache();
    uint16_t after = (uint16_t)crackedCache.size();
    newCracks = (after > before) ? (uint16_t)(after - before) : 0;
    Serial.printf("[PWNCRACK] pot %u B cracked=%u\n", (unsigned)body, after);
    lastError[0] = '\0';
    return true;
}

struct PwnScanCtx {
    struct Item {
        char path[80];
        char id[32];
    };
    Item items[PWN_MAX_PENDING];
    uint8_t count;
    uint8_t skipped;
};
static PwnScanCtx s_pwnScan;

static bool isHashName(const char* name) {
    size_t n = strlen(name);
    if (n > 6 && strcasecmp(name + n - 6, ".22000") == 0) return true;
    if (n > 8 && strcasecmp(name + n - 8, ".hc22000") == 0) return true;
    return false;
}

static void pwnCollect(const char* name, size_t size, void* raw) {
    PwnScanCtx* ctx = (PwnScanCtx*)raw;
    if (ctx->count >= PWN_MAX_PENDING) return;
    if (size == 0 || !isHashName(name)) return;
    if (Pwncrack::isUploaded(name)) {
        ctx->skipped++;
        return;
    }
    snprintf(ctx->items[ctx->count].path, sizeof(ctx->items[0].path),
             "%s/%s", Storage::DIR_HS, name);
    strncpy(ctx->items[ctx->count].id, name, sizeof(ctx->items[0].id) - 1);
    ctx->count++;
}

PwncrackSyncResult Pwncrack::syncCaptures(const char* apiKey, PwncrackProgressCallback cb) {
    PwncrackSyncResult result{};
    result.success = false;
    result.error[0] = '\0';

    if (busy) {
        strncpy(result.error, "already syncing", sizeof(result.error) - 1);
        return result;
    }
    if (!hasApiKey(apiKey)) {
        strncpy(result.error, "missing API key", sizeof(result.error) - 1);
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

    if (cb) cb("Converting", 0, 1);
    uint16_t conv = Hc22000::convertAllPcaps();
    Serial.printf("[PWNCRACK] converted %u pcap->22000\n", (unsigned)conv);
    Storage::brewHeap();

    loadCache();
    memset(&s_pwnScan, 0, sizeof(s_pwnScan));
    Storage::forEachPwn(pwnCollect, &s_pwnScan);
    result.skipped = s_pwnScan.skipped;

    if (cb) cb("Uploading", 0, s_pwnScan.count);
    for (uint8_t i = 0; i < s_pwnScan.count; i++) {
        if (cb) cb("Uploading", i + 1, s_pwnScan.count);
        if (uploadFile(s_pwnScan.items[i].path, apiKey)) {
            markAsUploaded(s_pwnScan.items[i].id);
            result.uploaded++;
        } else {
            result.failed++;
        }
        delay(80);
        yield();
    }
    if (result.uploaded > 0) saveUploadedList();

    if (cb) cb("Potfile", s_pwnScan.count, s_pwnScan.count);
    uint16_t newCracks = 0;
    bool potOk = downloadPotfile(apiKey, newCracks);
    if (potOk) {
        result.newCracked = newCracks;
        result.cracked = getCrackedCount();
        result.success = true;
    } else {
        result.success = (result.uploaded > 0 || result.skipped > 0);
        if (!result.success) {
            strncpy(result.error, lastError[0] ? lastError : "pot fail", sizeof(result.error) - 1);
        } else {
            snprintf(result.error, sizeof(result.error), "pot: %s", lastError);
        }
    }

    busy = false;
    Serial.printf("[PWNCRACK] done up=%u fail=%u skip=%u cracked=%u\n",
                  result.uploaded, result.failed, result.skipped, result.cracked);
    return result;
}

bool Pwncrack::uploadOneFile(const char* filepath, const char* apiKey) {
    if (busy) {
        strncpy(lastError, "busy", sizeof(lastError) - 1);
        return false;
    }
    if (!hasApiKey(apiKey)) {
        strncpy(lastError, "invalid API key", sizeof(lastError) - 1);
        return false;
    }
    if (!filepath || !filepath[0]) {
        strncpy(lastError, "no file", sizeof(lastError) - 1);
        return false;
    }
    busy = true;
    bool ok = uploadFile(filepath, apiKey);
    if (ok) {
        markAsUploaded(Storage::baseName(filepath));
        saveUploadedList();
        lastError[0] = '\0';
    }
    busy = false;
    return ok;
}
