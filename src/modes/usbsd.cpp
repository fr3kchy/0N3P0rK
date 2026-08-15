// SD card as a USB disk on the PC. Same idea as Launcher USB MSC.
#include "usbsd.h"
#include "../storage/littlefs_ops.h"
#include "../cap/sniffer.h"
#include "../ui/display.h"
#include "../ui/keys.h"
#include "../piglet/avatar.h"
#include "../audio/sfx.h"
#include <M5Cardputer.h>
#include <string.h>
#include <stdio.h>

#if !ARDUINO_USB_MODE
#include "USB.h"
#include "USBMSC.h"
#endif

namespace UsbSdMode {

static bool s_run = false;
static bool s_ok = false;
static bool s_eject = false;
static bool s_keyWas = false;
static volatile uint32_t s_reads = 0;
static volatile uint32_t s_writes = 0;
static volatile uint32_t s_lastIo = 0;
static uint32_t s_sectors = 0;
static uint16_t s_secSz = 512;

#if !ARDUINO_USB_MODE
static USBMSC s_msc;

static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    uint32_t ss = s_secSz;
    if (!ss || offset || (bufsize % ss) != 0) return -1;
    uint8_t* p = (uint8_t*)buffer;
    uint32_t n = bufsize / ss;
    for (uint32_t i = 0; i < n; i++) {
        if (!Storage::readSector(lba + i, p + i * ss)) return -1;
    }
    s_reads++;
    s_lastIo = millis();
    return (int32_t)bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    uint32_t ss = s_secSz;
    if (!ss || offset || (bufsize % ss) != 0) return -1;
    uint32_t n = bufsize / ss;
    for (uint32_t i = 0; i < n; i++) {
        if (!Storage::writeSector(lba + i, buffer + i * ss)) return -1;
    }
    s_writes++;
    s_lastIo = millis();
    return (int32_t)bufsize;
}

static bool onStartStop(uint8_t, bool start, bool loadEject) {
    if (!start && loadEject) s_eject = true;
    return true;
}
#endif

void start() {
    if (s_run) return;
    if (Cap::isRunning()) Cap::stop();
    Avatar::suspendScene();
    s_run = true;
    s_ok = false;
    s_eject = false;
    s_keyWas = true;
    s_reads = 0;
    s_writes = 0;
    s_lastIo = 0;

    if (!Storage::available() && !Storage::begin()) {
        Display::showToast("NO SD");
        return;
    }

#if ARDUINO_USB_MODE
    Display::showToast("USB MODE OFF");
    return;
#else
    s_secSz = (uint16_t)Storage::sectorSize();
    s_sectors = Storage::numSectors();
    if (!s_secSz || !s_sectors) {
        Display::showToast("SD RAW FAIL");
        return;
    }

    s_msc.vendorID("0N3P0rK");
    s_msc.productID("SD CARD");
    s_msc.productRevision("1.0");
    s_msc.onRead(onRead);
    s_msc.onWrite(onWrite);
    s_msc.onStartStop(onStartStop);
    s_msc.mediaPresent(true);
    if (!s_msc.begin(s_sectors, s_secSz)) {
        Display::showToast("USB MSC FAIL");
        s_msc.mediaPresent(false);
        return;
    }
    USB.begin();
    s_ok = true;
    SFX::play(SFX::CONFIRM);
    Serial.printf("[USBSD] disk %u x %u\n", (unsigned)s_sectors, (unsigned)s_secSz);
#endif
}

void stop() {
    if (!s_run) return;
#if !ARDUINO_USB_MODE
    if (s_ok) {
        s_msc.mediaPresent(false);
        s_msc.end();
    }
#endif
    s_ok = false;
    s_run = false;
    Storage::remount();
    Avatar::resumeScene();
    Serial.println("[USBSD] stop");
}

void update() {
    if (!s_run) return;
    if (!keyNewPress(s_keyWas)) return;
    if (keyEsc()) {
        stop();
        return;
    }
}

bool isRunning() { return s_run; }

void getStatusLine(char* out, size_t n) {
    if (!out || !n) return;
    if (!s_ok) {
        snprintf(out, n, "USB SD  NO CARD");
        return;
    }
    if (s_eject) snprintf(out, n, "USB SD  EJECTED  ESC");
    else snprintf(out, n, "USB SD  R%lu W%lu  ESC",
                  (unsigned long)s_reads, (unsigned long)s_writes);
}

void draw(M5Canvas& canvas) {
    uint16_t fg = getColorFG();
    uint16_t bg = getColorBG();
    canvas.fillSprite(bg);
    canvas.setTextSize(1);
    canvas.setTextDatum(top_center);

    canvas.setTextColor(UiStyle::GOLD);
    canvas.drawString("USB SD", 120, 6);

    int bx = 52, by = 26, bw = 90, bh = 36;
    canvas.fillRoundRect(bx, by, bw, bh, 4, UiStyle::CYAN);
    canvas.fillRoundRect(bx + bw - 2, by + 6, 28, bh - 12, 3, UiStyle::DIM);
    canvas.fillRoundRect(bx + bw + 6, by + 10, 10, 6, 1, 0x4208);
    canvas.fillRoundRect(bx + bw + 6, by + 22, 10, 6, 1, 0x4208);
    bool live = s_ok && !s_eject && (millis() - s_lastIo < 400);
    canvas.fillRoundRect(bx + 8, by + 8, 6, 20, 1,
                         s_eject ? UiStyle::DIM : (live ? UiStyle::GREEN : UiStyle::RED));

    canvas.setTextDatum(top_left);
    canvas.setTextColor(fg);
    if (!Storage::available()) {
        canvas.drawString("NO SD CARD", 8, 72);
    } else if (!s_ok) {
        canvas.drawString("MSC DID NOT START", 8, 72);
    } else if (s_eject) {
        canvas.setTextColor(UiStyle::GREEN);
        canvas.drawString("PC EJECTED. ESC LEAVES.", 8, 72);
    } else {
        char line[40];
        uint32_t mb = (s_sectors / 2048); // 512B sectors
        snprintf(line, sizeof(line), "DISK %lu MB  PLUG USB", (unsigned long)mb);
        canvas.drawString(line, 8, 70);
        snprintf(line, sizeof(line), "R %lu   W %lu",
                 (unsigned long)s_reads, (unsigned long)s_writes);
        canvas.setTextColor(UiStyle::PINK);
        canvas.drawString(line, 8, 82);
        canvas.setTextColor(UiStyle::DIM);
        canvas.drawString("EJECT ON PC THEN ESC", 8, 94);
    }
}

}  // namespace UsbSdMode
