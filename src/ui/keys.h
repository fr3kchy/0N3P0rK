#pragma once
#include <M5Cardputer.h>

// Cardputer "Esc" is usually ` — also accept 27 and backspace.
inline bool keyEsc() {
    if (M5Cardputer.Keyboard.isKeyPressed('`')) return true;
    if (M5Cardputer.Keyboard.isKeyPressed(27)) return true;
    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) return true;
    auto keys = M5Cardputer.Keyboard.keysState();
    if (keys.del) return true;
    for (char c : keys.word) {
        if (c == '`' || c == 27) return true;
    }
    return false;
}
