#pragma once
#include <Arduino.h>
#include <SD.h>

namespace SDLayout {
inline bool usingNewLayout() { return true; }
inline void setUseNewLayout(bool) {}

inline const char* handshakesDir() { return "/loot/wpa-sec"; }
inline const char* passworldDir() { return "/loot/Passworld"; }
inline const char* pigpassDir() { return "/loot/pigpass"; }
inline const char* pigpassResultsPath() { return "/loot/pigpass/cracked.txt"; }
inline const char* pigpassCheckpointPath() { return "/loot/pigpass/checkpoint.txt"; }
inline const char* pigpassLastWordlistPath() { return "/loot/pigpass/last_wl.txt"; }
inline const char* evilpigDir() { return "/loot/evilpig"; }
inline const char* evilpigCredsPath() { return "/loot/evilpig/creds.csv"; }

inline void ensureDirs() {
    SD.mkdir("/loot");
    SD.mkdir("/loot/wpa-sec");
    SD.mkdir("/loot/pwncrack");
    SD.mkdir("/loot/pigpass");
    SD.mkdir("/loot/Passworld");
    SD.mkdir("/loot/evilpig");
}
}
