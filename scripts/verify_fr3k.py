#!/usr/bin/env python3
"""Source-level release gates for the fR3k Cardputer build.

v3 lab-unlock gates run alongside the v2 carry-over gates. The script
intentionally reads source only - no binary inspection - so a clean run
confirms the source tree is shippable and the v3 features are present.

Usage:
  python3 scripts/verify_fr3k.py
"""

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
errors: list[str] = []


def text(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")


def require(ok: bool, message: str) -> None:
    if not ok:
        errors.append(message)


pio = text("platformio.ini")
menu = text("src/ui/menu.cpp")
boot = text("src/ui/boot_splash.cpp")
main = text("src/main.cpp")
avatar = text("src/piglet/avatar.cpp")
demon = text("src/piglet/demon_renderer.cpp")
gps = text("src/gps/gps_service.cpp")
gps_h = text("src/gps/gps_service.h")
storage_h = text("src/storage/littlefs_ops.h")
settings = text("src/ui/settings_menu.cpp")
sfx_h = text("src/audio/sfx.h")
sfx_cpp = text("src/audio/sfx.cpp")
mood_cpp = text("src/piglet/mood.cpp")
lab_h = text("src/lab/lab_unlock.h")
lab_cpp = text("src/lab/lab_unlock.cpp")
config_h = text("src/core/config.h")
config_cpp = text("src/core/config.cpp")
telem_h = text("src/telemetry/telemetry.h")
telem_cpp = text("src/telemetry/telemetry.cpp")
sky_h = text("src/piglet/spectrum_sky.h")
sky_cpp = text("src/piglet/spectrum_sky.cpp")
au_db = text("src/ir/aus_brand_db.h")
irport_cpp = text("src/modes/irport.cpp")

# ===[ v2 carry-over ] ===]
require("m5cardputer-safe" in pio, "v2 safe env missing")
require("m5cardputer" in pio, "default env missing")
require("custom_version = 3.0.4-fr3k-lab" in pio, "fR3k v3 version missing")
require("-DFR3K_SAFE_BUILD=1" in pio, "safe build flag missing")
require("FR3K_SAFE_DEFAULT" in pio, "FR3K_SAFE_DEFAULT flag missing")
require('drawTalk(farm, px, "fR3k")' in boot, "boot splash is not fR3k branded")
require('drawString("fR3k", DISPLAY_W / 2, 2)' in menu, "menu title is not fR3k")
require("FR3K_NAME, FR3K_VERSION, FR3K_BUILD" in main, "boot banner is not fR3k")
require('const char* title = "DEMON"' in settings, "demon settings title missing")
require('"SHOW DEMON"' in settings, "demon scene toggle missing")

require("DemonRenderer::draw" in avatar, "avatar does not call demon renderer")
for feature in ("horn", "tail", "eye", "Palette"):
    require(feature in demon, f"demon renderer lacks {feature} implementation")

require("RX_PIN = 15" in gps_h and "TX_PIN = 13" in gps_h,
        "Mesh Cap GPS pins must be RX=15 TX=13")
for token in ("TinyGPSPlus", "BAUD_FAST", "BAUD_SLOW", "appendGpsCsv",
              "GPS FIX ACQUIRED", "GPS FIX LOST", "settimeofday"):
    require(token in gps + gps_h, f"GPS implementation missing {token}")
require('FILE_GPS_TRACK = "/0N3P0rK/gps/track.csv"' in storage_h,
        "GPS CSV path missing")
require('DIR_ROOT       = "/0N3P0rK"' in storage_h,
        "legacy save root changed")

# Find the v2 safe ROOT[] block. The block starts at the first
# `#if FR3K_SAFE_BUILD` and ends at the matching `#elif !FR3K_SAFE_BUILD`
# (which immediately precedes the v3 ROOT_LOCKED / ROOT_OPEN arrays).
parts = menu.split("#if FR3K_SAFE_BUILD", 1)
if len(parts) != 2:
    require(False, "no #if FR3K_SAFE_BUILD block in menu.cpp")
    safe_root = ""
else:
    safe_root = parts[1].split("#elif !FR3K_SAFE_BUILD", 1)[0]
for forbidden in ("ATTACK", "AGGRO", "EVILPIG", "PIGPASS", "DEAUTH", "SPECTRUM"):
    require(forbidden not in safe_root, f"safe root exposes {forbidden}")
require('"DISABLED: SAFE BUILD"' in menu, "unsafe action guard missing")
# Safe build still refuses every offensive hotkey via the compile-time
# gate (FR3K_SAFE_BUILD). The default v3 build adds a runtime unlock
# check on top of the existing ACT[] table; either way the safe path
# can't be skipped.
require(re.search(r"#if FR3K_SAFE_BUILD\s*\n\s*return false;\s*\n#else", menu)
        is not None, "safe build hotkeys are not disabled")

# ===[ v3 lab gate ] ===]
require("namespace Lab" in lab_h, "LabUnlock namespace missing")
require("cd3f0c85b158c08a2b113464991810cf2cdfc387" in lab_cpp,
        "sha-1(\"666\") hardcoded value mismatch")
require("Preferences" in lab_cpp or "preferences" in lab_cpp.lower(),
        "LabUnlock must use NVS/Preferences backend")
require("LAB UNLOCK" in menu, "Lab action missing from menu")
require("case 22:" in menu, "Lab action case missing")
require("SettingsPage::LAB" in settings,
        "SettingsPage::LAB enum missing")
require("ENTER 666" in settings, "LAB page banner missing")

# ===[ v3 SFX — demon words + CUNT ] ===]
for word in ("ACK", "HEY", "NAH", "MUM", "OOF", "CUNT"):
    require(word in sfx_cpp, f"SFX sequence {word} missing")
# OINK_* must be GONE from sfx.cpp
for forbidden_oink in ("SND_OINK_HAPPY", "SND_OINK_GRUNT", "SND_OINK_SQUEAL",
                       "SND_OINK_OINK"):
    require(forbidden_oink not in sfx_cpp,
            f"OINK sequence {forbidden_oink} still present")
require("playPersonality" in sfx_cpp,
        "SFX::playPersonality() helper missing")
require("playCuntJingle" in sfx_cpp,
        "SFX::playCuntJingle() helper missing")
require("setMenuMode" in sfx_cpp,
        "SFX::setMenuMode() gate missing (v3.0.2 per-screen audio gate)")
require("setMinimalTap" in sfx_cpp,
        "SFX::setMinimalTap() helper missing (v3.0.2 menu minimal piezo blip)")
require("playNav" in sfx_cpp,
        "SFX::playNav() helper missing (v3.0.1 queue-bypassing nav tap)")
# v3.0.2: SFX::init() must seed s_inMenu / s_minimalTap so the gate
# is well-defined at boot before App::setMode() runs the first time.
require("s_inMenu = false" in sfx_cpp,
        "SFX::init() must seed s_inMenu (v3.0.2 menu gate)")
require("s_minimalTap = false" in sfx_cpp,
        "SFX::init() must seed s_minimalTap (v3.0.2 menu gate)")
# v3.0.2: app.cpp::setMode() must call SFX::setMenuMode() to drive
# the gate from the current AppMode. FARM is the only non-menu state.
require("SFX::setMenuMode" in open("src/core/app.cpp").read(),
        "App::setMode() must call SFX::setMenuMode() (v3.0.2)")
# v3.0.2: settings rows for the operator to override the default.
require("MENU SOUND" in open("src/ui/settings_menu.cpp").read(),
        "MENU SOUND settings row missing (v3.0.2)")
require("MENU TAP" in open("src/ui/settings_menu.cpp").read(),
        "MENU TAP settings row missing (v3.0.2)")
# v3.0.2: PersonalityConfig carries the new fields.
config_h = open("src/core/config.h").read()
require("menuSound" in config_h,
        "PersonalityConfig::menuSound missing (v3.0.2)")
require("menuMinimalTap" in config_h,
        "PersonalityConfig::menuMinimalTap missing (v3.0.2)")
# v3.0.2: SFX::play / playPersonality / playCuntJingle / update all
# gate on s_inMenu. play() / playPersonality() / playCuntJingle()
# use single-line `if (s_inMenu) return;`; playNav() and update() use
# multi-line `if (s_inMenu) { ... }` blocks. Require at least 3 of
# each form so the gate is present in every audio path.
# v3.0.3: spectrum-sky overlay removed from the render path. The
# main loop must NOT call SpectrumSky::begin(); the avatar draw path
# must NOT call SpectrumSky::drawBackground(). The source files are
# still present (preserve for re-enable) so this is a behavioural
# gate, not a presence gate.
main_cpp = open("src/main.cpp").read()
# Match a call site (not a comment). Look for the call preceded by
# whitespace and not a `//` line.
import re
def has_call(source, call):
    for line in source.splitlines():
        s = line.strip()
        if s.startswith("//"):
            continue
        if call in s:
            return True
    return False
require(not has_call(main_cpp, "SpectrumSky::begin()"),
        "main.cpp must not call SpectrumSky::begin() (v3.0.3 spectrum-sky removed)")
avatar_cpp = open("src/piglet/avatar.cpp").read()
require(not has_call(avatar_cpp, "SpectrumSky::drawBackground"),
        "avatar.cpp must not call SpectrumSky::drawBackground() (v3.0.3 spectrum-sky removed)")
# The settings row id 23 must be hard-coded to 0 (forced off) and
# the setValue path must not toggle it back on.
settings_cpp = open("src/ui/settings_menu.cpp").read()
require("case 23: return 0;  // fR3k v3.0.3" in settings_cpp,
        "settings row 23 (SPECTRUM SKY) must be forced off in getValue (v3.0.3)")
require('"SPECTRUM SKY OFF (v3.0.3)"' in settings_cpp,
        "settings row 23 setValue must toast SPECTRUM SKY OFF (v3.0.3)")
require(not has_call(settings_cpp, "SpectrumSky::setEnabled"),
        "settings must NOT call SpectrumSky::setEnabled (v3.0.3 removed)")
require(sfx_cpp.count("if (s_inMenu) return") >= 3,
        "play/playPersonality/playCuntJingle must all gate on s_inMenu (v3.0.2)")
require(sfx_cpp.count("if (s_inMenu) {") >= 2,
        "playNav/update must both gate on s_inMenu with multi-line form (v3.0.2)")
# v3.0.1: CLICK/MENU_CLICK dispatcher arms must be no-ops so legacy
# SFX::play(SFX::CLICK) callers don't double-strike the speaker.
# playNav() is the new entry point and is wired at every call site.
require("startSequence(SND_CLICK)" not in sfx_cpp,
        "SND_CLICK sequence still dispatched - migrate to SFX::playNav()")
require("startSequence(SND_MENU_CLICK)" not in sfx_cpp,
        "SND_MENU_CLICK sequence still dispatched - migrate to SFX::playNav()")
# No remaining SFX::play(SFX::CLICK) / SFX::play(SFX::MENU_CLICK) call
# sites - the replacement is comprehensive.
import os as _os
for _root, _dirs, _files in _os.walk("src"):
    for _fn in _files:
        if not (_fn.endswith(".cpp") or _fn.endswith(".h")):
            continue
        if _fn == "sfx.cpp" or _fn == "sfx.h":
            continue
        _data = (_root + "/" + _fn) if _root != "src" else ("src/" + _fn)
        with open(_data) as _f:
            _src = _f.read()
        if "SFX::play(SFX::CLICK)" in _src:
            errors.append(f"{_data}: SFX::play(SFX::CLICK) still present")
        if "SFX::play(SFX::MENU_CLICK)" in _src:
            errors.append(f"{_data}: SFX::play(SFX::MENU_CLICK) still present")
require("voiceWord" in config_h and "voiceWord" in config_cpp,
        "PersonalityConfig::voiceWord missing")
require("cuntJingle" in config_h and "cuntJingle" in config_cpp,
        "PersonalityConfig::cuntJingle missing")
require("playPersonality" in mood_cpp,
        "mood.cpp must dispatch personality voice")

# ===[ v3 telemetry ] ===]
require("namespace Telemetry" in telem_h, "Telemetry namespace missing")
require("RING_N" in telem_h, "Telemetry RING_N constant missing")
require("DIR_TELEMETRY" in storage_h, "Telemetry dir constant missing")
require("/0N3P0rK/telemetry" in storage_h, "Telemetry path missing")

# ===[ v3 spectrum-sky (REMOVED in v3.0.3) ] ===
# Source files preserved for future re-enable but the render path no
# longer calls them. Behavioural gate: not the namespace's presence.
require("namespace SpectrumSky" in sky_h, "SpectrumSky namespace missing (preserve)")
require("SpectrumSky::drawBackground" not in avatar,
        "avatar must NOT call SpectrumSky::drawBackground (v3.0.3 removed)")

# ===[ v3 IR AU ] ===]
require("AUS_PROTO_NEC" in au_db, "AU NEC protocol missing")
require("AUS_PROTO_NEC42" in au_db, "AU NEC42 protocol missing")
require("AUS_PROTO_SAMSUNG" in au_db, "AU SAMSUNG protocol missing")
require("AUS_PROTO_SONY" in au_db, "AU SONY protocol missing")
require("kCarrierKHz = 38" in au_db, "AU carrier must be 38 kHz")
require("kWavelengthNm = 940" in au_db, "AU wavelength must be 940 nm")
require("kSingleShot = true" in au_db, "AU must be single-shot")
brand_count = len(re.findall(r"^    \{ AUS_PROTO_", au_db, re.M))
require(brand_count >= 40, f"AU brand DB has only {brand_count} entries (need >=40)")
require("loadAuBrands" in irport_cpp, "irport loadAuBrands missing")
require("Pack::AU_BRANDS" in irport_cpp, "irport AU_BRANDS pack missing")

# ===[ v3 status page ] ===]
require('"LAB"' in settings and '"TELEM"' in settings,
        "STATUS page must include LAB + TELEM rows")
# v3.0.3 spectrum-sky enable/disable: behavioural check lives in the
# v3.0.3 block above using has_call() to ignore comments.

# User-facing surfaces may keep the legacy path, but not legacy character
# branding.
for rel in ("src/ui/boot_splash.cpp", "src/ui/settings_menu.cpp"):
    data = text(rel)
    for literal in re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', data):
        if re.search(r"\b(pig|oink|snout|hoof|bacon)\b", literal, re.I):
            errors.append(f"pig-facing UI literal remains in {rel}: {literal}")

# ===[ v3.0.4: GPS restore, WPA-sec overhaul, Wigle, voice fix ] ===]
gps_cpp = text("src/modes/gps_mode.cpp")
gps_snap = text("src/gps/gps_service.h")
gps_svc = text("src/gps/gps_service.cpp")
require("DispMode" in gps_cpp, "GPS mode must have DispMode enum (v3.0.4)")
require("'m'" in gps_cpp,
        "GPS mode must accept M/m for display mode toggle (v3.0.4)")
require("'r'" in gps_cpp,
        "GPS mode must accept R/r for trip reset (v3.0.4)")
require("tripDistM" in gps_snap,
        "GpsSnapshot must carry tripDistM (v3.0.4)")
require("resetTrip" in gps_svc,
        "GpsService must expose resetTrip() (v3.0.4)")
require("getTripDistM" in gps_svc,
        "GpsService must expose getTripDistM() (v3.0.4)")
# Voice fix
require("cuntJingle ||" in text("src/audio/sfx.cpp"),
        "playPersonality() must fire CUNT jingle when voiceWord == CUNT (v3.0.4)")
# Wigle
require_path = os.path.exists("src/sync/wigle.h") and os.path.exists("src/sync/wigle.cpp")
require(require_path, "Wigle client must exist (src/sync/wigle.{h,cpp})")
wigle_h = text("src/sync/wigle.h")
require("uploadRecommended" in wigle_h, "Wigle::uploadRecommended must be declared (v3.0.4)")
require("recommendCount" in wigle_h, "Wigle::recommendCount must be declared (v3.0.4)")
require("getMaskedToken" in wigle_h, "Wigle::getMaskedToken must be declared (v3.0.4)")
# Settings
require("WIGLE = 8" in text("src/ui/settings_menu.h") or "WIGLE = 8" in text("src/ui/settings_menu.cpp"),
        "SettingsPage::WIGLE = 8 must exist (v3.0.4)")
require("WIGLE NAME" in text("src/ui/settings_menu.cpp"),
        "WIGLE NAME row must exist (v3.0.4)")
require("WIGLE TOKEN" in text("src/ui/settings_menu.cpp"),
        "WIGLE TOKEN row must exist (v3.0.4)")
require("AUTO SYNC" in text("src/ui/settings_menu.cpp"),
        "AUTO SYNC row must exist (v3.0.4)")
# LootMenu sort
loot_cpp = text("src/ui/loot_menu.cpp")
require("SortMode" in loot_cpp and "SortMode::DAY" in loot_cpp,
        "LootMenu::SortMode::DAY must be defined (v3.0.4)")
require("recommendUpload" in loot_cpp,
        "LootMenu must compute recommendUpload per row (v3.0.4)")
require("isKeyPressed('o')" in loot_cpp or "isKeyPressed('O')" in loot_cpp,
        "LootMenu must accept O/o for sort cycle (v3.0.4)")
require("isKeyPressed('w')" in loot_cpp or "isKeyPressed('W')" in loot_cpp,
        "LootMenu must accept W/w for Wigle upload (v3.0.4)")
require("pullPotfileIfStale" in text("src/sync/wpasec.cpp"),
        "WPASec::pullPotfileIfStale must be defined (v3.0.4)")
# GPX export
require("FileMgrMode_gpxExport" in text("src/modes/filemgr.cpp"),
        "GPX export function must be defined in filemgr.cpp (v3.0.4)")

if errors:
    print("fR3k source verification FAILED")
    for err in errors:
        print(f"- {err}")
    sys.exit(1)
print("fR3k source verification PASS")