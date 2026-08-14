#include "littlefs_ops.h"
#include "../net/ap_sta.h"
#include "../cap/capture_name.h"
#include <SPI.h>
#include <string.h>
#include <strings.h>
#include <esp_random.h>

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

    SD.mkdir(DIR_ROOT);
    SD.mkdir(DIR_WPASEC);
    SD.mkdir(DIR_PWNCRACK);
    SD.mkdir(DIR_EVILPIG);
    SD.mkdir(DIR_PIGPASS);
    SD.mkdir(DIR_PASSWORLD);
    SD.mkdir(DIR_IR);
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
    // Our previous /loot layout → /0N3P0rK. No m5porkchop.
    migrateAll("/loot/wpa-sec", DIR_WPASEC);
    migrateAll("/loot/pwncrack", DIR_PWNCRACK);
    migrateAll("/loot/evilpig", DIR_EVILPIG);
    migrateAll("/loot/pigpass", DIR_PIGPASS);
    migrateAll("/loot/Passworld", DIR_PASSWORLD);
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
    if (loadKeyFile(FILE_WPASEC_KEY, buf, sizeof(buf))) {
        Net::setWpaSecKey(buf);
        Serial.println("[SD] wpasec key loaded");
    }
    if (loadKeyFile(FILE_PWNCRACK_KEY, buf, sizeof(buf))) {
        Net::setPwncrackKey(buf);
        Serial.println("[SD] pwncrack key loaded");
    }
}

uint8_t eatRandomLoot(uint8_t want) {
    if (!s_mounted || want == 0) return 0;
    struct Item { char path[80]; };
    Item list[32];
    uint8_t n = 0;
    auto collect = [&](const char* dir) {
        File root = SD.open(dir);
        if (!root || !root.isDirectory()) {
            if (root) root.close();
            return;
        }
        File f = root.openNextFile();
        while (f && n < 32) {
            if (!f.isDirectory()) {
                const char* name = baseName(f.name());
                bool loot = endsWithCI(name, ".pcap") || endsWithCI(name, ".pcapng") ||
                            endsWithCI(name, ".cap") || endsWithCI(name, ".22000") ||
                            endsWithCI(name, ".hc22000");
                if (loot) {
                    snprintf(list[n].path, sizeof(list[n].path), "%s/%s", dir, name);
                    n++;
                }
            }
            f.close();
            f = root.openNextFile();
        }
        if (f) f.close();
        root.close();
    };
    collect(DIR_WPASEC);
    collect(DIR_PWNCRACK);
    if (n == 0) return 0;
    if (want > n) want = n;
    uint8_t eaten = 0;
    for (uint8_t k = 0; k < want && n > 0; k++) {
        uint8_t pick = (uint8_t)(esp_random() % n);
        if (SD.remove(list[pick].path)) {
            Serial.printf("[LOOT] wolf ate %s\n", list[pick].path);
            eaten++;
        }
        list[pick] = list[n - 1];
        n--;
    }
    return eaten;
}

static bool isProtectedName(const char* name) {
    if (!name || !name[0]) return true;
    if (strncasecmp(name, "wpasec_", 7) == 0) return true;
    if (strcasecmp(name, "key.txt") == 0) return true;
    if (strcasecmp(name, "key.txt.imported") == 0) return true;
    if (strcasecmp(name, "results.txt") == 0) return true;
    if (strcasecmp(name, "uploaded.txt") == 0) return true;
    return false;
}

static bool isLootCap(const char* name) {
    return endsWithCI(name, ".pcap") || endsWithCI(name, ".pcapng") ||
           endsWithCI(name, ".cap") || endsWithCI(name, ".22000") ||
           endsWithCI(name, ".hc22000");
}

static int lootScore(const char* name, uint32_t size) {
    int s = 0;
    if (endsWithCI(name, "_hs.22000")) s += 300;
    else if (endsWithCI(name, ".hc22000")) s += 80;
    else if (endsWithCI(name, ".22000")) s += 120;
    else if (endsWithCI(name, ".pcap") || endsWithCI(name, ".pcapng") ||
             endsWithCI(name, ".cap"))
        s += 200;
    char ssid[33] = {0};
    if (CapName::extractSsidFromName(name, ssid)) {
        if (strcasecmp(ssid, "HIDDEN") == 0) s += 20;
        else s += 400;
    }
    if (size > 24) {
        uint32_t bonus = size / 512;
        if (bonus > 60) bonus = 60;
        s += (int)bonus;
    }
    return s;
}

static void dropCompanion(const char* dir, const char* name) {
    const char* base = baseName(name);
    const char* dot = strrchr(base, '.');
    size_t n = dot ? (size_t)(dot - base) : strlen(base);
    if (n > 3 && strncmp(base + n - 3, "_hs", 3) == 0) n -= 3;
    if (n == 0) return;
    char path[96];
    snprintf(path, sizeof(path), "%s/%.*s.txt", dir, (int)n, base);
    if (SD.exists(path)) SD.remove(path);
}

static uint8_t compactOneDir(const char* dir) {
    struct Cand {
        char name[48];
        char hex[13];
        uint32_t size;
        int16_t score;
    };
    Cand list[48];
    uint8_t n = 0;
    File root = SD.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 0;
    }
    File f = root.openNextFile();
    while (f && n < 48) {
        if (!f.isDirectory()) {
            const char* name = baseName(f.name());
            if (isLootCap(name) && !isProtectedName(name)) {
                Cand& c = list[n];
                memset(&c, 0, sizeof(c));
                strncpy(c.name, name, sizeof(c.name) - 1);
                c.size = (uint32_t)f.size();
                CapName::extractBssidHex(name, c.hex);
                if (!c.hex[0]) {
                    char hx[13] = {0}, ss[33] = {0};
                    if (CapName::metaFrom22000File(dir, name, hx, ss) && hx[0])
                        memcpy(c.hex, hx, 13);
                }
                c.score = (int16_t)lootScore(name, c.size);
                if (c.hex[0]) n++;
            }
        }
        f.close();
        f = root.openNextFile();
    }
    if (f) f.close();
    root.close();

    bool used[48];
    memset(used, 0, sizeof(used));
    uint8_t killed = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (used[i]) continue;
        uint8_t best = i;
        for (uint8_t j = i + 1; j < n; j++) {
            if (used[j]) continue;
            if (strcmp(list[j].hex, list[i].hex) != 0) continue;
            if (list[j].score > list[best].score) best = j;
        }
        for (uint8_t j = 0; j < n; j++) {
            if (strcmp(list[j].hex, list[i].hex) != 0) continue;
            used[j] = true;
            if (j == best) continue;
            char path[96];
            snprintf(path, sizeof(path), "%s/%s", dir, list[j].name);
            if (SD.remove(path)) {
                dropCompanion(dir, list[j].name);
                killed++;
                Serial.printf("[LOOT] dedup %s\n", list[j].name);
            }
        }
    }
    return killed;
}

uint8_t compactLoot() {
    if (!s_mounted) return 0;
    uint8_t n = (uint8_t)(compactOneDir(DIR_WPASEC) + compactOneDir(DIR_PWNCRACK));
    if (n) Serial.printf("[LOOT] compact removed %u dupes\n", (unsigned)n);
    return n;
}

} // namespace Storage
