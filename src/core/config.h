// Slim personality config for the Tamagotchi pig.
// WiFi / API keys live in Net (Stamp NVS), not here.
#pragma once

#include <Arduino.h>
#include <stdint.h>

enum class SkyMode : uint8_t {
    AUTO = 0,
    DAY = 1,
    NIGHT = 2
};
static const uint8_t SKY_MODE_COUNT = 3;

enum class PigSkin : uint8_t {
    CLASSIC = 0,
    BLUSH   = 1,
    HOG     = 2,
    ZOMBIE  = 3,
    RETRO   = 4
};
static const uint8_t PIG_SKIN_COUNT = 5;

enum class SeasonMode : uint8_t {
    AUTO   = 0,
    SPRING = 1,
    SUMMER = 2,
    AUTUMN = 3,
    WINTER = 4,
    RETRO  = 5
};
static const uint8_t SEASON_MODE_COUNT = 6;

enum class Season : uint8_t {
    SPRING = 0,
    SUMMER = 1,
    AUTUMN = 2,
    WINTER = 3,
    RETRO  = 4
};
static const uint8_t SEASON_COUNT = 4;

struct PersonalityConfig {
    char name[32] = "Lexi";
    uint8_t soundLevel = 1;
    uint8_t brightness = 80;
    uint8_t dimLevel = 20;
    uint16_t dimTimeout = 30;
    uint8_t skyMode = 0;
    uint8_t pigSkin = 0;
    uint8_t nightWolfBites = 0;
    bool zombieSkinUnlocked = false;
    uint8_t seasonMode = 0;
    bool animTest = false;
    bool wolfEnabled = true;
    uint8_t scrollSpeed = 9;
    bool fruitTreesAmbient = true;
    bool freeLife = true;  // pig walks/jumps/hides even during functions
};

enum class HopSet : uint8_t { ALL = 0, PRIORITY = 1, CORE = 2 };
static const uint8_t HOP_SET_COUNT = 3;

// Knobs for LIGHT / AGGRO / EVILPIG — same code, different tune.
struct RadioConfig {
    uint16_t hopMs = 300;      // 50..2000 channel dwell
    uint16_t lockMs = 8000;    // stay on channel after EAPOL (0 = never)
    bool lockOnHs = true;
    bool deauth = true;        // AGGRO / EVILPIG kicks
    bool randomMac = false;
    int8_t minRssi = -85;      // skip weaker APs for kick
    uint8_t hopSet = 0;        // HopSet: ALL / PRI / 1-6-11
};

struct BleConfig {
    uint16_t burstMs = 200;    // 50..500 between bursts
    uint16_t advMs = 100;      // 50..200 per advertisement
};

class Config {
public:
    static bool init();
    static bool save();

    static PersonalityConfig& personality() { return personalityConfig; }
    static RadioConfig& radio() { return radioConfig; }
    static BleConfig& ble() { return bleConfig; }
    static void setPersonality(const PersonalityConfig& cfg);

    static bool isZombieSkinUnlocked();
    static bool registerNightWolfBite();
    static bool isSDAvailable();

private:
    static PersonalityConfig personalityConfig;
    static RadioConfig radioConfig;
    static BleConfig bleConfig;
    static bool initialized;
};
