#include "hashes_menu.h"
#include "display.h"
#include "../storage/littlefs_ops.h"
#include "../sync/wpasec.h"
#include "../net/ap_sta.h"
#include "../piglet/avatar.h"
#include "../audio/sfx.h"
#include "../cap/sniffer.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <SD.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <esp_heap_caps.h>

bool HashesMenu::active = false;
bool HashesMenu::keyWasPressed = false;
bool HashesMenu::detailView = false;
bool HashesMenu::syncModal = false;
bool HashesMenu::diagModal = false;
uint8_t HashesMenu::selected = 0;
uint8_t HashesMenu::scroll = 0;
uint8_t HashesMenu::count = 0;
CaptureInfo HashesMenu::items[48];
char HashesMenu::syncText[48] = "";
char HashesMenu::diagText[8][28];
uint8_t HashesMenu::diagLines = 0;
uint8_t HashesMenu::hintIndex = 0;

static const char* const HINTS[] = {
    "ENT:DET  S:SYNC  T:TEST",
    "FEED YO HASHCAT.",
    "OK=PASS  ..=SENT  --=LOC",
    "KEY IN /0N3P0rK/wpa-sec/"
};
static const uint8_t HINT_COUNT = 4;
static const uint8_t VISIBLE = 5;

static void formatSize(char* out, size_t len, uint32_t bytes) {
    if (bytes < 1024) snprintf(out, len, "%uB", (unsigned)bytes);
    else snprintf(out, len, "%uKB", (unsigned)(bytes / 1024));
}

static bool endsWith(const char* name, const char* suf) {
    size_t n = strlen(name), s = strlen(suf);
    if (n < s) return false;
    for (size_t i = 0; i < s; i++) {
        char a = name[n - s + i], b = suf[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static void parseBssidFromName(const char* name, char* hex13, char* pretty) {
    hex13[0] = '\0';
    pretty[0] = '\0';
    char hex[13];
    size_t n = 0;
    for (const char* p = name; *p && *p != '.' && n < 12; p++) {
        if (*p == '-' || *p == ':') continue;
        if (!isxdigit((unsigned char)*p)) break;
        hex[n++] = (char)toupper((unsigned char)*p);
    }
    if (n != 12) return;
    hex[12] = '\0';
    memcpy(hex13, hex, 13);
    snprintf(pretty, 18, "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
             hex[0], hex[1], hex[2], hex[3], hex[4], hex[5],
             hex[6], hex[7], hex[8], hex[9], hex[10], hex[11]);
}

static bool connectHome() {
    return Net::joinHome(22000);
}

static void dropWifi() {
    Net::leaveHome();
}

void HashesMenu::scan() {
    count = 0;
    selected = 0;
    scroll = 0;
    if (!Storage::available()) return;
    WPASec::loadCache();
    File root = SD.open(Storage::DIR_HS);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }
    File f = root.openNextFile();
    while (f && count < 48) {
        if (!f.isDirectory()) {
            const char* name = Storage::baseName(f.name());
            bool pcap = endsWith(name, ".pcap") || endsWith(name, ".pcapng");
            if (pcap) {
                CaptureInfo& c = items[count];
                memset(&c, 0, sizeof(c));
                strncpy(c.filename, name, sizeof(c.filename) - 1);
                c.fileSize = (uint32_t)f.size();
                c.isPMKID = false;
                parseBssidFromName(name, c.bssidHex, c.bssid);
                const char* ss = WPASec::getSSID(c.bssidHex[0] ? c.bssidHex : c.bssid);
                if (ss && ss[0]) strncpy(c.ssid, ss, sizeof(c.ssid) - 1);
                else strncpy(c.ssid, c.bssid[0] ? c.bssid : name, sizeof(c.ssid) - 1);
                const char* pw = WPASec::getPassword(c.bssidHex);
                if (pw && pw[0]) {
                    strncpy(c.password, pw, sizeof(c.password) - 1);
                    c.status = CaptureStatus::CRACKED;
                } else if (WPASec::isUploaded(c.bssidHex)) {
                    c.status = CaptureStatus::UPLOADED;
                } else {
                    c.status = CaptureStatus::LOCAL;
                }
                count++;
            }
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();
}

void HashesMenu::show() {
    active = true;
    detailView = false;
    syncModal = false;
    diagModal = false;
    keyWasPressed = true;
    scan();
}

void HashesMenu::hide() {
    active = false;
    detailView = false;
    syncModal = false;
    diagModal = false;
}

const char* HashesMenu::getBottomHint() {
    return HINTS[hintIndex % HINT_COUNT];
}

void HashesMenu::runDiag() {
    diagLines = 0;
    auto add = [&](const char* s) {
        if (diagLines >= 8) return;
        strncpy(diagText[diagLines], s, 27);
        diagText[diagLines][27] = '\0';
        diagLines++;
    };
    add("WPA-SEC TEST");
    add(WPASec::hasApiKey() ? "KEY  ok" : "KEY  missing file");
    add(Net::hasStaCreds() ? "WIFI  saved" : "WIFI  no home SSID");
    char buf[28];
    snprintf(buf, sizeof(buf), "SD  %s", Storage::available() ? "ok" : "NO CARD");
    add(buf);
    snprintf(buf, sizeof(buf), "LOOT  %u", (unsigned)count);
    add(buf);
    snprintf(buf, sizeof(buf), "HEAP  %uK", (unsigned)(ESP.getFreeHeap() / 1024));
    add(buf);
    add(WPASec::canSync() ? "TLS  heap ok" : WPASec::getLastError());
    diagModal = true;
}

void HashesMenu::startSync() {
    if (Cap::isRunning()) Cap::stop();
    if (!Storage::available()) {
        Display::showToast("NO SD", 1500);
        return;
    }
    if (!WPASec::hasApiKey()) {
        Display::showToast("NO WPA KEY", 1500);
        return;
    }
    if (!Net::hasStaCreds()) {
        Display::showToast("SET HOME WIFI", 1500);
        return;
    }
    syncModal = true;
    strncpy(syncText, "CONNECTING...", sizeof(syncText) - 1);
    Avatar::suspendScene();
    Display::update();

    if (!connectHome()) {
        strncpy(syncText, "WIFI FAIL", sizeof(syncText) - 1);
        dropWifi();
        Avatar::resumeScene();
        return;
    }

    strncpy(syncText, "UPLOADING...", sizeof(syncText) - 1);
    Display::update();
    Storage::brewHeap();
    WPASecSyncResult r = WPASec::syncCaptures(Net::cfg().wpaSecKey, nullptr);
    dropWifi();
    Avatar::resumeScene();
    if (r.success) {
        snprintf(syncText, sizeof(syncText), "OK up%u skip%u crk%u",
                 r.uploaded, r.skipped, r.cracked);
    } else {
        snprintf(syncText, sizeof(syncText), "FAIL %s", r.error[0] ? r.error : "?");
    }
    scan();
}

void HashesMenu::handleInput() {
    if (!M5Cardputer.Keyboard.isPressed()) {
        keyWasPressed = false;
        return;
    }
    if (keyWasPressed) return;
    keyWasPressed = true;

    if (M5Cardputer.Keyboard.isKeyPressed('`')) {
        if (detailView) { detailView = false; return; }
        if (syncModal) { syncModal = false; return; }
        if (diagModal) { diagModal = false; return; }
        hide();
        return;
    }

    if (syncModal || diagModal) {
        if (M5Cardputer.Keyboard.keysState().enter) {
            syncModal = false;
            diagModal = false;
        }
        return;
    }

    if (detailView) {
        if (M5Cardputer.Keyboard.keysState().enter) detailView = false;
        return;
    }

    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
        hintIndex = (uint8_t)((hintIndex + HINT_COUNT - 1) % HINT_COUNT);
        if (selected > 0) {
            selected--;
            if (selected < scroll) scroll = selected;
        }
        SFX::play(SFX::MENU_CLICK);
    }
    if (M5Cardputer.Keyboard.isKeyPressed('.')) {
        hintIndex = (uint8_t)((hintIndex + 1) % HINT_COUNT);
        if (count && selected + 1 < count) {
            selected++;
            if (selected >= scroll + VISIBLE) scroll = (uint8_t)(selected - VISIBLE + 1);
        }
        SFX::play(SFX::MENU_CLICK);
    }
    if (M5Cardputer.Keyboard.keysState().enter && count) {
        detailView = true;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S')) {
        startSync();
    }
    if (M5Cardputer.Keyboard.isKeyPressed('t') || M5Cardputer.Keyboard.isKeyPressed('T')) {
        runDiag();
    }
}

void HashesMenu::update() {
    if (!active) return;
    handleInput();
}

void HashesMenu::draw(M5Canvas& canvas) {
    uiListBackground(canvas);
    canvas.setTextSize(1);

    if (!Storage::available()) {
        canvas.setTextColor(UiStyle::RED);
        canvas.setCursor(4, 40);
        canvas.print("NO SD CARD");
        canvas.setTextColor(UiStyle::TEXT);
        canvas.setCursor(4, 55);
        canvas.print("INSERT AND RESTART");
        return;
    }

    if (syncModal) {
        canvas.setTextColor(UiStyle::GOLD);
        canvas.setCursor(8, 28);
        canvas.print("WPA-SEC");
        canvas.setTextColor(UiStyle::TEXT);
        canvas.setCursor(8, 48);
        canvas.print(syncText);
        canvas.setTextColor(UiStyle::DIM);
        canvas.setCursor(8, 72);
        canvas.print("ENTER / `  close");
        return;
    }

    if (diagModal) {
        canvas.setTextColor(UiStyle::CYAN);
        for (uint8_t i = 0; i < diagLines; i++) {
            canvas.setCursor(6, 4 + i * 12);
            canvas.print(diagText[i]);
        }
        return;
    }

    if (detailView && selected < count) {
        const CaptureInfo& c = items[selected];
        canvas.setTextColor(UiStyle::PINK);
        canvas.setCursor(6, 6);
        canvas.print(c.ssid);
        canvas.setTextColor(UiStyle::TEXT);
        canvas.setCursor(6, 22);
        canvas.print(c.bssid);
        canvas.setCursor(6, 38);
        canvas.print(c.isPMKID ? "PMKID" : "HANDSHAKE");
        canvas.setCursor(6, 54);
        if (c.status == CaptureStatus::CRACKED) {
            canvas.setTextColor(UiStyle::GREEN);
            canvas.print(c.password);
        } else if (c.status == CaptureStatus::UPLOADED) {
            canvas.print("uploaded, waiting");
        } else {
            canvas.print("local only");
        }
        return;
    }

    if (count == 0) {
        canvas.setTextColor(UiStyle::GOLD);
        canvas.setCursor(4, 36);
        canvas.print("NO CAPTURES FOUND");
        canvas.setTextColor(UiStyle::TEXT);
        canvas.setCursor(4, 52);
        canvas.print("MENU -> ATTACK");
        return;
    }

    uint16_t ok = 0, up = 0, loc = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (items[i].status == CaptureStatus::CRACKED) ok++;
        else if (items[i].status == CaptureStatus::UPLOADED) up++;
        else loc++;
    }
    char summary[64];
    snprintf(summary, sizeof(summary), "LOOT %u OK %u UP %u LOC %u",
             (unsigned)count, (unsigned)ok, (unsigned)up, (unsigned)loc);
    canvas.setTextColor(UiStyle::GOLD);
    canvas.setCursor(4, 2);
    canvas.print(summary);

    canvas.setTextColor(UiStyle::CYAN);
    canvas.setCursor(4, 12);
    canvas.print("SSID");
    canvas.setCursor(120, 12);
    canvas.print("ST");
    canvas.setCursor(150, 12);
    canvas.print("TYPE");
    canvas.setCursor(190, 12);
    canvas.print("SIZE");

    int y = 22;
    for (uint8_t i = scroll; i < count && i < scroll + VISIBLE; i++) {
        const CaptureInfo& cap = items[i];
        bool sel = (i == selected);
        uiListRow(canvas, y, 16, sel, UiStyle::PINK);
        canvas.setTextColor(sel ? UiStyle::BG : UiStyle::TEXT);

        char ssidBuf[20];
        size_t pos = 0;
        const char* src = cap.ssid;
        while (*src && pos < 17) ssidBuf[pos++] = (char)toupper((unsigned char)*src++);
        ssidBuf[pos] = '\0';
        if (*src && pos >= 2) {
            ssidBuf[pos - 2] = '.';
            ssidBuf[pos - 1] = '.';
        }
        canvas.setCursor(8, y + 1);
        canvas.print(ssidBuf);

        canvas.setCursor(120, y);
        if (cap.status == CaptureStatus::CRACKED) canvas.print("[OK]");
        else if (cap.status == CaptureStatus::UPLOADED) canvas.print("[..]");
        else canvas.print("[--]");

        canvas.setCursor(150, y);
        canvas.print(cap.isPMKID ? "PM" : "HS");

        char sizeBuf[12];
        formatSize(sizeBuf, sizeof(sizeBuf), cap.fileSize);
        canvas.setCursor(190, y);
        canvas.print(sizeBuf);
        y += 16;
    }
}
