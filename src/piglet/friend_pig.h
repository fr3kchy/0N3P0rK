#pragma once
// Second pig on the farm (unlock lv 40). Own AI; wolf can bite her too.
// Toggle: SCENE → FRIEND
#include <Arduino.h>
#include <M5Unified.h>
#include <stdint.h>

namespace FriendPig {

void begin();
bool unlocked();          // XP level >= 40 (or unlockAll)
bool enabled();           // unlocked && config toggle
void setEnabled(bool on); // not stored here — use Config

void update();
void draw(M5Canvas& canvas, int16_t yOffset);
void scroll(int8_t dx);   // world treadmill (optional)

int getFeetX();           // for wolf targeting
bool isActive();
void onWolfBitten();      // flinch + short flee

}  // namespace FriendPig
