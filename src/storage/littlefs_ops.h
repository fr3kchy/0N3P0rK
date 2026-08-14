// SD card storage - OnePork layout on the Cardputer SD.
// Namespace stays Storage so Stamp cap/sync keep compiling.
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>

namespace Storage {

static const uint8_t FILE_NAME_MAX = 32;

bool begin();
bool available();

struct Stats {
    size_t total;
    size_t used;
    size_t free;
    uint16_t handshakes;
    uint16_t results;
};
Stats stats();

const char* baseName(const char* path);

typedef void (*FileVisitor)(const char* name, size_t size, void* ctx);
uint16_t forEachInDir(const char* dir, FileVisitor fn, void* ctx);
uint16_t forEachHandshake(FileVisitor fn, void* ctx);
uint16_t forEachPwn(FileVisitor fn, void* ctx);

uint16_t listHandshakes(char out[][FILE_NAME_MAX], uint16_t max);
uint16_t listResults(char out[][FILE_NAME_MAX], uint16_t max);

// loot / wpa-sec + pwncrack (keys, results, captures)
const char* const DIR_LOOT       = "/loot";
const char* const DIR_WPASEC     = "/loot/wpa-sec";
const char* const DIR_PWNCRACK   = "/loot/pwncrack";
const char* const DIR_EVILPIG    = "/loot/evilpig";
const char* const DIR_HANDSHAKES = "/loot/wpa-sec";
const char* const DIR_RESULTS    = "/loot/wpa-sec";

const char* const FILE_WPASEC_RESULTS    = "/loot/wpa-sec/wpasec_results.txt";
const char* const FILE_WPASEC_UPLOADED   = "/loot/wpa-sec/wpasec_uploaded.txt";
const char* const FILE_WPASEC_KEY        = "/loot/wpa-sec/wpasec_key.txt";
const char* const FILE_PWNCRACK_RESULTS  = "/loot/pwncrack/results.txt";
const char* const FILE_PWNCRACK_UPLOADED = "/loot/pwncrack/uploaded.txt";
const char* const FILE_PWNCRACK_KEY      = "/loot/pwncrack/key.txt";

bool ensureDir(const char* path);
bool removeFile(const char* path);
bool fileExists(const char* path);
size_t fileSize(const char* path);
bool formatStorage();

// Read first line of a key file into dest. Returns true if non-empty.
bool loadKeyFile(const char* path, char* dest, size_t destLen);
void loadKeysIntoNet();
void migrateLegacy();

} // namespace Storage
