#include "gps_mode.h"

#include "../gps/gps_service.h"
#include "../core/config.h"
#include "../ui/display.h"
#include "../ui/keys.h"
#include "../audio/sfx.h"
#include <M5Cardputer.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace GpsMode {

static bool s_running = false;
static bool s_keyWas = false;
// fR3k v3.0.4: display mode (compact one-line vs verbose full panel).
// 'M' hotkey toggles. Default = verbose (matches the restored v1 layout).
enum class DispMode : uint8_t { COMPACT = 0, VERBOSE = 1 };
static DispMode s_disp = DispMode::VERBOSE;

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
    } else if (M5Cardputer.Keyboard.isKeyPressed('m') || M5Cardputer.Keyboard.isKeyPressed('M')) {
        // fR3k v3.0.4: toggle compact / verbose display.
        s_disp = (s_disp == DispMode::VERBOSE) ? DispMode::COMPACT : DispMode::VERBOSE;
        Display::showToast(s_disp == DispMode::VERBOSE ? "MODE VERBOSE" : "MODE COMPACT", 700);
    } else if (M5Cardputer.Keyboard.isKeyPressed('r') || M5Cardputer.Keyboard.isKeyPressed('R')) {
        // fR3k v3.0.4: reset trip odometer.
        GpsService::resetTrip();
        Display::showToast("TRIP RESET", 700);
    }
    SFX::playNav();
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
    const uint16_t CHIP_BG = 0x2945, CHIP_BORDER = 0x5C9A;
    const GpsSnapshot s = GpsService::snapshot();
    const auto& cfg = Config::gps();

    c.fillSprite(BG);
    c.setFont(&fonts::Font0);

    // Title + status badge (top-right: FIX / SEARCH / OFF).
    c.setTextDatum(top_center);
    c.setTextSize(2);
    c.setTextColor(TITLE);
    c.drawString("fR3k GPS", 120, 2);
    c.drawLine(10, 20, 230, 20, TITLE);

    // Status pill in top-right corner.
    const char* pill = s.fix ? "FIX" : (s.enabled ? "SEARCH" : "OFF");
    const uint16_t pillColor = s.fix ? GOOD : (s.enabled ? ACCENT : BAD);
    c.setTextSize(1);
    c.setTextDatum(top_right);
    c.setTextColor(pillColor);
    c.drawString(pill, 234, 6);

    // MODE chip in top-left corner.
    c.setTextDatum(top_left);
    c.setTextColor(DIM);
    c.drawString(s_disp == DispMode::VERBOSE ? "VERB" : "CMPC", 4, 6);

    // -- VERBOSE LAYOUT (restored v1 panel) --
    if (s_disp == DispMode::VERBOSE) {
        // Big LAT / LON at the top of the body.
        c.setTextSize(2);
        c.setTextColor(s.fix ? GOOD : DIM);
        char coord[40];
        snprintf(coord, sizeof(coord), "%.4f %s", fabs(s.latitude),
                 s.latitude >= 0 ? "N" : "S");
        c.setTextDatum(top_left);
        c.drawString(coord, 6, 26);

        snprintf(coord, sizeof(coord), "%.4f %s", fabs(s.longitude),
                 s.longitude >= 0 ? "E" : "W");
        c.setTextColor(s.fix ? GOOD : DIM);
        c.drawString(coord, 6, 46);

        // Secondary metrics row.
        c.setTextSize(1);
        c.setTextColor(TEXT);
        char line[64];
        char age[16];
        if (s.fixAgeMs == UINT32_MAX) snprintf(age, sizeof(age), "--");
        else snprintf(age, sizeof(age), "%lums", (unsigned long)s.fixAgeMs);
        snprintf(line, sizeof(line), "ALT %.1fm  SPD %.1fkm/h  HDOP %.1f  AGE %s",
                 s.altitudeM, s.speedKph, s.hdop, age);
        c.drawString(line, 6, 68);

        snprintf(line, sizeof(line), "CRS %.1f %s  TRIP %.2fkm",
                 s.courseDeg, s.courseValid ? GpsService::cardinal(s.courseDeg) : "--",
                 s.tripDistM / 1000.0);
        c.drawString(line, 6, 80);

        // Sat signal panel — three horizontal bars by SNR band.
        c.setTextColor(DIM);
        c.drawString("SATS", 6, 92);
        const uint16_t COLOR_HI = 0x07FF;  // cyan-ish for high
        const uint16_t COLOR_MID = 0xC618; // grey for mid
        const uint16_t COLOR_LO = 0xF800;  // red for low
        const int sbX = 30, sbY = 92, sbW = 200, sbH = 5;
        c.drawRect(sbX, sbY, sbW, sbH, DIM);
        if (s.satellites) {
            const uint16_t tot = (uint16_t)(s.satellites > 31 ? 31 : s.satellites);
            const uint16_t hiW = (uint16_t)((s.satHigh * sbW) / (tot ? tot : 1));
            const uint16_t midW = (uint16_t)((s.satMid * sbW) / (tot ? tot : 1));
            const uint16_t loW = (uint16_t)((s.satLow * sbW) / (tot ? tot : 1));
            if (hiW) c.fillRect(sbX, sbY, hiW, sbH, COLOR_HI);
            if (midW) c.fillRect(sbX + hiW, sbY, midW, sbH, COLOR_MID);
            if (loW)  c.fillRect(sbX + hiW + midW, sbY, loW, sbH, COLOR_LO);
        }
        c.setTextColor(ACCENT);
        snprintf(line, sizeof(line), "%lu", (unsigned long)s.satellites);
        c.drawString(line, 234, 92);

        // Local time big if valid.
        if (s.timeValid) {
            int totalMinutes = (int)s.hour * 60 + s.minute + (int)cfg.timezoneQuarterHours * 15;
            while (totalMinutes < 0) totalMinutes += 1440;
            while (totalMinutes >= 1440) totalMinutes -= 1440;
            c.setTextSize(2);
            c.setTextColor(TEXT);
            snprintf(line, sizeof(line), "%02d:%02d:%02d", totalMinutes / 60,
                     totalMinutes % 60, s.second);
            c.setTextDatum(top_right);
            c.drawString(line, 234, 26);
            c.setTextSize(1);
            c.setTextColor(DIM);
            snprintf(line, sizeof(line), "%04u-%02u-%02u", (unsigned)s.year,
                     (unsigned)s.month, (unsigned)s.day);
            c.drawString(line, 234, 46);
            c.setTextDatum(top_left);
        }

        // Mode chips row.
        auto drawChip = [&](const char* label, const char* value, int x, int y, bool on) {
            c.drawRect(x, y, 38, 11, CHIP_BORDER);
            c.setTextColor(on ? GOOD : DIM);
            c.setTextDatum(top_left);
            c.drawString(label, x + 2, y + 1);
            c.setTextDatum(top_right);
            c.setTextColor(on ? ACCENT : DIM);
            c.drawString(value, x + 36, y + 1);
            c.setTextDatum(top_left);
        };
        drawChip("B", baudLabel(cfg.baudMode), 6, 104, true);
        drawChip("L", cfg.logging ? "ON" : "OFF", 48, 104, cfg.logging);
        drawChip("U", cfg.syncUtc ? "UTC" : "LOC", 90, 104, cfg.syncUtc);
        char tzbuf[8];
        snprintf(tzbuf, sizeof(tzbuf), "%+0.1fh", (double)cfg.timezoneQuarterHours / 4.0);
        drawChip("TZ", tzbuf, 132, 104, true);
        c.setTextColor(DIM);
        c.drawString("G GPS  M MODE  R TRIP  -/+ TZ  ESC", 6, 120);
    } else {
        // -- COMPACT LAYOUT --
        c.setTextSize(1);
        c.setTextColor(TEXT);
        char line[64];
        char age[16];
        if (s.fixAgeMs == UINT32_MAX) snprintf(age, sizeof(age), "--");
        else snprintf(age, sizeof(age), "%lums", (unsigned long)s.fixAgeMs);
        snprintf(line, sizeof(line), "LAT %.5f  LON %.5f", s.latitude, s.longitude);
        c.drawString(line, 6, 30);
        snprintf(line, sizeof(line), "ALT %.1fm  SPD %.1fkm/h  SAT %lu  AGE %s",
                 s.altitudeM, s.speedKph, (unsigned long)s.satellites, age);
        c.drawString(line, 6, 44);
        snprintf(line, sizeof(line), "TRIP %.2fkm  B %s  L %s  U %s  TZ %+0.1fh",
                 s.tripDistM / 1000.0, baudLabel(cfg.baudMode),
                 cfg.logging ? "ON" : "OFF", cfg.syncUtc ? "UTC" : "LOC",
                 (double)cfg.timezoneQuarterHours / 4.0);
        c.drawString(line, 6, 58);
        c.setTextColor(DIM);
        c.drawString("G GPS  M MODE  R TRIP  -/+ TZ  B/L/U toggles  ESC", 6, 78);
    }
}

}  // namespace GpsMode
