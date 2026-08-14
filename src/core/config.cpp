#include "config.h"
#include "../storage/littlefs_ops.h"
#include <Preferences.h>
#include <string.h>

PersonalityConfig Config::personalityConfig;
RadioConfig Config::radioConfig;
BleConfig Config::bleConfig;
bool Config::initialized = false;

static Preferences s_prefs;

bool Config::init() {
    if (initialized) return true;
    if (!s_prefs.begin("onelpig", false)) {
        Serial.println("[CFG] NVS open failed, using defaults");
        initialized = true;
        return false;
    }

    PersonalityConfig& p = personalityConfig;
    s_prefs.getString("name", p.name, sizeof(p.name));
    if (p.name[0] == '\0' || strcmp(p.name, "Lexi") == 0) {
        strncpy(p.name, "Pig", sizeof(p.name) - 1);
        p.name[sizeof(p.name) - 1] = '\0';
    }
    p.soundLevel = s_prefs.getUChar("snd", p.soundLevel);
    p.brightness = s_prefs.getUChar("bri", p.brightness);
    p.dimLevel = s_prefs.getUChar("dim", p.dimLevel);
    p.dimTimeout = s_prefs.getUShort("dimt", p.dimTimeout);
    p.skyMode = s_prefs.getUChar("sky", p.skyMode);
    p.pigSkin = s_prefs.getUChar("skin", p.pigSkin);
    p.nightWolfBites = s_prefs.getUChar("nwolf", p.nightWolfBites);
    p.zombieSkinUnlocked = s_prefs.getBool("zombie", p.zombieSkinUnlocked);
    p.seasonMode = s_prefs.getUChar("season", p.seasonMode);
    p.animTest = s_prefs.getBool("anim", p.animTest);
    p.wolfEnabled = s_prefs.getBool("wolf", p.wolfEnabled);
    p.scrollSpeed = s_prefs.getUChar("scroll", p.scrollSpeed);
    p.fruitTreesAmbient = s_prefs.getBool("fruit", p.fruitTreesAmbient);
    p.freeLife = s_prefs.getBool("life", p.freeLife);
    p.wolfEatLoot = s_prefs.getBool("weat", p.wolfEatLoot);

    RadioConfig& r = radioConfig;
    r.hopMs = s_prefs.getUShort("hop", r.hopMs);
    r.lockMs = s_prefs.getUShort("lock", r.lockMs);
    r.lockOnHs = s_prefs.getBool("lockhs", r.lockOnHs);
    r.deauth = s_prefs.getBool("deauth", r.deauth);
    r.randomMac = s_prefs.getBool("rndmac", r.randomMac);
    r.minRssi = (int8_t)s_prefs.getChar("rssi", r.minRssi);
    r.hopSet = s_prefs.getUChar("hopset", r.hopSet);

    BleConfig& b = bleConfig;
    b.burstMs = s_prefs.getUShort("bleb", b.burstMs);
    b.advMs = s_prefs.getUShort("blea", b.advMs);

    if (p.soundLevel > 5) p.soundLevel = 5;
    if (p.brightness > 100) p.brightness = 100;
    if (p.scrollSpeed < 1) p.scrollSpeed = 1;
    if (p.scrollSpeed > 10) p.scrollSpeed = 10;
    if (p.pigSkin >= PIG_SKIN_COUNT) p.pigSkin = 0;
    if (p.seasonMode >= SEASON_MODE_COUNT) p.seasonMode = 0;
    if (p.skyMode >= SKY_MODE_COUNT) p.skyMode = 0;
    if (r.hopMs < 50) r.hopMs = 50;
    if (r.hopMs > 2000) r.hopMs = 2000;
    if (r.lockMs > 15000) r.lockMs = 15000;
    if (r.minRssi < -90) r.minRssi = -90;
    if (r.minRssi > -50) r.minRssi = -50;
    if (r.hopSet >= HOP_SET_COUNT) r.hopSet = 0;
    if (b.burstMs < 50) b.burstMs = 50;
    if (b.burstMs > 500) b.burstMs = 500;
    if (b.advMs < 50) b.advMs = 50;
    if (b.advMs > 200) b.advMs = 200;

    initialized = true;
    Serial.printf("[CFG] pig=%s skin=%u season=%u\n", p.name, p.pigSkin, p.seasonMode);
    return true;
}

bool Config::save() {
    if (!initialized) init();
    const PersonalityConfig& p = personalityConfig;
    s_prefs.putString("name", p.name);
    s_prefs.putUChar("snd", p.soundLevel);
    s_prefs.putUChar("bri", p.brightness);
    s_prefs.putUChar("dim", p.dimLevel);
    s_prefs.putUShort("dimt", p.dimTimeout);
    s_prefs.putUChar("sky", p.skyMode);
    s_prefs.putUChar("skin", p.pigSkin);
    s_prefs.putUChar("nwolf", p.nightWolfBites);
    s_prefs.putBool("zombie", p.zombieSkinUnlocked);
    s_prefs.putUChar("season", p.seasonMode);
    s_prefs.putBool("anim", p.animTest);
    s_prefs.putBool("wolf", p.wolfEnabled);
    s_prefs.putUChar("scroll", p.scrollSpeed);
    s_prefs.putBool("fruit", p.fruitTreesAmbient);
    s_prefs.putBool("life", p.freeLife);
    s_prefs.putBool("weat", p.wolfEatLoot);

    const RadioConfig& r = radioConfig;
    s_prefs.putUShort("hop", r.hopMs);
    s_prefs.putUShort("lock", r.lockMs);
    s_prefs.putBool("lockhs", r.lockOnHs);
    s_prefs.putBool("deauth", r.deauth);
    s_prefs.putBool("rndmac", r.randomMac);
    s_prefs.putChar("rssi", r.minRssi);
    s_prefs.putUChar("hopset", r.hopSet);

    const BleConfig& b = bleConfig;
    s_prefs.putUShort("bleb", b.burstMs);
    s_prefs.putUShort("blea", b.advMs);
    return true;
}

void Config::setPersonality(const PersonalityConfig& cfg) {
    personalityConfig = cfg;
    save();
}

bool Config::isSDAvailable() {
    return Storage::available();
}

bool Config::isZombieSkinUnlocked() {
    return personalityConfig.zombieSkinUnlocked;
}

bool Config::registerNightWolfBite() {
    if (personalityConfig.zombieSkinUnlocked) return false;
    if (personalityConfig.nightWolfBites < 255)
        personalityConfig.nightWolfBites++;
    if (personalityConfig.nightWolfBites >= 3) {
        personalityConfig.zombieSkinUnlocked = true;
        save();
        return true;
    }
    save();
    return false;
}
