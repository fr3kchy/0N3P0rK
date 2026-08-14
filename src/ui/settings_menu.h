// OnePork-style settings: SCENE / RADIO / BLE
// ;/. move   ENT = toggle or enter value   then ;/. change   ` back
#pragma once

#include <M5Unified.h>

enum class SettingsPage : uint8_t { SCENE = 0, RADIO = 1, BLE = 2 };

namespace SettingsMenu {

void show(SettingsPage page);
void hide();
bool isActive();
void update();
void draw(M5Canvas& canvas);
const char* bottomHint();
SettingsPage page();

}  // namespace SettingsMenu
