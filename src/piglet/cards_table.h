#pragma once
// Farm table — unlock lv 45. Jump next to it to start cards (stub monologue for now).
#include <Arduino.h>
#include <M5Unified.h>
#include <stdint.h>

namespace CardsTable {

void begin();
bool unlocked();
void update();
void draw(M5Canvas& canvas, int16_t yOffset);
void scroll(int8_t dx);

}  // namespace CardsTable
