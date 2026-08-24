#include "config.h"
#include "../storage/littlefs_ops.h"
#include "../cap/methods/method_ctx.h"
#include <Preferences.h>
#include <string.h>

PersonalityConfig Config::personalityConfig;
RadioConfig Config::radioConfig;
BleConfig Config::bleConfig;
HotkeyConfig Config::hotkeyConfig;
bool Config::initialized = false;

static char normHot(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static bool hotReserved(char c) {
    c = normHot(c);
    if (c == 0) return false;
    if (c < 32 || c >= 127) return true;
    return c == '`' || c == '~' || c == ' ' ||
           c == ';' || c == '.' || c == ',' || c == '/';
}

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
    p.pigSkinAlive = s_prefs.getUChar("skinal", p.pigSkinAlive);
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
    r.hsMethod = s_prefs.getUChar("hsmeth", r.hsMethod);
    r.fallbackSec = s_prefs.getUChar("hsfall", r.fallbackSec);
    r.kickBurst = s_prefs.getUChar("kickn", r.kickBurst);
    r.bidirKick = s_prefs.getBool("bidir", r.bidirKick);
    r.eapolTx = s_prefs.getBool("eapoltx", r.eapolTx);
    r.pmkidProbe = s_prefs.getBool("pmkid", r.pmkidProbe);
    r.csaHerd = s_prefs.getBool("csa", r.csaHerd);
    r.authFlood = s_prefs.getBool("aflood", r.authFlood);
    r.deauthReason = s_prefs.getUChar("dreason", r.deauthReason);
    r.pauseMs = s_prefs.getUShort("pausems", r.pauseMs);
    r.fatPcap = s_prefs.getBool("fatpcap", r.fatPcap);
    r.pack = s_prefs.getUChar("rpack", r.pack);

    BleConfig& b = bleConfig;
    b.burstMs = s_prefs.getUShort("bleb", b.burstMs);
    b.advMs = s_prefs.getUShort("blea", b.advMs);

    HotkeyConfig def{};
    hotkeyConfig = def;
    char raw[HOTKEY_COUNT];
    size_t got = s_prefs.getBytes("hotk", raw, HOTKEY_COUNT);
    if (got > 0) {
        if (got > HOTKEY_COUNT) got = HOTKEY_COUNT;
        for (size_t i = 0; i < got; i++) {
            char c = normHot(raw[i]);
            hotkeyConfig.key[i] = hotReserved(c) ? 0 : c;
        }
    }

    if (p.soundLevel > 5) p.soundLevel = 5;
    if (p.brightness > 100) p.brightness = 100;
    if (p.scrollSpeed < 1) p.scrollSpeed = 1;
    if (p.scrollSpeed > 10) p.scrollSpeed = 10;
    if (p.pigSkin >= PIG_SKIN_COUNT) p.pigSkin = 0;
    if (p.pigSkinAlive >= PIG_SKIN_COUNT ||
        p.pigSkinAlive == (uint8_t)PigSkin::ZOMBIE)
        p.pigSkinAlive = 0;
    if (p.seasonMode >= SEASON_MODE_COUNT) p.seasonMode = 0;
    if (p.skyMode >= SKY_MODE_COUNT) p.skyMode = 0;
    if (r.hopMs < 50) r.hopMs = 50;
    if (r.hopMs > 2000) r.hopMs = 2000;
    if (r.lockMs > 15000) r.lockMs = 15000;
    if (r.minRssi < -90) r.minRssi = -90;
    if (r.minRssi > -50) r.minRssi = -50;
    if (r.hopSet >= HOP_SET_COUNT) r.hopSet = 0;
    if (r.hsMethod >= HS_METHOD_COUNT_MAX) r.hsMethod = 0;
    if (r.fallbackSec < 10) r.fallbackSec = 10;
    if (r.fallbackSec > 90) r.fallbackSec = 90;
    if (r.kickBurst < 1) r.kickBurst = 1;
    if (r.kickBurst > 6) r.kickBurst = 6;
    if (r.deauthReason < 1) r.deauthReason = 7;
    if (r.deauthReason > 8) r.deauthReason = 7;
    if (r.pauseMs < 400) r.pauseMs = 400;
    if (r.pauseMs > 3000) r.pauseMs = 3000;
    if (r.pack >= RADIO_PACK_COUNT) r.pack = 0;
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
    s_prefs.putUChar("skinal", p.pigSkinAlive);
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
    s_prefs.putUChar("hsmeth", r.hsMethod);
    s_prefs.putUChar("hsfall", r.fallbackSec);
    s_prefs.putUChar("kickn", r.kickBurst);
    s_prefs.putBool("bidir", r.bidirKick);
    s_prefs.putBool("eapoltx", r.eapolTx);
    s_prefs.putBool("pmkid", r.pmkidProbe);
    s_prefs.putBool("csa", r.csaHerd);
    s_prefs.putBool("aflood", r.authFlood);
    s_prefs.putUChar("dreason", r.deauthReason);
    s_prefs.putUShort("pausems", r.pauseMs);
    s_prefs.putBool("fatpcap", r.fatPcap);
    s_prefs.putUChar("rpack", r.pack);

    const BleConfig& b = bleConfig;
    s_prefs.putUShort("bleb", b.burstMs);
    s_prefs.putUShort("blea", b.advMs);

    char raw[HOTKEY_COUNT];
    for (uint8_t i = 0; i < HOTKEY_COUNT; i++)
        raw[i] = normHot(hotkeyConfig.key[i]);
    s_prefs.putBytes("hotk", raw, HOTKEY_COUNT);
    return true;
}

void Config::setPersonality(const PersonalityConfig& cfg) {
    personalityConfig = cfg;
    save();
}

// Resolve a method name to its enum index (1-based, slot 0 is AUTO).
// Returns 1 (OURS) as a safe fallback if the registry doesn't have it.
static uint8_t hsMethodIndex(const char* name) {
    uint8_t n = 0;
    const Cap::Methods::Entry* tbl = Cap::Methods::table(&n);
    for (uint8_t i = 0; i < n; i++) {
        if (strcmp(tbl[i].name, name) == 0) return (uint8_t)(i + 1);
    }
    return 1;
}

void Config::applyRadioPack(uint8_t pack) {
    if (pack >= RADIO_PACK_COUNT) pack = 0;
    // CUSTOM is a UI-side flag, not a preset. Setting PACK=CUSTOM from the
    // menu is a no-op — CUSTOM only flips when the user hand-tunes a knob
    // (see markRadioCustom()).
    if (pack == (uint8_t)RadioPack::CUSTOM) return;
    RadioConfig r;
    if (pack == (uint8_t)RadioPack::OURS) {
        r.hsMethod = hsMethodIndex("OURS");
        r.bidirKick = false;
        r.eapolTx = false;
        r.pmkidProbe = false;
        r.csaHerd = false;
        r.authFlood = false;
        r.kickBurst = 2;
        r.pauseMs = 1200;
    } else if (pack == (uint8_t)RadioPack::PAN) {
        r.hsMethod = hsMethodIndex("PAN");
        r.bidirKick = true;
        r.eapolTx = true;
        r.pmkidProbe = true;
        r.csaHerd = false;
        r.authFlood = false;
        r.kickBurst = 3;
        r.pauseMs = 1500;
        r.lockMs = 10000;
        r.hopMs = 250;
    }
    r.pack = pack;
    radioConfig = r;
    save();
}

void Config::markRadioCustom() {
    // Called when the user edits any radio knob except PACK / HS METHOD.
    // Flips the PACK indicator to CUSTOM so the UI shows that the current
    // parameters no longer match any preset, without overwriting anything
    // the user just set.
    if ((RadioPack)radioConfig.pack == RadioPack::CUSTOM) return;
    radioConfig.pack = (uint8_t)RadioPack::CUSTOM;
    save();
}

void Config::resetRadio() {
    applyRadioPack((uint8_t)RadioPack::STOCK);
}

bool Config::isSDAvailable() {
    return Storage::available();
}

bool Config::isZombieSkinUnlocked() {
    return personalityConfig.zombieSkinUnlocked;
}

bool Config::registerNightWolfBite() {
    if (personalityConfig.pigSkin == (uint8_t)PigSkin::ZOMBIE) return false;
    if (personalityConfig.nightWolfBites < 255)
        personalityConfig.nightWolfBites++;
    if (personalityConfig.nightWolfBites >= 5) {
        personalityConfig.nightWolfBites = 5;
        personalityConfig.zombieSkinUnlocked = true;
        save();
        return true;
    }
    save();
    return false;
}

void Config::becomeZombie() {
    if (personalityConfig.pigSkin != (uint8_t)PigSkin::ZOMBIE) {
        personalityConfig.pigSkinAlive = personalityConfig.pigSkin;
        if (personalityConfig.pigSkinAlive == (uint8_t)PigSkin::ZOMBIE ||
            personalityConfig.pigSkinAlive >= PIG_SKIN_COUNT)
            personalityConfig.pigSkinAlive = (uint8_t)PigSkin::CLASSIC;
    }
    personalityConfig.pigSkin = (uint8_t)PigSkin::ZOMBIE;
    personalityConfig.zombieSkinUnlocked = true;
    save();
}

void Config::cureZombie() {
    if (personalityConfig.pigSkin != (uint8_t)PigSkin::ZOMBIE) return;
    uint8_t back = personalityConfig.pigSkinAlive;
    if (back >= PIG_SKIN_COUNT || back == (uint8_t)PigSkin::ZOMBIE)
        back = (uint8_t)PigSkin::CLASSIC;
    personalityConfig.pigSkin = back;
    personalityConfig.nightWolfBites = 0;
    save();
}
