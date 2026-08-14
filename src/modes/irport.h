// IR port — onboard LED power blast (NA/EU packs) + SD custom codes.
#pragma once

#include <Arduino.h>
#include <M5Unified.h>

class IrPortMode {
public:
    static void start();
    static void stop();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isRunning() { return running; }
    static bool isBlasting() { return running && phase == Phase::BLAST; }
    static void getStatusLine(char* buf, size_t n);

private:
    enum class Phase : uint8_t { REGION = 0, READY, BLAST, FILE_PICK, DONE };
    enum class Pack : uint8_t { BUILTIN = 0, CUSTOM = 1 };
    enum class Proto : uint8_t { NEC = 0, SAMSUNG, SONY };

    struct Code {
        Proto proto;
        uint16_t addr;
        uint16_t cmd;
        uint8_t bits;
        char name[18];
    };

    static constexpr uint8_t MAX_CODES = 48;
    static constexpr uint8_t MAX_FILES = 24;

    static bool running;
    static Phase phase;
    static Pack pack;
    static Code codes[MAX_CODES];
    static uint8_t codeCount;
    static uint8_t blastIndex;
    static uint8_t blastTotal;
    static uint32_t nextSendMs;
    static char packName[28];
    static char statusMsg[40];
    static char fileNames[MAX_FILES][28];
    static uint8_t fileCount;
    static uint8_t fileSel;
    static uint8_t fileScroll;
    static uint8_t regionSel;
    static bool keyLatch;

    static void loadBuiltin();
    static bool loadFile(const char* path);
    static void scanFiles();
    static void startBlast();
    static void handleInput();
};
