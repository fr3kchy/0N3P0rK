#include "xp.h"

static uint32_t s_xp = 0;

void XP::addXP(XPEvent event) {
    uint16_t n = 3;
    if (event == XPEvent::WOLF_SCARE) n = 5;
    if (event == XPEvent::WOLF_HIDE) n = 8;
    addXP(n);
}

void XP::addXP(uint16_t amount) {
    s_xp += amount;
}

uint8_t XP::getLevel() {
    uint8_t lvl = (uint8_t)(1 + s_xp / 100);
    if (lvl < 1) lvl = 1;
    if (lvl > 50) lvl = 50;
    return lvl;
}
