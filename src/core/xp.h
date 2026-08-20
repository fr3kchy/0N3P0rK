#pragma once
#include <stdint.h>

enum class XPEvent : uint8_t {
    FRUIT_PICKED = 0,
    GOLD_APPLE,
    WOLF_SCARE,
    WOLF_HIDE,
    PIGPASS_CRACK,
    EVILPIG_CATCH,
    HANDSHAKE,
    PET,
    FEED,
    NIGHT_SURVIVE
};

class XP {
public:
    static void begin();
    static void addXP(XPEvent event);
    static void addXP(uint16_t amount);
    static uint8_t getLevel();
    static uint32_t getXP();
    static uint32_t intoLevel();    // XP already earned toward next level
    static uint32_t needForNext();  // XP required for this level-up
    static bool blushUnlocked();
    static bool goldAppleUnlocked();
    static bool retroUnlocked();
    static bool noirUnlocked();
    static bool allUnlocked();
    static void unlockAll();
    static bool isSkinLocked(uint8_t skin);
    static bool isSeasonLocked(uint8_t seasonMode);
};
