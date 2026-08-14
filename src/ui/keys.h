#pragma once
#include <M5Cardputer.h>

// Cardputer Esc is the `~` key. Also accept 27 and backspace.
inline bool keyEsc() {
    if (M5Cardputer.Keyboard.isKeyPressed('`')) return true;
    if (M5Cardputer.Keyboard.isKeyPressed('~')) return true;
    if (M5Cardputer.Keyboard.isKeyPressed(27)) return true;
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) return true;
    auto keys = M5Cardputer.Keyboard.keysState();
    if (keys.del) return true;
    for (char c : keys.word) {
        if (c == '`' || c == '~' || c == 27) return true;
    }
    return false;
}

// New key event: either latch was clear, or Cardputer posted isChange.
inline bool keyNewPress(bool& latch) {
    if (!M5Cardputer.Keyboard.isPressed()) {
        latch = false;
        return false;
    }
    bool change = M5Cardputer.Keyboard.isChange();
    if (latch && !change) return false;
    latch = true;
    return true;
}
