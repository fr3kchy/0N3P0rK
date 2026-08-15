#include "loot_menu.h"
#include "display.h"
#include "keys.h"
#include "../core/app.h"
#include "../storage/littlefs_ops.h"
#include "../sync/wpasec.h"
#include "../sync/pwncrack.h"
#include "../net/ap_sta.h"
#include "../piglet/avatar.h"
#include "../audio/sfx.h"
#include "../cap/sniffer.h"
#include "../cap/capture_name.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <SD.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

bool LootMenu::active = false;
bool LootMenu::keyWasPressed = false;
bool LootMenu::detailView = false;
bool LootMenu::syncModal = false;
bool LootMenu::diagModal = false;
LootMenu::Tab LootMenu::tab = LootMenu::Tab::WPASEC;
uint8_t LootMenu::selected = 0;
uint8_t LootMenu::scroll = 0;
uint8_t LootMenu::count = 0;

enum class St : uint8_t { LOCAL, UPLOADED, CRACKED };

struct Row {
    char filename[48];
    char ssid[33];
    char id[18];
    char hex[13];
    uint32_t fileSize;
    bool isPMKID;
    St status;
    char password[64];
};

static Row s_rows[48];
static char s_syncText[48] = "";
static char s_diag[8][28];
static uint8_t s_diagN = 0;
static const uint8_t VISIBLE = 4;

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

static void fillIdentity(Row& r, const char* dir) {
    r.hex[0] = r.id[0] = '\0';
    if (!r.ssid[0]) r.ssid[0] = '\0';
    CapName::extractBssidHex(r.filename, r.hex);
    if (r.hex[0]) CapName::prettyMac(r.hex, r.id);

    char fromName[33] = {0};
    CapName::extractSsidFromName(r.filename, fromName);
    if (fromName[0]) strncpy(r.ssid, fromName, sizeof(r.ssid) - 1);

    char fromTxt[33] = {0};
    if (CapName::readCompanionSsid(dir, r.filename, fromTxt) && fromTxt[0]) {
        strncpy(r.ssid, fromTxt, sizeof(r.ssid) - 1);
    }

    if (endsWith(r.filename, ".22000") || endsWith(r.filename, ".hc22000")) {
        char hex[13] = {0}, ss[33] = {0};
        if (CapName::metaFrom22000File(dir, r.filename, hex, ss)) {
            if (hex[0] && !r.hex[0]) {
                strncpy(r.hex, hex, sizeof(r.hex) - 1);
                CapName::prettyMac(r.hex, r.id);
            }
            if (ss[0]) strncpy(r.ssid, ss, sizeof(r.ssid) - 1);
        }
    }
}

static void formatSize(char* out, size_t len, uint32_t bytes) {
    if (bytes < 1024) snprintf(out, len, "%uB", (unsigned)bytes);
    else snprintf(out, len, "%uKB", (unsigned)(bytes / 1024));
}

static bool connectHome() {
    return Net::joinHome(22000);
}

static void dropWifi() {
    Net::leaveHome();
}

void LootMenu::scan() {
    count = 0;
    selected = 0;
    scroll = 0;
    if (!Storage::available()) return;
    Storage::compactLoot();

    const bool wpa = (tab == Tab::WPASEC);
    if (wpa) {
        WPASec::freeCacheMemory();
        WPASec::loadCache();
    } else {
        Pwncrack::freeCacheMemory();
        Pwncrack::loadCache();
    }

    const char* dirs[2];
    uint8_t nd = 0;
    dirs[nd++] = Storage::DIR_HS;

    for (uint8_t d = 0; d < nd && count < 48; d++) {
        File root = SD.open(dirs[d]);
        if (!root || !root.isDirectory()) {
            if (root) root.close();
            continue;
        }
        File f = root.openNextFile();
        while (f && count < 48) {
            if (!f.isDirectory()) {
                const char* name = Storage::baseName(f.name());
                bool pcap = endsWith(name, ".pcap") || endsWith(name, ".pcapng");
                bool h220 = endsWith(name, ".22000") || endsWith(name, ".hc22000");
                if ((wpa && pcap) || (!wpa && h220)) {
                    bool dup = false;
                    for (uint8_t i = 0; i < count; i++) {
                        if (strcmp(s_rows[i].filename, name) == 0) { dup = true; break; }
                    }
                    if (!dup) {
                        Row tmp;
                        memset(&tmp, 0, sizeof(tmp));
                        strncpy(tmp.filename, name, sizeof(tmp.filename) - 1);
                        tmp.fileSize = (uint32_t)f.size();
                        tmp.isPMKID = h220 && !endsWith(name, "_hs.22000");
                        fillIdentity(tmp, dirs[d]);
                        int same = -1;
                        if (tmp.hex[0]) {
                            for (uint8_t i = 0; i < count; i++) {
                                if (s_rows[i].hex[0] && strcmp(s_rows[i].hex, tmp.hex) == 0) {
                                    same = (int)i;
                                    break;
                                }
                            }
                        }
                        if (same >= 0) {
                            Row& o = s_rows[same];
                            bool betterName = tmp.ssid[0] && strcasecmp(tmp.ssid, "HIDDEN") != 0 &&
                                (!o.ssid[0] || strcasecmp(o.ssid, "HIDDEN") == 0);
                            bool betterFile = (!tmp.isPMKID && o.isPMKID) ||
                                (tmp.fileSize > o.fileSize && tmp.isPMKID == o.isPMKID);
                            if (betterName) strncpy(o.ssid, tmp.ssid, sizeof(o.ssid) - 1);
                            if (betterFile) {
                                strncpy(o.filename, tmp.filename, sizeof(o.filename) - 1);
                                o.fileSize = tmp.fileSize;
                                o.isPMKID = tmp.isPMKID;
                            }
                        }
                        Row& r = (same >= 0) ? s_rows[same] : s_rows[count];
                        if (same < 0) r = tmp;
                        if (wpa) {
                            const char* potSs = r.hex[0] ? WPASec::getSSID(r.hex) : nullptr;
                            if (potSs && potSs[0]) strncpy(r.ssid, potSs, sizeof(r.ssid) - 1);
                            if (!r.ssid[0]) strncpy(r.ssid, r.id[0] ? r.id : name, sizeof(r.ssid) - 1);
                            const char* pw = r.hex[0] ? WPASec::getPassword(r.hex) : nullptr;
                            if ((!pw || !pw[0]) && r.ssid[0]) pw = WPASec::getPassword(r.ssid);
                            if ((!pw || !pw[0])) pw = WPASec::getPassword(name);
                            if (pw && pw[0]) {
                                strncpy(r.password, pw, sizeof(r.password) - 1);
                                r.status = St::CRACKED;
                            } else if (r.status != St::CRACKED &&
                                       ((r.hex[0] && WPASec::isUploaded(r.hex)) ||
                                        WPASec::isUploaded(name) ||
                                        (r.id[0] && WPASec::isUploaded(r.id)))) {
                                r.status = St::UPLOADED;
                            } else if (r.status != St::CRACKED && r.status != St::UPLOADED) {
                                r.status = St::LOCAL;
                            }
                        } else {
                            char stem[48];
                            strncpy(stem, name, sizeof(stem) - 1);
                            stem[sizeof(stem) - 1] = '\0';
                            char* dot = strchr(stem, '.');
                            if (dot) *dot = '\0';
                            const char* pw = Pwncrack::getPassword(stem);
                            if ((!pw || !pw[0]) && r.hex[0]) pw = Pwncrack::getPassword(r.hex);
                            if ((!pw || !pw[0]) && r.ssid[0]) pw = Pwncrack::getPassword(r.ssid);
                            if ((!pw || !pw[0])) pw = Pwncrack::getPassword(name);
                            if (pw && pw[0]) {
                                strncpy(r.password, pw, sizeof(r.password) - 1);
                                r.status = St::CRACKED;
                            } else if (r.status != St::CRACKED &&
                                       (Pwncrack::isUploaded(name) ||
                                        Pwncrack::isUploaded(stem) ||
                                        (r.hex[0] && Pwncrack::isUploaded(r.hex)))) {
                                r.status = St::UPLOADED;
                            } else if (r.status != St::CRACKED && r.status != St::UPLOADED) {
                                r.status = St::LOCAL;
                            }
                            if (!r.ssid[0]) strncpy(r.ssid, r.id[0] ? r.id : name, sizeof(r.ssid) - 1);
                        }
                        if (same < 0) count++;
                    }
                }
            }
            f.close();
            f = root.openNextFile();
        }
        root.close();
    }
}

void LootMenu::show() {
    active = true;
    detailView = false;
    syncModal = false;
    diagModal = false;
    keyWasPressed = true;
    scan();
}

void LootMenu::openWpaSec() {
    tab = Tab::WPASEC;
    show();
}

void LootMenu::openPwncrack() {
    tab = Tab::PWNCRACK;
    show();
}

void LootMenu::hide() {
    active = false;
    detailView = false;
    syncModal = false;
    diagModal = false;
}

const char* LootMenu::getBottomHint() {
    return "ENT:PASS  S:SYNC  ,/:TAB";
}

void LootMenu::runDiag() {
    s_diagN = 0;
    auto add = [&](const char* s) {
        if (s_diagN >= 8) return;
        strncpy(s_diag[s_diagN], s, 27);
        s_diag[s_diagN][27] = '\0';
        s_diagN++;
    };
    char buf[28];
    if (tab == Tab::WPASEC) {
        add("WPA-SEC TEST");
        add(WPASec::hasApiKey() ? "KEY  ok" : "KEY  /0N3P0rK/wpa-sec/");
        add(WPASec::canSync() ? "TLS  heap ok" : WPASec::getLastError());
    } else {
        add("PWNCRACK TEST");
        add(Pwncrack::hasApiKey() ? "KEY  ok" : "KEY  /0N3P0rK/pwncrack/");
        add(Pwncrack::canSync() ? "NET  heap ok" : Pwncrack::getLastError());
    }
    add(Net::hasStaCreds() ? "WIFI  saved" : "WIFI  no home SSID");
    snprintf(buf, sizeof(buf), "SD  %s", Storage::available() ? "ok" : "NO CARD");
    add(buf);
    snprintf(buf, sizeof(buf), "FILES %u", (unsigned)count);
    add(buf);
    diagModal = true;
}

void LootMenu::startSync() {
    if (Cap::isRunning()) Cap::stop();
    if (!Storage::available()) {
        Display::showToast("NO SD", 1500);
        return;
    }
    if (tab == Tab::WPASEC && !WPASec::hasApiKey()) {
        Display::showToast("NO WPA KEY", 1500);
        return;
    }
    if (tab == Tab::PWNCRACK && !Pwncrack::hasApiKey()) {
        Display::showToast("NO PWN KEY", 1500);
        return;
    }
    if (!Net::hasStaCreds()) {
        Display::showToast("SET HOME WIFI", 1500);
        return;
    }
    syncModal = true;
    strncpy(s_syncText, "CONNECTING...", sizeof(s_syncText) - 1);
    Avatar::suspendScene();
    SFX::stop();
    WPASec::freeCacheMemory();
    Pwncrack::freeCacheMemory();
    Display::update();
    Storage::brewHeap();

    if (!connectHome()) {
        strncpy(s_syncText, "WIFI FAIL", sizeof(s_syncText) - 1);
        dropWifi();
        Avatar::resumeScene();
        return;
    }

    auto onProg = [](const char* st, uint8_t p, uint8_t t) {
        if (t)
            snprintf(s_syncText, sizeof(s_syncText), "%s %u/%u", st, p, t);
        else
            strncpy(s_syncText, st ? st : "...", sizeof(s_syncText) - 1);
        // Do not redraw during TLS/HTTP — farm draw + WiFi stack reboots the S3.
    };
    strncpy(s_syncText, "SYNC...", sizeof(s_syncText) - 1);
    Display::update();
    Storage::brewHeap();
    if (tab == Tab::WPASEC) {
        WPASecSyncResult r = WPASec::syncCaptures(Net::cfg().wpaSecKey, onProg);
        if (r.success)
            snprintf(s_syncText, sizeof(s_syncText), "OK up%u skip%u crk%u",
                     r.uploaded, r.skipped, r.cracked);
        else
            snprintf(s_syncText, sizeof(s_syncText), "FAIL %s", r.error[0] ? r.error : "?");
    } else {
        PwncrackSyncResult r = Pwncrack::syncCaptures(Net::cfg().pwncrackKey, onProg);
        if (r.success)
            snprintf(s_syncText, sizeof(s_syncText), "OK up%u skip%u crk%u",
                     r.uploaded, r.skipped, r.cracked);
        else
            snprintf(s_syncText, sizeof(s_syncText), "FAIL %s", r.error[0] ? r.error : "?");
    }
    dropWifi();
    Avatar::resumeScene();
    scan();
}

void LootMenu::handleInput() {
    if (!keyNewPress(keyWasPressed)) return;

    auto keys = M5Cardputer.Keyboard.keysState();
    bool esc = keyEsc();
    if (esc) {
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

    if (M5Cardputer.Keyboard.isKeyPressed(',') || M5Cardputer.Keyboard.isKeyPressed('/')) {
        tab = (tab == Tab::WPASEC) ? Tab::PWNCRACK : Tab::WPASEC;
        SFX::play(SFX::MENU_CLICK);
        scan();
        return;
    }

    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
        if (selected > 0) {
            selected--;
            if (selected < scroll) scroll = selected;
        }
        SFX::play(SFX::MENU_CLICK);
    }
    if (M5Cardputer.Keyboard.isKeyPressed('.')) {
        if (count && selected + 1 < count) {
            selected++;
            if (selected >= scroll + VISIBLE) scroll = (uint8_t)(selected - VISIBLE + 1);
        }
        SFX::play(SFX::MENU_CLICK);
    }
    if (M5Cardputer.Keyboard.keysState().enter && count) detailView = true;
    if (M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S')) startSync();
    if (M5Cardputer.Keyboard.isKeyPressed('t') || M5Cardputer.Keyboard.isKeyPressed('T')) runDiag();
}

void LootMenu::update() {
    if (!active) return;
    if (App::windowHidden()) return;
    handleInput();
}

void LootMenu::draw(M5Canvas& canvas) {
    uiListBackground(canvas);
    canvas.setTextSize(1);

    canvas.fillRect(4, 2, 112, 13, tab == Tab::WPASEC ? UiStyle::PINK : UiStyle::PANEL);
    canvas.fillRect(124, 2, 112, 13, tab == Tab::PWNCRACK ? UiStyle::PINK : UiStyle::PANEL);
    canvas.setTextDatum(top_center);
    canvas.setTextSize(1);
    canvas.setTextColor(tab == Tab::WPASEC ? UiStyle::BG : UiStyle::TEXT);
    canvas.drawString("WPASEC", 60, 5);
    canvas.setTextColor(tab == Tab::PWNCRACK ? UiStyle::BG : UiStyle::TEXT);
    canvas.drawString("PWNCRACK", 180, 5);
    canvas.setTextDatum(top_left);

    if (!Storage::available()) {
        canvas.setTextColor(UiStyle::RED);
        canvas.setCursor(4, 40);
        canvas.print("NO SD CARD");
        return;
    }

    if (syncModal) {
        canvas.setTextColor(UiStyle::GOLD);
        canvas.setCursor(8, 28);
        canvas.print(tab == Tab::WPASEC ? "WPA-SEC" : "PWNCRACK");
        canvas.setTextColor(UiStyle::TEXT);
        canvas.setCursor(8, 48);
        canvas.print(s_syncText);
        return;
    }
    if (diagModal) {
        canvas.setTextColor(UiStyle::CYAN);
        for (uint8_t i = 0; i < s_diagN; i++) {
            canvas.setCursor(6, 18 + i * 12);
            canvas.print(s_diag[i]);
        }
        return;
    }
    if (detailView && selected < count) {
        const Row& c = s_rows[selected];
        canvas.setTextColor(UiStyle::PINK);
        canvas.setCursor(6, 18);
        canvas.print(c.ssid[0] ? c.ssid : "[UNKNOWN]");
        canvas.setTextColor(UiStyle::TEXT);
        canvas.setCursor(6, 30);
        canvas.print(c.id[0] ? c.id : c.filename);
        canvas.setCursor(6, 42);
        canvas.print(c.isPMKID ? "PMKID" : "HANDSHAKE");
        canvas.setCursor(6, 56);
        if (c.status == St::CRACKED && c.password[0]) {
            canvas.setTextColor(UiStyle::GREEN);
            canvas.print("PASS ");
            canvas.print(c.password);
        } else if (c.status == St::UPLOADED) {
            canvas.setTextColor(UiStyle::GOLD);
            canvas.print("uploaded, waiting");
        } else {
            canvas.setTextColor(UiStyle::DIM);
            canvas.print("local only");
        }
        return;
    }

    if (count == 0) {
        canvas.setTextColor(UiStyle::GOLD);
        canvas.setCursor(4, 36);
        canvas.print(tab == Tab::WPASEC ? "NO PCAP IN LOOT" : "NO 22000 IN LOOT");
        canvas.setTextColor(UiStyle::TEXT);
        canvas.setCursor(4, 52);
        canvas.print("/0N3P0rK/hs/");
        return;
    }

    uint16_t ok = 0, up = 0, loc = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (s_rows[i].status == St::CRACKED) ok++;
        else if (s_rows[i].status == St::UPLOADED) up++;
        else loc++;
    }
    char summary[64];
    snprintf(summary, sizeof(summary), "%u  OK %u  UP %u  LOC %u",
             (unsigned)count, (unsigned)ok, (unsigned)up, (unsigned)loc);
    canvas.setTextColor(UiStyle::GOLD);
    canvas.setCursor(4, 20);
    canvas.print(summary);

    canvas.setTextColor(UiStyle::CYAN);
    canvas.setCursor(4, 30);
    canvas.print("SSID");
    canvas.setCursor(120, 26);
    canvas.print("ST");
    canvas.setCursor(150, 26);
    canvas.print("TYPE");
    canvas.setCursor(190, 26);
    canvas.print("SIZE");

    int y = 36;
    for (uint8_t i = scroll; i < count && i < scroll + VISIBLE; i++) {
        const Row& cap = s_rows[i];
        bool sel = (i == selected);
        uiListRow(canvas, y, 14, sel, UiStyle::PINK);
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
        canvas.setCursor(8, y + 2);
        canvas.print(ssidBuf);
        canvas.setCursor(120, y + 2);
        if (cap.status == St::CRACKED) canvas.print("[OK]");
        else if (cap.status == St::UPLOADED) canvas.print("[..]");
        else canvas.print("[--]");
        canvas.setCursor(150, y + 2);
        canvas.print(cap.isPMKID ? "PM" : "HS");
        char sizeBuf[12];
        formatSize(sizeBuf, sizeof(sizeBuf), cap.fileSize);
        canvas.setCursor(190, y + 2);
        canvas.print(sizeBuf);
        y += 14;
    }
}
