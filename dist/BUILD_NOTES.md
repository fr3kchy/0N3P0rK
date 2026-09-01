# fR3k Cardputer Build Notes (v3.0.0-fr3k-lab)

## Provenance

- Upstream: `https://github.com/lexilexiko/0N3P0rK`
- Upstream branch: `Methodik`
- Upstream base commit: `4985112`
- fR3k version: `3.0.0-fr3k-lab` (default env), `3.0.0-fr3k-safe` (safe env)
- Build profile: lab-gated distributable + safe-v2 fallback

## Toolchain

- PlatformIO Core: `6.1.19`
- Platform: `espressif32@6.12.0`
- Default environment: `m5cardputer` (FR3K_SAFE_DEFAULT=0)
- Safe environment:    `m5cardputer-safe` (FR3K_SAFE_BUILD=1)
- Board definition: `m5stack-stamps3`
- Target MCU: ESP32-S3, 240 MHz, 8 MB flash
- Framework: Arduino
- Arduino-ESP32: `2.0.17` (`framework-arduinoespressif32 3.20017.241212`)
- ESP-IDF base: `v4.4.7`
- TinyGPSPlus: `1.1.0`, vendored under `lib/TinyGPSPlus`

## GNSS hardware

This build targets the M5Stack Cardputer Mesh Kit's Cap LoRa-1262 /
ATGM336H-6N GNSS interface, not the Cardputer's HY2.0/Grove port.

- Cardputer ADV UART RX: `GPIO15`, connected to Cap `GPS_TX`
- Cardputer ADV UART TX: `GPIO13`, connected to Cap `GPS_RX`
- UART format: `8N1`
- Default/fast baud: `115200`
- Fallback baud: `9600`
- AUTO mode: starts at 115200 and alternates with 9600 every four seconds
  until a checksum-valid NMEA sentence is observed

## Build commands

```sh
/home/parrot/.platformio/penv/bin/pio run -e m5cardputer       # default (lab-gated)
/home/parrot/.platformio/penv/bin/pio run -e m5cardputer-safe  # strict-safe v2
```

Result: **both SUCCESS**

| Build          | Image bytes  | % of app partition | SHA-256 |
|----------------|-------------:|-------------------:|---------|
| `m5cardputer`      | 2,090,816 | 79.8 % | `d275c1a7a1ea88db63f3e3d0a0c36032f5b9c72baa23812d55e10a8ef60baea8` |
| `m5cardputer-safe` | 2,089,424 | 79.7 % | `2f456db9cebd07d4f89fedee8e5f4b9f023db590c468148f9271d831993c9134` |

App partition size: 2,621,440 bytes. ~530 KB headroom in both builds.

## Safe-build controls (m5cardputer-safe)

- Network / capture / portal modes are not initialised at boot.
- The safe root menu contains DEMON, GPS, STATUS, SYSTEM, FILES, CONNECT.
- No `LAB UNLOCK` row appears in the safe build.
- Offensive radio hotkeys return false in the safe build.
- Any legacy offensive action ID is rejected with `DISABLED: SAFE BUILD`.
- Legacy radio TX settings are forced false when configuration loads.
- Dormant upstream radio implementation remains in source for provenance
  but has no active safe-build menu or hotkey launch path.

## v3 lab-gate controls (m5cardputer)

- Default env ships the offensive code path behind the runtime gate.
- Root menu exposes `LAB UNLOCK` until the operator enters `666` via the
  new `SettingsPage::LAB`.
- After unlock the root switches to the original upstream
  `ATTACK / LOOT / DEMON / SET` shape with all 10 hotkeys active.
- `Lab::begin()` initialises the persisted unlock flag from NVS namespace
  `fr3klab` (`unlock` uint8 + `mask` uint8).
- SHA-1 of "666" baked in as `cd3f0c85b158c08a2b113464991810cf2cdfc387`.
  Implementation is RFC 3174 / FIPS 180-4 (hand-rolled, no mbedTLS).
- The verify script (`tools/lab_unlock_check.py`) re-implements the same
  SHA-1 in Python and confirms the constant matches.
- `tools/lab_unlock_check.py` rejects 11 negative cases
  (`667 / 665 / 0666 / 6666 / 66 / 6660 / " 666 " / "PIG" / "  " /
  " six six six " / "6 6 6"`).

## v3 spectrum-sky

- 13-bar 2.4 GHz RSSI histogram drawn between sky and clouds.
- No second canvas. The bars ride the existing mainCanvas to keep the v2
  image budget intact (~530 KB headroom).
- Feed source: sniffer per-channel RSSI table (`Cap::getRssi13()`) when
  `Cap::RunMode::Light` is live; passive `WiFi.scanNetworks()` poll every
  5 s otherwise.
- Toggle: `SETTINGS > DEMON > SPECTRUM SKY`.

## v3 Australian IR DB

- 75 brand entries across 4 protocols (NEC, NEC42, SAMSUNG, SONY).
- Carrier locked to 38 kHz / 940 nm. Single-shot only. No repeats.
- Generator: `tools/aus_ir_codes.py` (parses inline YAML, emits
  `src/ir/aus_brand_db.h`).
- Self-test: `tools/aus_ir_codes_test.py` re-emits the header and asserts
  byte-for-byte agreement with the on-disk copy.

## v3 telemetry

- 96-record ring buffer (5 min @ 5 s), 16 bytes per record.
- Daily-rotated file: `/0N3P0rK/telemetry/YYYY-MM-DD.bin`.
- Records: heap, largest free block, satellites, fix age, captures,
  day/night flag.
- STATUS page renders a single-pixel-high sparkline of the last 5 min.

## v3 demon voice

- Five 2-step pitch contours in 280-1100 Hz range, ~80-120 ms each.
  `ACK / HEY / NAH / MUM / OOF` replace the v2 `OINK_*` sequences.
- New `SFX::CUNT` event + `SFX::playCuntJingle()` for the operator-default
  personality jingle.
- Default `voiceWord = VOICE_CUNT` per operator instruction. Switchable
  via `SETTINGS > DEMON > SOUND WORD`.
- `Mood::pet / Mood::feed / boot_splash` dispatch via
  `SFX::playPersonality()`.

## Verification performed

- Clean upstream baseline build: PASS
- Final PlatformIO build (`m5cardputer`): PASS
- Final PlatformIO build (`m5cardputer-safe`): PASS
- Source release gates (`scripts/verify_fr3k.py`): PASS
- AU brand DB self-test (`tools/aus_ir_codes_test.py`): PASS
- Lab unlock self-test (`tools/lab_unlock_check.py`): PASS
- `git diff --check`: PASS
- ESP32-S3 image checksum and validation hash: PASS
- Physical GNSS fix, SD append, IR blast, lab unlock on-device:
  : not hardware-tested in this build session