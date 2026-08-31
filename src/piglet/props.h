#pragma once
// Seasonal daily props.
// Unlock: level 35  OR  PIG → CODE → P0rkP0tk  (also pork4 / o1nk4).
//
// Natural seasons (one max per game-day = 360s):
//   SUMMER  — hive + bees (chase when near)
//   WINTER  — snowman (jump to break)
//   AUTUMN  — sleeping fox + Zzz (no interact; leaves when you walk away)
//   SPRING  — campfire after storm lightning (burns ~½ game-day; no break)
// Extra (same unlock):
//   CITY    — cardboard box + street cat (leaves when you walk away)
//   DESERT  — sun-bleached skull (ambient; fades after a while)
//
// Spawns OFF-SCREEN ahead of walk; toast only when first in view.
#include <Arduino.h>
#include <M5Unified.h>
#include <stdint.h>

namespace Props {

static constexpr uint8_t SLOT_COUNT = 6;
static constexpr uint8_t SLOT_SUMMER = 0;
static constexpr uint8_t SLOT_WINTER = 1;
static constexpr uint8_t SLOT_SPRING = 2;
static constexpr uint8_t SLOT_AUTUMN = 3;
static constexpr uint8_t SLOT_CITY   = 4;
static constexpr uint8_t SLOT_DESERT = 5;

void begin();
bool anyUnlocked();
bool isUnlocked(uint8_t slot);
void unlockSlot(uint8_t slot);
void unlockAllFour();   // unlock all seasonal props (name kept for CODE)

void scroll(int8_t dx);
void update();
void draw(M5Canvas& canvas, int16_t yOffset);

// ANIM TEST: force prop on farm (ignores day gate / off-screen rule)
// 0 HIVE  1 SNOWMAN  2 FOX  3 FIRE  4 CAT  5 SKULL  6 CLEAR
void forceDemo(uint8_t which);

}  // namespace Props
