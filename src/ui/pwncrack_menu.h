#pragma once

#include <Arduino.h>
#include <M5Unified.h>

class PwncrackMenu {
public:
    static void show();
    static void hide();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isActive() { return active; }
    static const char* getBottomHint();

private:
    static bool active;
    static bool keyWasPressed;
    static bool detailView;
    static bool syncModal;
    static bool diagModal;
    static uint8_t selected;
    static uint8_t scroll;
    static uint8_t count;
    static uint8_t hintIndex;
    static char syncText[48];
    static char diagText[8][28];
    static uint8_t diagLines;

    static void scan();
    static void handleInput();
    static void startSync();
    static void runDiag();
};
