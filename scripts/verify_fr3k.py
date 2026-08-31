#!/usr/bin/env python3
"""Source-level release gates for the fR3k Cardputer build."""
from pathlib import Path
import re
import sys

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

require("-DFR3K_SAFE_BUILD=1" in pio, "safe build flag missing")
require('custom_version = 2.0.0-fr3k' in pio, "fR3k version missing")
require('drawTalk(farm, px, "fR3k")' in boot, "boot splash is not fR3k branded")
require('drawString("fR3k", DISPLAY_W / 2, 2)' in menu, "menu title is not fR3k")
require('FR3K_NAME, FR3K_VERSION, FR3K_BUILD' in main, "boot banner is not fR3k")
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

safe_root = menu.split("#if FR3K_SAFE_BUILD", 1)[1].split("#else", 1)[0]
for forbidden in ("ATTACK", "AGGRO", "EVILPIG", "PIGPASS", "DEAUTH", "SPECTRUM"):
    require(forbidden not in safe_root, f"safe root exposes {forbidden}")
require('"DISABLED: SAFE BUILD"' in menu, "unsafe action guard missing")
require("return false;\n#else\n    static const uint8_t ACT" in menu,
        "safe build hotkeys are not disabled")

# User-facing surfaces may keep the legacy path, but not legacy character branding.
for rel in ("src/ui/boot_splash.cpp", "src/ui/settings_menu.cpp"):
    data = text(rel)
    for literal in re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', data):
        if re.search(r"\b(pig|oink|snout|hoof|bacon)\b", literal, re.I):
            errors.append(f"pig-facing UI literal remains in {rel}: {literal}")

if errors:
    print("fR3k source verification FAILED")
    for err in errors:
        print(f"- {err}")
    sys.exit(1)
print("fR3k source verification PASS")
