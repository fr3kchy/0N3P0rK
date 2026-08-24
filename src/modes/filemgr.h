// File manager — browse SD (Storage::) and internal LittleFS, view/edit .txt.
// Same on/off + draw contract as IrPortMode/UsbSdMode so App/Menu wire it
// the same way.
#pragma once
#include <Arduino.h>
#include <M5Unified.h>

class FileMgrMode {
public:
    static void start();
    static void stop();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isRunning() { return running; }
    static void getStatusLine(char* buf, size_t n);

private:
    enum class Phase : uint8_t { BROWSE, VIEW, EDIT, CONFIRM_DEL };
    enum class Volume : uint8_t { SD = 0, LFS = 1 };

    struct Entry {
        char name[40];
        bool isDir;
        uint32_t size;
    };

    static constexpr uint8_t MAX_ENTRIES = 48;
    static constexpr uint16_t EDIT_CAP = 6144;   // internal RAM, no PSRAM on this board
    static constexpr uint8_t PATH_BUF = 96;
    static constexpr uint8_t VIS_ROWS = 6;

    static bool running;
    static Phase phase;
    static Volume vol;

    static char curPath[PATH_BUF];
    static Entry entries[MAX_ENTRIES];
    static uint8_t entryCount;
    static uint8_t sel;
    static uint8_t scroll;
    static bool keyLatch;
    static char statusMsg[40];

    // view/edit buffer — one file at a time, either volume
    static char buf[EDIT_CAP];
    static uint16_t bufLen;
    static uint16_t cursor;
    static bool dirty;
    static char openName[40];
    static uint16_t viewTopLine;

    static void refreshList();
    static void enterDir(const char* name);
    static void goUp();
    static bool openSelected();
    static bool loadFile(const char* path);
    static bool saveFile();
    static void buildFullPath(char* out, size_t n, const char* name);
    static uint16_t cursorLine();
    static void handleBrowseInput();
    static void handleViewInput();
    static void handleEditInput();
    static void drawBrowse(M5Canvas& canvas);
    static void drawViewEdit(M5Canvas& canvas);
};
