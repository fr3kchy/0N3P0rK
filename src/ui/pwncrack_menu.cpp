#include "pwncrack_menu.h"
#include "display.h"
#include "keys.h"
#include "../storage/littlefs_ops.h"
#include "../sync/pwncrack.h"
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

bool PwncrackMenu::active = false;
bool PwncrackMenu::keyWasPressed = false;
bool PwncrackMenu::detailView = false;
bool PwncrackMenu::syncModal = false;
bool PwncrackMenu::diagModal = false;
uint8_t PwncrackMenu::selected = 0;
uint8_t PwncrackMenu::scroll = 0;
uint8_t PwncrackMenu::count = 0;
uint8_t PwncrackMenu::hintIndex = 0;
char PwncrackMenu::syncText[48] = "";
char PwncrackMenu::diagText[8][28];
uint8_t PwncrackMenu::diagLines = 0;

enum class PwnSt : uint8_t { LOCAL, UPLOADED, CRACKED };

struct PwnRow {
    char filename[48];
    char ssid[33];
    char id[32];
    uint32_t fileSize;
    bool isPMKID;
    PwnSt status;
    char password[64];
};

static PwnRow s_rows[48];

static const char* const HINTS[] = {
    "ENT:DET  S:SYNC  T:TEST",
    "R:KEY  C:CLR UPLOAD LOG",
    "OK=PASS  ..=SENT  --=LOC",
    "KEY IN /0N3P0rK/pwncrack/"
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

static bool connectHome() {
    return Net::joinHome(22000);
}

static void dropWifi() {
    Net::leaveHome();
}

void PwncrackMenu::scan() {
    count = 0;
    selected = 0;
    scroll = 0;
    if (!Storage::available()) return;
    Pwncrack::loadCache();
    File root = SD.open(Storage::DIR_HS);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }
    File f = root.openNextFile();
    while (f && count < 48) {
        if (!f.isDirectory()) {
            const char* name = Storage::baseName(f.name());
            bool hash = endsWith(name, ".22000") || endsWith(name, ".hc22000");
            if (hash) {
                PwnRow& c = s_rows[count];
                memset(&c, 0, sizeof(c));
                strncpy(c.filename, name, sizeof(c.filename) - 1);
                c.fileSize = (uint32_t)f.size();
                c.isPMKID = !endsWith(name, "_hs.22000");
                strncpy(c.id, name, sizeof(c.id) - 1);
                char* dot = strchr(c.id, '.');
                if (dot) *dot = '\0';
                const char* ss = Pwncrack::getPassword(c.id);
                (void)ss;
                const char* lab = c.id;
                strncpy(c.ssid, lab, sizeof(c.ssid) - 1);
                const char* pw = Pwncrack::getPassword(c.id);
                if (pw && pw[0]) {
                    strncpy(c.password, pw, sizeof(c.password) - 1);
                    c.status = PwnSt::CRACKED;
                } else if (Pwncrack::isUploaded(c.id)) {
                    c.status = PwnSt::UPLOADED;
                } else {
                    c.status = PwnSt::LOCAL;
                }
                count++;
            }
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();
}

void PwncrackMenu::show() {
    active = true;
    detailView = false;
    syncModal = false;
    diagModal = false;
    keyWasPressed = true;
    scan();
}

void PwncrackMenu::hide() {
    active = false;
    detailView = false;
    syncModal = false;
    diagModal = false;
}

const char* PwncrackMenu::getBottomHint() {
    return HINTS[hintIndex % HINT_COUNT];
}

void PwncrackMenu::runDiag() {
    diagLines = 0;
    auto add = [&](const char* s) {
        if (diagLines >= 8) return;
        strncpy(diagText[diagLines], s, 27);
        diagText[diagLines][27] = '\0';
        diagLines++;
    };
    add("PWNCRACK TEST");
    add(Pwncrack::hasApiKey() ? "KEY  ok" : "KEY  missing file");
    add(Net::hasStaCreds() ? "WIFI  saved" : "WIFI  no home SSID");
    char buf[28];
    snprintf(buf, sizeof(buf), "SD  %s", Storage::available() ? "ok" : "NO CARD");
    add(buf);
    snprintf(buf, sizeof(buf), "HASH  %u", (unsigned)count);
    add(buf);
    add(Pwncrack::canSync() ? "NET  heap ok" : Pwncrack::getLastError());
    diagModal = true;
}

void PwncrackMenu::startSync() {
    if (Cap::isRunning()) Cap::stop();
    if (!Storage::available()) {
        Display::showToast("NO SD", 1500);
        return;
    }
    if (!Pwncrack::hasApiKey()) {
        Display::showToast("NO PWN KEY", 1500);
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
    PwncrackSyncResult r = Pwncrack::syncCaptures(Net::cfg().pwncrackKey);
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

void PwncrackMenu::handleInput() {
    if (!M5Cardputer.Keyboard.isPressed()) {
        keyWasPressed = false;
        return;
    }
    if (keyWasPressed) return;
    keyWasPressed = true;

    if (keyEsc()) {
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
    if (M5Cardputer.Keyboard.keysState().enter && count) detailView = true;
    if (M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S')) {
        startSync();
    }
    if (M5Cardputer.Keyboard.isKeyPressed('t') || M5Cardputer.Keyboard.isKeyPressed('T')) {
        runDiag();
    }
}

void PwncrackMenu::update() {
    if (!active) return;
    handleInput();
}

void PwncrackMenu::draw(M5Canvas& canvas) {
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
        canvas.print("PWNCRACK");
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
        const PwnRow& c = s_rows[selected];
        canvas.setTextColor(UiStyle::PINK);
        canvas.setCursor(6, 6);
        canvas.print(c.ssid);
        canvas.setTextColor(UiStyle::TEXT);
        canvas.setCursor(6, 22);
        canvas.print(c.filename);
        canvas.setCursor(6, 38);
        canvas.print(c.isPMKID ? "PMKID" : "HS");
        canvas.setCursor(6, 54);
        if (c.status == PwnSt::CRACKED) {
            canvas.setTextColor(UiStyle::GREEN);
            canvas.print(c.password);
        } else if (c.status == PwnSt::UPLOADED) {
            canvas.print("uploaded, waiting");
        } else {
            canvas.print("local only");
        }
        return;
    }

    if (count == 0) {
        canvas.setTextColor(UiStyle::GOLD);
        canvas.setCursor(4, 36);
        canvas.print("NO 22000 FILES");
        canvas.setTextColor(UiStyle::TEXT);
        canvas.setCursor(4, 52);
        canvas.print("MENU -> ATTACK");
        return;
    }

    uint16_t ok = 0, up = 0, loc = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (s_rows[i].status == PwnSt::CRACKED) ok++;
        else if (s_rows[i].status == PwnSt::UPLOADED) up++;
        else loc++;
    }
    char summary[64];
    snprintf(summary, sizeof(summary), "PWN %u OK %u UP %u LOC %u",
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
        const PwnRow& cap = s_rows[i];
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
        if (cap.status == PwnSt::CRACKED) canvas.print("[OK]");
        else if (cap.status == PwnSt::UPLOADED) canvas.print("[..]");
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
