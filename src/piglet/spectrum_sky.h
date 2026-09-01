// fR3k v3 spectrum-sky overlay.
//
// Draws a 13-bar 2.4 GHz spectrum histogram between the sky gradient and
// the cloud layer so the operator can see ambient RF at a glance. The
// histogram is fed by Cap::RunMode::Light when the lab is unlocked and
// the sniffer is running; otherwise a passive WiFi.scanNetworks() poller
// fills it from observed beacons every 5 s.
//
// Bars are 1-tap EMA smoothed so they twitch without flickering. Drawn
// directly into the supplied M5Canvas - no second canvas is allocated
// because the v2 image is already at 79% flash budget.
#pragma once

#include <M5Unified.h>
#include <stdint.h>

namespace SpectrumSky {

// Begin: schedule the next safe-build scan, restore the on-disk toggle
// from the personality config.
void begin();

// Mirror the personality toggle into the module without dragging Config
// into the header. Called by settings_menu.cpp after a SET.
void setEnabled(bool on);
bool isEnabled();

// Called once per redraw pass after Sky::drawBackdrop(). Draws nothing
// when disabled or when the sniffer is running (the spectrum hunt path
// already overlays a richer bar).
void drawBackground(M5Canvas& canvas);

// Called by the sniffer loop when Cap::RunMode::Light is active so the
// sky histogram mirrors the most recent per-channel RSSI table. Cheap:
// 14-byte memcpy + 13 multiplies + 13 draws. Skips a draw pass when
// nothing meaningful changed since the last feed().
void feed();

}  // namespace SpectrumSky