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
    // fR3k v3.0.4: returns the count of unviewed BSSIDs with new
    // cracks (NEW badges). Used by the root menu for the "N new" badge.
    static uint8_t getNewCrackCount();
    // fR3k v3.0.4: kick a Wigle upload of all BSSIDs currently
    // flagged as recommend-upload. Safe to call from anywhere.
    static void startWigleUpload();
    // fR3k v3.0.4: Wigle busy?
    static bool isWigleBusy();
    // fR3k v3.0.4: count of BSSIDs that pass the recommend-upload
    // filter (not cracked, not uploaded, clean BSSID, file < 1 MB).
    static uint16_t getRecommendUploadCount();

private:
    enum class Tab : uint8_t { WPASEC = 0, PWNCRACK = 1 };
    // fR3k v3.0.4: sort modes for the visible page.
    //   DAY     = newest file mtime first
    //   AMOUNT  = number of cracked handshakes per BSSID, descending
    //   ALPHA   = SSID/filename ascending
    //   CAPTURED= earliest capture date first
    //   CRACKED = never-cracked first (operator sees what still needs work)
    enum class SortMode : uint8_t {
        DAY = 0, AMOUNT = 1, ALPHA = 2, CAPTURED = 3, CRACKED = 4,
        COUNT = 5
    };
    static const char* sortModeName(SortMode m);

    static const uint8_t PAGE_SIZE = 48; // same footprint that already worked fine

    static bool active;
    static bool keyWasPressed;
    static bool detailView;
    static bool syncModal;
    static bool diagModal;
    static Tab tab;
    // fR3k v3.0.4: per-tab sort mode + dirty flag so the next scan()
    // re-applies the sort. Defaults to DAY.
    static SortMode sortMode;
    static bool sortDirty;
    // fR3k v3.0.4: pending per-row NEW badge map. BSSID -> true if
    // cracked entry is newer than the operator's last seen timestamp
    // for that BSSID. Cleared on `.` hotkey.
    static uint8_t s_newBadgeBssid[8][13];
    static uint32_t s_newBadgeUntilMs[8];
    static bool s_newBadgeValid[8];
    static void markAllSeen();
    static bool isNewBssid(const char* bssid);
    static void maybeAutoPull();
    static uint8_t selected;
    static uint8_t scroll;
    static uint8_t count;       // matches on the CURRENT page only (0..PAGE_SIZE)
    static uint8_t page;        // 0-based page index into the current tab's file listing
    static bool hasMore;        // true if at least one more matching file exists past this page
    static uint16_t totalItems;

    static void scan();
    static void countTotal();
    static void gotoPage(uint8_t newPage, bool landOnLast);
    static void handleInput();
    static void startSync(bool oneFile = false);
    static void startPullResults();
    static void runDiag();
    static void deleteSelected();
    static void reloadList();
};
