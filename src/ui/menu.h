// Sirloin-style grouped modal — same feel as OnePork.
#pragma once

#include <M5Unified.h>
#include "../core/app.h"

namespace Menu {

void begin();
void show();
void hide();
bool isActive();
bool isInModal();
bool closeModal();
void update();
void draw(M5Canvas& canvas);
void onEnter(AppMode mode);
void handleKey(char c, bool enter, bool del, bool fn);
const char* hint();
const char* selectedHint();

}  // namespace Menu
