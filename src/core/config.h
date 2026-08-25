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
    RETRO   = 4,
    SHADOW  = 5,
    CANDY   = 6,
    GOLD    = 7
};
static const uint8_t PIG_SKIN_COUNT = 8;

enum class SeasonMode : uint8_t {
    AUTO   = 0,
    SPRING = 1,
    SUMMER = 2,
    AUTUMN = 3,
    WINTER = 4,
    RETRO  = 5,
    NOIR   = 6
};
static const uint8_t SEASON_MODE_COUNT = 7;

enum class Season : uint8_t {
    SPRING = 0,
    SUMMER = 1,
    AUTUMN = 2,
    WINTER = 3,
    RETRO  = 4,
    NOIR   = 5
};
static const uint8_t SEASON_COUNT = 4;

struct PersonalityConfig {
    char name[32] = "Pig";
    uint8_t soundLevel = 1;
    uint8_t brightness = 80;
    uint8_t dimLevel = 20;
    uint16_t dimTimeout = 30;
    uint8_t skyMode = 0;
    uint8_t pigSkin = 0;
    uint8_t pigSkinAlive = 0;  // last non-zombie skin (restore on 5 hearts)
    uint8_t nightWolfBites = 0;
    bool zombieSkinUnlocked = false;
    uint8_t seasonMode = 0;
    bool animTest = false;
    bool wolfEnabled = true;
    uint8_t scrollSpeed = 9;
    bool fruitTreesAmbient = true;
    bool freeLife = true;  // pig walks/jumps/hides even during functions
    bool wolfEatLoot = true;  // bite at 0 hearts stashes loot; hit/Am-off returns it
};

enum class HopSet : uint8_t { ALL = 0, PRIORITY = 1, CORE = 2 };
static const uint8_t HOP_SET_COUNT = 3;

// OURS = current greedy EAPOL + broadcast kick
// PAN  = extra stack (bidir kick, EAPOL-Start, PMKID probe, optional CSA/flood)
// AUTO = OURS first, then PAN if no pair lands
//
// The enum values are the on-disk format for the radio.hsMethod byte:
//   0      -> AUTO (special, not a real method)
//   1..N   -> Methods::name(idx - 1) from cap/methods/method_registry.cpp
// Adding a new method no longer requires touching this enum — just add a
// row to METHOD_LIST() in method_ctx.h and the new entry shows up in the
// radio settings UI at the next index. Values written by older firmware
// (OURS=1, PAN=2) keep resolving to the same name because the first two
// registry rows are still OURS and PAN in that order.
enum class HsMethod : uint8_t { AUTO = 0, OURS = 1, PAN = 2 };
// Runtime count for the UI is 1 + Cap::Methods::count(); see
// HS_METHOD_COUNT below. Use HS_METHOD_COUNT for legacy code that needs a
// constexpr upper bound (the registry isn't visible from config.h).
static const uint8_t HS_METHOD_COUNT_MAX = 8;

// pack on-disk layout (RadioConfig::pack byte) mirrors hsMethod above, but
// walks the independent Cap::Packs table (cap/packs/), not the Methods one:
//   0      -> STOCK  (factory-default knobs, hsMethod left on AUTO)
//   1..N   -> Cap::Packs::table()[idx-1] - a named knob bundle that can
//              point at any capture method by name (or none, for AUTO).
//              Dropping a pack_yourname.cpp file into src/cap/packs/ (see
//              its README.md) adds a new numbered slot here automatically,
//              same plug-and-play pattern as capture methods.
//   0xFF   -> CUSTOM (fixed sentinel, deliberately NOT N+1 - if it were
//              N+1 it would silently mean a different thing after a
//              firmware update that adds/removes a pack; a fixed byte
//              keeps a saved CUSTOM pack CUSTOM forever)
enum class RadioPack : uint8_t { STOCK = 0, CUSTOM = 0xFF };
static const uint8_t RADIO_PACK_CUSTOM = 0xFF;
// Sanity-clamp bound for NVS load, same role as HS_METHOD_COUNT_MAX above
// (the Packs registry isn't visible from config.h either).
static const uint8_t RADIO_PACK_COUNT_MAX = 8;

// Knobs for LIGHT / AGGRO / EVILPIG — same code, different tune.
struct RadioConfig {
    uint16_t hopMs = 300;      // 50..2000 channel dwell
    uint16_t lockMs = 8000;    // stay on channel after EAPOL (0 = never)
    bool lockOnHs = true;
    bool deauth = true;        // AGGRO / EVILPIG kicks
    bool randomMac = false;
    int8_t minRssi = -85;      // skip weaker APs for kick
    uint8_t hopSet = 0;        // HopSet: ALL / PRI / 1-6-11
    uint8_t hsMethod = 0;      // HsMethod AUTO / OURS / PAN
    uint8_t fallbackSec = 25;  // AUTO: seconds before trying the other method
    uint8_t kickBurst = 2;     // deauth/disassoc rounds per AP
    bool bidirKick = true;     // also spoof client -> AP
    bool eapolTx = true;       // EAPOL-Start / Logoff (works on PMF)
    bool pmkidProbe = true;    // Open-System auth + assoc for PMKID
    bool csaHerd = false;      // spoofed CSA beacon
    bool authFlood = false;    // random-MAC auth flood if no clients
    uint8_t deauthReason = 7;
    uint16_t pauseMs = 1200;   // listen after M1, don't kick
    bool fatPcap = true;       // radiotap with ch / rate / rssi
    uint8_t pack = 0;          // RadioPack last applied
};

struct BleConfig {
    uint16_t burstMs = 200;    // 50..500 between bursts
    uint16_t advMs = 100;      // 50..200 per advertisement
};

static const uint8_t HOTKEY_COUNT = 10;
// Slots: AGGRO LIGHT PIGPASS EVILPIG BLE IR SPECTRUM LOOT RADIO FILES
struct HotkeyConfig {
    char key[HOTKEY_COUNT] = { 'a', 'l', 'p', 'e', 'b', 'i', 's', 'h', 'r', 'f' };
};
static const uint8_t HOTKEY_RADIO = 8;

class Config {
public:
    static bool init();
    static bool save();
    static void applyRadioPack(uint8_t pack);
    static void resetRadio();
    // Mark the current radio config as hand-tuned: PACK in the UI flips to
    // CUSTOM, future applyRadioPack() calls from presets stop auto-overwriting
    // the user's knobs. Called by the settings UI whenever any radio knob
    // (other than PACK / HS METHOD) is edited.
    static void markRadioCustom();

    static PersonalityConfig& personality() { return personalityConfig; }
    static RadioConfig& radio() { return radioConfig; }
    static BleConfig& ble() { return bleConfig; }
    static HotkeyConfig& hotkeys() { return hotkeyConfig; }
    static void setPersonality(const PersonalityConfig& cfg);

    static bool isZombieSkinUnlocked();
    static bool registerNightWolfBite();
    static void becomeZombie();
    static void cureZombie();
    static bool isSDAvailable();

private:
    static PersonalityConfig personalityConfig;
    static RadioConfig radioConfig;
    static BleConfig bleConfig;
    static HotkeyConfig hotkeyConfig;
    static bool initialized;
};
