#include "littlefs_ops.h"
#include "../net/ap_sta.h"
#include <SPI.h>
#include <string.h>

namespace Storage {

static bool s_mounted = false;
static SPIClass s_sdSPI(FSPI);

static constexpr int SD_CS_PIN   = 12;
static constexpr int SD_MOSI_PIN = 14;
static constexpr int SD_MISO_PIN = 39;
static constexpr int SD_SCK_PIN  = 40;

const char* baseName(const char* path) {
    if (!path || !path[0]) return "";
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void copyName(const char* src, char* dst, size_t dstLen) {
    size_t i = 0;
    for (; i + 1 < dstLen && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

static uint16_t countFiles(const char* dir) {
    uint16_t n = 0;
    if (!s_mounted) return 0;
    File root = SD.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 0;
    }
    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) n++;
        f.close();
        f = root.openNextFile();
    }
    root.close();
    return n;
}

static uint16_t listDir(const char* dir, char out[][FILE_NAME_MAX], uint16_t max) {
    uint16_t n = 0;
    if (!s_mounted) return 0;
    File root = SD.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 0;
    }
    File f = root.openNextFile();
    while (f && n < max) {
        if (!f.isDirectory()) {
            copyName(baseName(f.name()), out[n], FILE_NAME_MAX);
            n++;
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();
    return n;
}

static void prepareBus() {
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    s_sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    delay(20);
}

bool begin() {
    if (s_mounted) return true;

    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
    prepareBus();

    const uint32_t speeds[] = {25000000, 20000000, 10000000, 8000000, 4000000, 1000000};
    for (uint8_t i = 0; i < 6 && !s_mounted; i++) {
        if (i > 0) {
            SD.end();
            delay(80);
            prepareBus();
        }
        if (SD.begin(SD_CS_PIN, s_sdSPI, speeds[i])) {
            Serial.printf("[SD] mounted %luMHz\n", speeds[i] / 1000000UL);
            s_mounted = true;
        }
    }

    if (!s_mounted) {
        Serial.println("[SD] mount failed");
        return false;
    }

    SD.mkdir(DIR_LOOT);
    SD.mkdir(DIR_WPASEC);
    SD.mkdir(DIR_PWNCRACK);
    SD.mkdir(DIR_EVILPIG);
    SD.mkdir("/loot/pigpass");
    SD.mkdir("/loot/Passworld");
    migrateLegacy();
    return true;
}

bool available() { return s_mounted; }

bool ensureDir(const char* path) {
    if (!s_mounted || !path) return false;
    if (SD.exists(path)) {
        File f = SD.open(path);
        bool ok = f && f.isDirectory();
        if (f) f.close();
        if (ok) return true;
    }
    if (SD.mkdir(path)) return true;
    File f = SD.open(path, "r");
    bool ok = f && f.isDirectory();
    if (f) f.close();
    return ok;
}

bool removeFile(const char* path) {
    if (!s_mounted || !path) return false;
    return SD.remove(path);
}

bool fileExists(const char* path) {
    if (!s_mounted || !path) return false;
    return SD.exists(path);
}

size_t fileSize(const char* path) {
    if (!s_mounted || !path) return 0;
    File f = SD.open(path, "r");
    if (!f) return 0;
    size_t n = f.size();
    f.close();
    return n;
}

Stats stats() {
    Stats s{};
    if (!s_mounted) return s;
    s.total = SD.totalBytes();
    s.used  = SD.usedBytes();
    s.free  = (s.total > s.used) ? (s.total - s.used) : 0;
    s.handshakes = countFiles(DIR_WPASEC);
    s.results = countFiles(DIR_PWNCRACK);
    return s;
}

uint16_t forEachInDir(const char* dir, FileVisitor fn, void* ctx) {
    if (!s_mounted || !fn || !dir) return 0;
    File root = SD.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 0;
    }
    uint16_t n = 0;
    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            fn(baseName(f.name()), f.size(), ctx);
            n++;
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();
    return n;
}

uint16_t forEachHandshake(FileVisitor fn, void* ctx) {
    return forEachInDir(DIR_WPASEC, fn, ctx);
}

uint16_t forEachPwn(FileVisitor fn, void* ctx) {
    return forEachInDir(DIR_PWNCRACK, fn, ctx);
}

uint16_t listHandshakes(char out[][FILE_NAME_MAX], uint16_t max) {
    return listDir(DIR_WPASEC, out, max);
}

uint16_t listResults(char out[][FILE_NAME_MAX], uint16_t max) {
    return listDir(DIR_WPASEC, out, max);
}

static bool endsWithCI(const char* name, const char* suf) {
    size_t n = strlen(name), s = strlen(suf);
    if (n < s) return false;
    for (size_t i = 0; i < s; i++) {
        char a = name[n - s + i], b = suf[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static bool moveInto(const char* fromPath, const char* toDir) {
    const char* name = baseName(fromPath);
    if (!name[0]) return false;
    char dest[96];
    snprintf(dest, sizeof(dest), "%s/%s", toDir, name);
    if (strcmp(fromPath, dest) == 0) return true;
    if (SD.exists(dest)) {
        Serial.printf("[SD] keep %s (already in loot)\n", name);
        return true;
    }
    if (SD.rename(fromPath, dest)) {
        Serial.printf("[SD] moved %s -> %s\n", fromPath, dest);
        return true;
    }
    File in = SD.open(fromPath, "r");
    if (!in) return false;
    File out = SD.open(dest, "w");
    if (!out) {
        in.close();
        return false;
    }
    uint8_t buf[512];
    while (in.available()) {
        int n = in.read(buf, sizeof(buf));
        if (n <= 0) break;
        out.write(buf, (size_t)n);
        yield();
    }
    in.close();
    out.close();
    SD.remove(fromPath);
    Serial.printf("[SD] copied %s -> %s\n", fromPath, dest);
    return true;
}

static void migrateDirByExt(const char* from) {
    File root = SD.open(from);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }
    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            char path[96];
            snprintf(path, sizeof(path), "%s/%s", from, baseName(f.name()));
            const char* name = baseName(f.name());
            f.close();
            if (endsWithCI(name, ".22000") || endsWithCI(name, ".hc22000"))
                moveInto(path, DIR_PWNCRACK);
            else if (endsWithCI(name, ".pcap") || endsWithCI(name, ".pcapng"))
                moveInto(path, DIR_WPASEC);
            else
                moveInto(path, DIR_WPASEC);
        } else {
            f.close();
        }
        f = root.openNextFile();
    }
    root.close();
}

static void migrateAll(const char* from, const char* toDir) {
    File root = SD.open(from);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }
    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            char path[96];
            snprintf(path, sizeof(path), "%s/%s", from, baseName(f.name()));
            f.close();
            moveInto(path, toDir);
        } else {
            f.close();
        }
        f = root.openNextFile();
    }
    root.close();
}

void migrateLegacy() {
    if (!s_mounted) return;
    migrateDirByExt("/m5porkchop/handshakes");
    migrateDirByExt("/handshakes");
    migrateAll("/m5porkchop/wpa-sec", DIR_WPASEC);
    migrateAll("/m5porkchop/pwncrack", DIR_PWNCRACK);
    migrateAll("/m5porkchop/evilpig", DIR_EVILPIG);
}

bool formatStorage() {
    return false;
}

bool loadKeyFile(const char* path, char* dest, size_t destLen) {
    if (!s_mounted || !path || !dest || destLen < 2) return false;
    if (!SD.exists(path)) return false;
    File f = SD.open(path, "r");
    if (!f) return false;
    size_t n = f.readBytes(dest, destLen - 1);
    f.close();
    dest[n] = '\0';
    while (n > 0 && (dest[n - 1] == '\n' || dest[n - 1] == '\r' || dest[n - 1] == ' '))
        dest[--n] = '\0';
    return dest[0] != '\0';
}

void loadKeysIntoNet() {
    char buf[65];
    if (loadKeyFile(FILE_WPASEC_KEY, buf, sizeof(buf)) ||
        loadKeyFile("/m5porkchop/wpa-sec/wpasec_key.txt", buf, sizeof(buf))) {
        Net::setWpaSecKey(buf);
        Serial.println("[SD] wpasec key loaded");
    }
    if (loadKeyFile(FILE_PWNCRACK_KEY, buf, sizeof(buf)) ||
        loadKeyFile("/m5porkchop/pwncrack/key.txt", buf, sizeof(buf))) {
        Net::setPwncrackKey(buf);
        Serial.println("[SD] pwncrack key loaded");
    }
}

} // namespace Storage
