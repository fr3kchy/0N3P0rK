// SD card storage - OnePork layout on the Cardputer SD.
// Namespace stays Storage so Stamp cap/sync keep compiling.
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>

namespace Storage {

static const uint8_t FILE_NAME_MAX = 48;

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

// /0N3P0rK — project SD root (wpa-sec, pwncrack, pigpass, Passworld, evilpig)
const char* const DIR_ROOT       = "/0N3P0rK";
const char* const DIR_LOOT       = "/0N3P0rK";
const char* const DIR_WPASEC     = "/0N3P0rK/wpa-sec";
const char* const DIR_PWNCRACK   = "/0N3P0rK/pwncrack";
const char* const DIR_EVILPIG    = "/0N3P0rK/evilpig";
const char* const DIR_PIGPASS    = "/0N3P0rK/pigpass";
const char* const DIR_PASSWORLD  = "/0N3P0rK/Passworld";
const char* const DIR_HANDSHAKES = "/0N3P0rK/wpa-sec";
const char* const DIR_RESULTS    = "/0N3P0rK/wpa-sec";

const char* const FILE_WPASEC_RESULTS    = "/0N3P0rK/wpa-sec/wpasec_results.txt";
const char* const FILE_WPASEC_UPLOADED   = "/0N3P0rK/wpa-sec/wpasec_uploaded.txt";
const char* const FILE_WPASEC_KEY        = "/0N3P0rK/wpa-sec/wpasec_key.txt";
const char* const FILE_PWNCRACK_RESULTS  = "/0N3P0rK/pwncrack/results.txt";
const char* const FILE_PWNCRACK_UPLOADED = "/0N3P0rK/pwncrack/uploaded.txt";
const char* const FILE_PWNCRACK_KEY      = "/0N3P0rK/pwncrack/key.txt";

bool ensureDir(const char* path);
bool removeFile(const char* path);
bool fileExists(const char* path);
size_t fileSize(const char* path);
bool formatStorage();

// Read first line of a key file into dest. Returns true if non-empty.
bool loadKeyFile(const char* path, char* dest, size_t destLen);
void loadKeysIntoNet();
void migrateLegacy();

// Wolf-eat: delete up to want random capture files (.pcap/.22000). Never keys.
uint8_t eatRandomLoot(uint8_t want);

// One capture per BSSID: drop HIDDEN_/MAC copies, keep named SSID + handshake.
uint8_t compactLoot();

} // namespace Storage
