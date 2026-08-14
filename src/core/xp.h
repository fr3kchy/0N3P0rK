// Tiny XP stub so trees / wolf / weather still compile.
// No ranks, no achievements - Tamagotchi pig does not use this UI.
#pragma once

#include <stdint.h>

enum class XPEvent : uint8_t {
    FRUIT_PICKED = 0,
    WOLF_SCARE,
    WOLF_HIDE,
    PIGPASS_CRACK,
    EVILPIG_CATCH
};

class XP {
public:
    static void addXP(XPEvent event);
    static void addXP(uint16_t amount);
    static uint8_t getLevel();
};
