#include "gps_mode.h"

#include "../gps/gps_service.h"
#include "../core/config.h"
#include "../ui/display.h"
#include "../ui/keys.h"
#include "../audio/sfx.h"
#include <M5Cardputer.h>
#include <stdio.h>
#include <string.h>

namespace GpsMode {

static bool s_running = false;
static bool s_keyWas = false;

void start() {
    s_running = true;
    s_keyWas = true;
}

void stop() { s_running = false; }
bool isRunning() { return s_running; }

static const char* baudLabel(uint8_t mode) {
    if (mode == 1) return "115200";
    if (mode == 2) return "9600";
    return "AUTO";
}

void update() {
    if (!s_running) return;
    const bool down = M5Cardputer.Keyboard.isPressed();
    const bool edge = down && !s_keyWas;
    s_keyWas = down;
    if (!edge) return;
    if (keyEsc()) {
        stop();
        return;
    }

    auto& cfg = Config::gps();
    if (M5Cardputer.Keyboard.isKeyPressed('g') || M5Cardputer.Keyboard.isKeyPressed('G')) {
        cfg.enabled = !cfg.enabled;
        Config::save();
        GpsService::restart();
        Display::showToast(cfg.enabled ? "GPS ON" : "GPS OFF", 900);
    } else if (M5Cardputer.Keyboard.isKeyPressed('b') || M5Cardputer.Keyboard.isKeyPressed('B')) {
        cfg.baudMode = (uint8_t)((cfg.baudMode + 1) % 3);
        Config::save();
        GpsService::restart();
        Display::showToast(baudLabel(cfg.baudMode), 900);
    } else if (M5Cardputer.Keyboard.isKeyPressed('l') || M5Cardputer.Keyboard.isKeyPressed('L')) {
        if (!cfg.logging) {
            const GpsSnapshot s = GpsService::snapshot();
            if (!s.fix) {
                Display::showToast("GPS LOG NEEDS FIX", 1300);
                return;
            }
            if (!Config::isSDAvailable()) {
                Display::showToast("GPS LOG NEEDS SD", 1300);
                return;
            }
        }
        cfg.logging = !cfg.logging;
        Config::save();
        Display::showToast(cfg.logging ? "GPS LOG ON" : "GPS LOG OFF", 900);
    } else if (M5Cardputer.Keyboard.isKeyPressed('u') || M5Cardputer.Keyboard.isKeyPressed('U')) {
        cfg.syncUtc = !cfg.syncUtc;
        Config::save();
        if (cfg.syncUtc) GpsService::requestClockSync();
        Display::showToast(cfg.syncUtc ? "UTC SYNC ON" : "UTC SYNC OFF", 900);
    } else if (M5Cardputer.Keyboard.isKeyPressed('-') || M5Cardputer.Keyboard.isKeyPressed('_')) {
        if (cfg.timezoneQuarterHours > -48) cfg.timezoneQuarterHours--;
        Config::save();
    } else if (M5Cardputer.Keyboard.isKeyPressed('=') || M5Cardputer.Keyboard.isKeyPressed('+')) {
        if (cfg.timezoneQuarterHours < 56) cfg.timezoneQuarterHours++;
        Config::save();
    }
    SFX::play(SFX::CLICK);
}

void getStatusLine(char* out, size_t len) {
    if (!out || len == 0) return;
    const GpsSnapshot s = GpsService::snapshot();
    if (!s.enabled) snprintf(out, len, "GPS OFF");
    else if (!s.fix) snprintf(out, len, "GPS SEARCH %lu SAT", (unsigned long)s.satellites);
    else snprintf(out, len, "GPS FIX %lu SAT %s", (unsigned long)s.satellites,
                  Config::gps().logging ? "LOG" : "");
}

void draw(M5Canvas& c) {
    const uint16_t BG = 0x2145, TITLE = 0xFFE0, TEXT = 0xEF5D, DIM = 0x9CD3;
    const uint16_t GOOD = 0x07E0, BAD = 0xF800, ACCENT = 0xFDB6;
    const GpsSnapshot s = GpsService::snapshot();
    const auto& cfg = Config::gps();

    c.fillSprite(BG);
    c.setFont(&fonts::Font0);
    c.setTextDatum(top_center);
    c.setTextSize(2);
    c.setTextColor(TITLE);
    c.drawString("fR3k GPS", 120, 2);
    c.drawLine(10, 20, 230, 20, TITLE);
    c.setTextSize(1);
    c.setTextDatum(top_left);

    char line[64];
    c.setTextColor(s.fix ? GOOD : BAD);
    char age[16];
    if (s.fixAgeMs == UINT32_MAX) snprintf(age, sizeof(age), "--");
    else snprintf(age, sizeof(age), "%lums", (unsigned long)s.fixAgeMs);
    snprintf(line, sizeof(line), "%s  SAT %lu  HDOP %.1f  AGE %s",
             s.fix ? "FIX" : (s.enabled ? "SEARCH" : "OFF"),
             (unsigned long)s.satellites, s.hdop, age);
    c.drawString(line, 6, 24);

    c.setTextColor(TEXT);
    snprintf(line, sizeof(line), "LAT  %.6f", s.latitude); c.drawString(line, 6, 36);
    snprintf(line, sizeof(line), "LON  %.6f", s.longitude); c.drawString(line, 6, 48);
    snprintf(line, sizeof(line), "ALT  %.1fm   SPD %.1fkm/h", s.altitudeM, s.speedKph); c.drawString(line, 6, 60);
    snprintf(line, sizeof(line), "COURSE %.1f %s", s.courseDeg,
             s.courseValid ? GpsService::cardinal(s.courseDeg) : "--"); c.drawString(line, 6, 72);

    if (s.timeValid) {
        int totalMinutes = (int)s.hour * 60 + s.minute + (int)cfg.timezoneQuarterHours * 15;
        while (totalMinutes < 0) totalMinutes += 1440;
        while (totalMinutes >= 1440) totalMinutes -= 1440;
        snprintf(line, sizeof(line), "UTC %02u:%02u:%02u  LOCAL %02d:%02d",
                 (unsigned)s.hour, (unsigned)s.minute, (unsigned)s.second,
                 totalMinutes / 60, totalMinutes % 60);
    } else {
        snprintf(line, sizeof(line), "UTC --:--:--  LOCAL --:--");
    }
    c.drawString(line, 6, 84);

    c.setTextColor(ACCENT);
    snprintf(line, sizeof(line), "B %s  L %s  U %s  TZ %+0.2fh",
             baudLabel(cfg.baudMode), cfg.logging ? "ON" : "OFF",
             cfg.syncUtc ? "ON" : "OFF", (double)cfg.timezoneQuarterHours / 4.0);
    c.drawString(line, 6, 96);
    c.setTextColor(DIM);
    c.drawString("G GPS  B BAUD  L LOG  U UTC  -/+ TZ", 6, 108);
}

}  // namespace GpsMode
