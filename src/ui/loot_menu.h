#pragma once

#include <Arduino.h>
#include <M5Unified.h>

class LootMenu {
public:
    static void show();
    static void openWpaSec();
    static void openPwncrack();
    static void hide();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isActive() { return active; }
    static const char* getBottomHint();

private:
    enum class Tab : uint8_t { WPASEC = 0, PWNCRACK = 1 };

    static bool active;
    static bool keyWasPressed;
    static bool detailView;
    static bool syncModal;
    static bool diagModal;
    static Tab tab;
    static uint8_t selected;
    static uint8_t scroll;
    static uint8_t count;

    static void scan();
    static void handleInput();
    static void startSync();
    static void startPull();
    static void runDiag();
};
