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
static const size_t PWN_MAX_CACHE = 512;
static const char* PWN_PENDING = "/0N3P0rK/pwncrack/_pending.txt";

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
static bool isHex12Field(const char* s) {
    if (!s) return false;
    size_t n = 0;
    for (; s[n]; n++) {
        if (!isxdigit((unsigned char)s[n])) return false;
    }
    return n == 12;
}

static void copyField(char* dst, size_t dstLen, const char* src) {
    if (!dst || dstLen == 0) return;
    dst[0] = '\0';
    if (!src) return;
    strncpy(dst, src, dstLen - 1);
    dst[dstLen - 1] = '\0';
}

static void hex12Upper(const char* src, char* dst) {
    for (int i = 0; i < 12; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[12] = '\0';
}

// pwncrack plugin / hcx pot: hash:AP:STA:SSID:password
// Do NOT take the 32-hex hash as BSSID (extractBssidHex would keep the first 12).
static bool parsePwnPotLine(const char* line, char* id, char* label, char* pass) {
    if (id) id[0] = '\0';
    if (label) label[0] = '\0';
    if (pass) pass[0] = '\0';
    if (!line || line[0] == '#' || line[0] == '<' || line[0] == '\0') return false;

    if (strncmp(line, "WPA*", 4) == 0) {
        char bssidP[18], ssid[33], pw[64];
        if (!Pot::parseLine(line, bssidP, ssid, pw) || !pw[0]) return false;
        copyField(pass, 64, pw);
        if (ssid[0]) copyField(label, 33, ssid);
        if (!CapName::extractBssidHex(bssidP, id) && ssid[0])
            copyField(id, 33, ssid);
        return pass && pass[0];
    }

    const char* col[4];
    int n = 0;
    const char* p = line;
    while (n < 4) {
        const char* c = strchr(p, ':');
        if (!c) break;
        col[n++] = c;
        p = c + 1;
    }
    if (n >= 4) {
        char f0[40], f1[20];
        size_t l0 = (size_t)(col[0] - line);
        if (l0 >= sizeof(f0)) l0 = sizeof(f0) - 1;
        memcpy(f0, line, l0);
        f0[l0] = '\0';
        size_t l1 = (size_t)(col[1] - (col[0] + 1));
        if (l1 >= sizeof(f1)) l1 = sizeof(f1) - 1;
        memcpy(f1, col[0] + 1, l1);
        f1[l1] = '\0';

        size_t sl = (size_t)(col[3] - (col[2] + 1));
        if (sl > 32) sl = 32;
        memcpy(label, col[2] + 1, sl);
        label[sl] = '\0';
        copyField(pass, 64, col[3] + 1);

        if (isHex12Field(f1)) hex12Upper(f1, id);
        else if (isHex12Field(f0)) hex12Upper(f0, id);
        else if (label[0]) copyField(id, 33, label);
        return pass[0] != '\0';
    }

    char buf[200];
    copyField(buf, sizeof(buf), line);
    char* parts[8];
    int pc = 0;
    char* q = buf;
    while (pc < 8) {
        parts[pc++] = q;
        char* c = strchr(q, ':');
        if (!c) break;
        *c = '\0';
        q = c + 1;
    }
    if (pc >= 2) {
        copyField(label, 33, parts[0]);
        copyField(pass, 64, parts[pc - 1]);
        if (isHex12Field(parts[0])) hex12Upper(parts[0], id);
        else copyField(id, 33, parts[0]);
        return pass[0] != '\0';
    }
    return false;
}

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
    const char* tmp = "/0N3P0rK/pwncrack/uploaded.tmp";
    File f = SD.open(tmp, "w");
    if (!f) return false;
    for (const auto& e : uploadedCache) {
        if (e.id[0]) f.println(e.id);
    }
    f.flush();
    f.close();
    SD.remove(Storage::FILE_PWNCRACK_UPLOADED);
    if (!SD.rename(tmp, Storage::FILE_PWNCRACK_UPLOADED)) {
        SD.remove(tmp);
        return false;
    }
    Serial.printf("[PWNCRACK] saved %u uploaded\n", (unsigned)uploadedCache.size());
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
                if (!parsePwnPotLine(line, e.id, e.label, e.password)) continue;
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
        if (!e.password[0]) continue;
        if (strcasecmp(e.id, key) == 0 || strcasecmp(e.label, key) == 0) return e.password;
        if (norm[0] && (strcasecmp(e.id, norm) == 0 || strcasecmp(e.label, norm) == 0))
            return e.password;
        if (hex[0] && strcasecmp(e.id, hex) == 0) return e.password;
        if (e.label[0] && CapName::sameSsid(e.label, key)) return e.password;
        if (e.id[0] && CapName::sameSsid(e.id, key)) return e.password;
        char nid[48], nlab[48];
        normPwnId(e.id, nid, sizeof(nid));
        normPwnId(e.label, nlab, sizeof(nlab));
        if (norm[0] && (strcasecmp(nid, norm) == 0 || strcasecmp(nlab, norm) == 0))
            return e.password;
        if (hex[0]) {
            char ehex[13] = {0};
            if (CapName::extractBssidHex(e.id, ehex) && strcasecmp(ehex, hex) == 0)
                return e.password;
            ehex[0] = '\0';
            if (CapName::extractBssidHex(e.label, ehex) && strcasecmp(ehex, hex) == 0)
                return e.password;
        }
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
    if (fileSize == 0) {
        capFile.close();
        snprintf(lastError, sizeof(lastError), "empty");
        return false;
    }
    if (fileSize > kHsUploadMax) {
        capFile.close();
        snprintf(lastError, sizeof(lastError), "too big");
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
    WiFiClient& sock = useTls ? (WiFiClient&)tls : (WiFiClient&)plain;
    sock.setTimeout(60000);
    ioXfer().sent = 0;
    ioXfer().size = (uint32_t)fileSize;
    ioXferPaint(true);

    auto sendAll = [&](const uint8_t* data, size_t n) -> bool {
        return ioWriteAll(sock, data, n);
    };
    auto sendStr = [&](const char* s) -> bool {
        return ioWriteAll(sock, s);
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

    uint8_t buf[1460];
    size_t left = fileSize;
    size_t sent = 0;
    while (left > 0) {
        size_t chunk = left > sizeof(buf) ? sizeof(buf) : left;
        size_t rd = capFile.read(buf, chunk);
        if (rd == 0) break;
        if (!sendAll(buf, rd)) {
            capFile.close();
            sock.stop();
            snprintf(lastError, sizeof(lastError), "send body");
            return false;
        }
        left -= rd;
        sent += rd;
        ioXfer().sent = (uint32_t)sent;
        ioXferPaint(false);
        yield();
    }
    capFile.close();
    if (!sendStr(fileTail)) {
        sock.stop();
        snprintf(lastError, sizeof(lastError), "send tail");
        return false;
    }
    ioXfer().sent = (uint32_t)fileSize;
    ioXferPaint(true);

    char status[80] = {0};
    ioReadStatusLine(sock, status, sizeof(status), 45000);
    ioDrain(sock, 20000);
    sock.stop();
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
    const char* potTmp = "/0N3P0rK/pwncrack/results.tmp";
    SD.remove(potTmp);

    // Receive first (headers + HTML sniff). Only then write the potfile.
    // 1-byte SD writes on a full M5Launcher card miss the 25s window.
    File out;
    bool outOpen = false;
    uint8_t wbuf[512];
    size_t wlen = 0;
    size_t body = 0;
    bool looksHtml = false;
    bool firstNonWs = false;
    auto flushW = [&]() {
        if (!outOpen || wlen == 0) return;
        out.write(wbuf, wlen);
        wlen = 0;
    };
    auto putB = [&](uint8_t b) {
        if (looksHtml) return;
        if (!outOpen) {
            out = SD.open(potTmp, "w");
            if (!out) return;
            outOpen = true;
        }
        if (wlen >= sizeof(wbuf)) flushW();
        wbuf[wlen++] = b;
        body++;
    };

    while (millis() - t0 < 45000) {
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
                    Serial.printf("[PWNCRACK] pot %s via %s\n", line, useTls ? "HTTPS" : "HTTP");
                }
                if (li == 0 || (li == 1 && line[0] == '\r')) headersDone = true;
                li = 0;
            } else if (ch != '\r' && li + 1 < sizeof(line)) {
                line[li++] = (char)ch;
            }
        } else if (statusOk && !looksHtml) {
            if (!firstNonWs) {
                if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                    continue;
                }
                firstNonWs = true;
                if (ch == '<') {
                    looksHtml = true;
                    continue;
                }
            }
            putB((uint8_t)ch);
            if (body > 80000) break;
        }
    }
    flushW();
    if (outOpen) {
        out.flush();
        out.close();
    }
    if (useTls) tls.stop(); else plain.stop();

    if (!headersDone || !statusOk) {
        SD.remove(potTmp);
        snprintf(lastError, sizeof(lastError), "pot http");
        return false;
    }

    if (looksHtml || body < 4) {
        SD.remove(potTmp);
        snprintf(lastError, sizeof(lastError), looksHtml ? "pot html" : "pot empty");
        Serial.printf("[PWNCRACK] pot rejected: %s (body=%u), keep old\n",
                      lastError, (unsigned)body);
        return false;
    }
    if (!Storage::commitTempFile(potTmp, Storage::FILE_PWNCRACK_RESULTS)) {
        snprintf(lastError, sizeof(lastError), "pot save");
        return false;
    }

    uint16_t before = cacheLoaded ? (uint16_t)crackedCache.size() : 0;
    cacheLoaded = false;
    loadCache();
    if (crackedCache.empty() &&
        Storage::fileSize(Storage::FILE_PWNCRACK_RESULTS) > 4) {
        Serial.println("[PWNCRACK] cache empty after write, wait SD");
        Storage::sdSettle();
        cacheLoaded = false;
        loadCache();
    }
    uint16_t after = (uint16_t)crackedCache.size();
    newCracks = (after > before) ? (uint16_t)(after - before) : 0;
    Serial.printf("[PWNCRACK] pot %u B cracked=%u\n", (unsigned)body, after);
    lastError[0] = '\0';
    return true;
}

struct PwnPendCtx {
    File* out;
    uint16_t count;
    uint16_t skipped;
};

static bool isHashName(const char* name) {
    size_t n = strlen(name);
    if (n > 6 && strcasecmp(name + n - 6, ".22000") == 0) return true;
    if (n > 8 && strcasecmp(name + n - 8, ".hc22000") == 0) return true;
    return false;
}

static void pwnCollect(const char* name, size_t size, void* raw) {
    PwnPendCtx* ctx = (PwnPendCtx*)raw;
    if (!ctx || !ctx->out || size == 0 || !isHashName(name)) return;
    if (Pwncrack::isUploaded(name)) {
        ctx->skipped++;
        return;
    }
    ctx->out->println(name);
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

    ioXferPhase("CONVERT", 0, 0);
    if (cb) cb("Converting", 0, 1);
    uint16_t conv = Hc22000::convertAllPcaps();
    Serial.printf("[PWNCRACK] converted %u pcap->22000\n", (unsigned)conv);
    Storage::brewHeap();

    loadCache();
    Storage::ensureDir(Storage::DIR_PWNCRACK);
    SD.remove(PWN_PENDING);
    File pendOut = SD.open(PWN_PENDING, "w");
    PwnPendCtx pend{};
    pend.out = pendOut ? &pendOut : nullptr;
    if (pend.out) Storage::forEachPwn(pwnCollect, &pend);
    if (pendOut) pendOut.close();
    result.skipped = pend.skipped;
    Serial.printf("[PWNCRACK] pending=%u skipped=%u\n", pend.count, pend.skipped);

    crackedCache.clear();
    // uploaded list stays; cracked is stale until we reload from SD after potfile

    if (cb) cb("Uploading", 0, pend.count);
    ioXferPhase("UPLOAD", 0, pend.count);
    if (pend.count > 0) {
        File pendIn = SD.open(PWN_PENDING, "r");
        uint16_t i = 0;
        char name[64];
        while (pendIn && pendIn.available()) {
            size_t n = pendIn.readBytesUntil('\n', name, sizeof(name) - 1);
            name[n] = '\0';
            while (n > 0 && (name[n - 1] == '\r' || name[n - 1] == ' ')) name[--n] = '\0';
            if (n == 0) continue;
            i++;
            if (cb) cb("Uploading", i, pend.count);
            ioXferPhase("UPLOAD", i, pend.count);
            char path[80];
            snprintf(path, sizeof(path), "%s/%s", Storage::DIR_HS, name);
            if (uploadFile(path, apiKey)) {
                markAsUploaded(name);
                result.uploaded++;
                ioXfer().ok++;
            } else {
                result.failed++;
                ioXfer().fail++;
            }
            ioXferPaint(true);
            delay(80);
            yield();
        }
        if (pendIn) pendIn.close();
    }
    SD.remove(PWN_PENDING);
    if (result.uploaded > 0) saveUploadedList();
    Storage::sdSettle();

    if (cb) cb("Potfile", pend.count, pend.count);
    ioXferPhase("POTFILE", pend.count, pend.count);
    uint16_t newCracks = 0;
    bool potOk = downloadPotfile(apiKey, newCracks);
    if (potOk) result.newCracked = newCracks;
    cacheLoaded = false;
    loadCache();
    result.cracked = getCrackedCount();
    if (potOk) {
        result.success = true;
    } else {
        result.success = (result.uploaded > 0 || result.skipped > 0 || result.cracked > 0);
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
        loadCache();
        markAsUploaded(Storage::baseName(filepath));
        saveUploadedList();
        lastError[0] = '\0';
    }
    busy = false;
    return ok;
}
