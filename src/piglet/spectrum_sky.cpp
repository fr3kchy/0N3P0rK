// fR3k v3 spectrum-sky implementation.
// Sky-side histogram of 2.4 GHz RSSI per channel (1..13). When the
// Cap::RunMode::Light sniffer is live, it feeds the per-channel RSSI
// table (noteRssi in cap/sniffer.cpp). When the sniffer is not running
// (safe build / locked lab / paused cap), a passive WiFi.scanNetworks()
// poller fills the table once every 5 s.

#include "spectrum_sky.h"
#include "../core/config.h"
#include "../lab/lab_unlock.h"
#include "../cap/sniffer.h"
#include <M5Unified.h>
#include <WiFi.h>
#include <string.h>
#include <stdio.h>

namespace SpectrumSky {

static bool s_enabled = true;     // mirror of personality toggle
static bool s_scanAsync = false;  // WiFi.scanNetworks running?
static uint32_t s_lastScanMs = 0;
static const uint32_t kScanPeriodMs = 5000;

// 13 smoothed bar levels in 0..63 pixels.
static uint8_t s_level[13];

// Map a dBm reading to a 0..63 px bar height. -100 dBm -> 0 px (silent),
// -30 dBm -> 63 px (full). Clamp both ends so noisy RF can't render bars
// outside the canvas.
static uint8_t dBmToBar(int8_t rssi) {
    if (rssi >= -30) return 63;
    if (rssi <= -100) return 0;
    int pct = (-30 - rssi) * 63 / (-30 - -100);
    if (pct < 0) pct = 0;
    if (pct > 63) pct = 63;
    return (uint8_t)pct;
}

void begin() {
    s_enabled = true;
    s_scanAsync = false;
    s_lastScanMs = 0;
    for (int i = 0; i < 13; i++) s_level[i] = 0;
}

void setEnabled(bool on) {
    s_enabled = on;
    if (!s_enabled) {
        for (int i = 0; i < 13; i++) s_level[i] = 0;
    }
}

bool isEnabled() { return s_enabled; }

static void pollSafeScan() {
    if (!s_enabled) return;
    if (s_scanAsync) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) return;
        s_scanAsync = false;
        if (n == WIFI_SCAN_FAILED || n <= 0) {
            s_lastScanMs = millis();
            return;
        }
        // Walk the scan results and bucket each AP's RSSI into its
        // channel. We use the strongest RSSI for each channel so the
        // histogram reflects "how loud is ch N right now", not "how many
        // APs are on ch N".
        int8_t best[14];
        for (int i = 0; i < 14; i++) best[i] = -127;
        for (int i = 0; i < n; i++) {
            const char* ssid = WiFi.SSID(i).c_str();
            (void)ssid;
            int32_t ch = WiFi.channel(i);
            int32_t rssi = WiFi.RSSI(i);
            if (ch < 1 || ch > 13) continue;
            if (rssi > best[ch]) best[ch] = (int8_t)rssi;
        }
        WiFi.scanDelete();
        for (uint8_t ch = 1; ch <= 13; ch++) {
            // 3-tap EMA merge so the bar inherits last frame's stability.
            int prev = s_level[ch - 1];
            uint8_t bar = dBmToBar(best[ch]);
            int ema = (prev * 2 + bar) / 3;
            s_level[ch - 1] = (uint8_t)ema;
        }
        s_lastScanMs = millis();
        return;
    }
    // Not running - kick off the next scan if the period has elapsed.
    if ((millis() - s_lastScanMs) >= kScanPeriodMs) {
        // Sync=false: return immediately, results polled via scanComplete.
        WiFi.scanNetworks(false, true);
        s_scanAsync = true;
    }
}

void feed() {
    if (!s_enabled) return;
    if (Lab::isUnlocked() && Cap::runMode() == Cap::RunMode::Light && Cap::isRunning()) {
        int8_t rssi14[14];
        Cap::getRssi13(rssi14);
        for (uint8_t ch = 1; ch <= 13; ch++) {
            uint8_t bar = dBmToBar(rssi14[ch]);
            int prev = s_level[ch - 1];
            int ema = (prev * 2 + bar) / 3;
            s_level[ch - 1] = (uint8_t)ema;
        }
        // Reset scan timer so we don't double-feed the histogram.
        s_lastScanMs = millis();
        return;
    }
    // Safe build / locked / paused: passive scan poll.
    pollSafeScan();
}

static uint16_t colorForBar(uint8_t bar) {
    // 0..15 dim cyan (cold channel), 16..40 cyan->magenta ramp,
    // 41..63 magenta->red (hot channel).
    if (bar <= 15) {
        return (uint16_t)((bar << 3) & 0x001F) | 0x0000;  // muted dark cyan
    }
    if (bar <= 40) {
        uint8_t t = (uint8_t)((bar - 16) * 255 / 24);
        return (uint16_t)((t >> 1) & 0x001F) | (uint16_t)((t) & 0x07E0) | 0xF800;
    }
    uint8_t t = (uint8_t)((bar - 41) * 255 / 22);
    return (uint16_t)((255 - t) & 0x001F) | (uint16_t)((255 - t >> 2) & 0x07E0) | 0xF800;
}

void drawBackground(M5Canvas& canvas) {
    if (!s_enabled) return;
    // Pull the most recent frame so the histogram doesn't go stale.
    feed();

    // Bar layout: 13 channels across the screen with 2 px gap, baseline
    // at y=110 (just above the grass), max height 50 px.
    constexpr int kBaseY  = 110;
    constexpr int kMaxH   = 50;
    constexpr int kBarW   = 16;
    constexpr int kGap    = 2;
    constexpr int kStartX = (240 - (13 * kBarW + 12 * kGap)) / 2;

    // Baseline trace - thin dark line so the bars look like they sit on
    // a transparent shelf rather than a slab.
    canvas.drawFastHLine(0, kBaseY, 240, 0x0000);

    for (uint8_t ch = 0; ch < 13; ch++) {
        uint8_t bar = s_level[ch];
        if (bar > kMaxH) bar = kMaxH;
        int x = kStartX + ch * (kBarW + kGap);
        int y = kBaseY - bar;
        // 3-px highlight at the top, body fills below
        canvas.fillRect(x, y, kBarW, bar, colorForBar(bar));
        canvas.fillRect(x, kBaseY, kBarW, 1, 0x0000);
    }

    // Tiny channel labels under the bars: only show 1, 6, 11 to keep the
    // sky readable.
    canvas.setTextSize(1);
    canvas.setTextColor(0x0000);
    int y0 = kBaseY + 4;
    char lbl[4];
    snprintf(lbl, sizeof(lbl), "%d", 1);
    canvas.drawString(lbl, kStartX + 4, y0);
    snprintf(lbl, sizeof(lbl), "%d", 6);
    canvas.drawString(lbl, kStartX + 5 * (kBarW + kGap) + 4, y0);
    snprintf(lbl, sizeof(lbl), "%d", 11);
    canvas.drawString(lbl, kStartX + 10 * (kBarW + kGap) + 4, y0);
}

}  // namespace SpectrumSky