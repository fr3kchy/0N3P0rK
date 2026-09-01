# fR3k — Demon Farm + GNSS for M5Cardputer

fR3k 3.0.0-fr3k-lab is a fork of lexilexiko's 0N3P0rK. It preserves the
Tamagotchi farm, world artwork, movement, progression, weather, seasons,
trees, collisions, wolf, props, cards, saves and animation timing while
replacing the procedural pig with a procedural demon.

v3 ships two PlatformIO environments:

- `m5cardputer` — default. The offensive radio code path is compiled in
  but gated by the new runtime `Lab::unlock("666")` flag. Until the
  operator unlocks, the menu refuses every attack tool and the boot
  lifecycle skips `Cap::begin / EvilPigMode::init / PigpassMode::init`.
- `m5cardputer-safe` — strict-safe distributable. Identical to v2.0.0-fr3k
  in behaviour: no offensive labels, no hotkey launch, no initialised
  radio subsystems, `FR3K_SAFE_BUILD=1`.

The v3 default distributable **does not** expose offensive radio launch
paths until the operator enters `666` via `SETTINGS > LAB UNLOCK`. The
safe distributable never does.

## Target hardware

- M5Stack Cardputer / Cardputer ADV
- ESP32-S3, 8 MB flash
- Cardputer Mesh Kit Cap LoRa-1262 with integrated ATGM336H-6N GNSS
- microSD for optional GPS CSV logging and existing save content

## Build

```sh
/home/parrot/.platformio/penv/bin/pio run -e m5cardputer       # lab-gated v3
/home/parrot/.platformio/penv/bin/pio run -e m5cardputer-safe  # strict-safe v3
```

PlatformIO environment: `m5cardputer` (default) and `m5cardputer-safe`
(strict-safe).

- Platform: `espressif32@6.12.0`
- Board: `m5stack-stamps3`
- Framework: Arduino
- Output: `.pio/build/m5cardputer/firmware.bin`

See `BUILD_NOTES.md` for the verified toolchain, memory use, image size, SHA-256 and exact hardware facts.

## fR3k interface

### Safe distributable (`m5cardputer-safe`)

The safe root menu contains:

1. DEMON — name, demon palette, season, sky, scene layers and life settings
2. GPS — fix telemetry, UART/baud controls, UTC sync, timezone and CSV logging
3. STATUS — board, battery, SD, GPS, logging, safe-build state and version
4. SYSTEM — brightness, sound and dimming
5. FILES — SD/internal file manager inherited from the upstream architecture
6. CONNECT — benign Wi-Fi configuration/scan surface

### Lab-gated distributable (`m5cardputer`)

Locked root menu (matches the safe shape plus one extra row):

1. DEMON
2. GPS
3. STATUS
4. SYSTEM
5. FILES
6. CONNECT
7. **LAB UNLOCK** — opens `SETTINGS > LAB UNLOCK`. Enter the password
   `666` (text input, hashes via SHA-1) to unlock the lab.

After unlock the root menu switches to the upstream
`ATTACK / LOOT / DEMON / SET` shape with all 10 hotkeys active.

Press the normal back key to return to the farm.

## Demon

The new renderer uses the original avatar interface and movement footprint. Existing state machines still drive:

- idle and walking cadence
- blink and sniff
- jump and attack-hop movement
- sleep, sit and play-dead
- happy, excited, sad, angry and hunting expressions
- direction mirroring, ear/horn motion and tail animation
- weather and thunder reactions
- companion rendering

Persisted skin indices are compatible and now display as CRIMSON, EMBER, ASH, UNDEAD, RETRO, SHADOW, CANDY, GOLD and DUST demon palettes.

## GNSS

For the Cardputer Mesh Cap:

- host RX: GPIO15, connected to Cap GPS_TX
- host TX: GPIO13, connected to Cap GPS_RX
- default baud: 115200 8N1
- fallback baud: 9600

AUTO mode alternates baud rates until TinyGPSPlus observes a checksum-valid NMEA sentence. UART parsing is bounded to 256 bytes per game loop pass.

The GPS page shows:

- latitude and longitude
- altitude
- UTC and configured local time
- speed
- course and cardinal heading
- satellites
- HDOP
- fix state and age
- current baud

Farm HUD indicator:

- `G+` — fresh fix
- `G~` — searching
- `G-` — GPS disabled

Controls on the GPS page:

- `G` — GPS on/off
- `B` — AUTO / 115200 / 9600
- `L` — CSV logging on/off
- `U` — GPS UTC clock sync on/off
- `-` / `+` — timezone in 15-minute steps

## GPS logging

Logging requires a fresh fix and mounted SD card. Records append every two seconds to:

```text
/0N3P0rK/gps/track.csv
```

Columns:

```text
timestamp,latitude,longitude,altitude_m,satellites,speed_kph,heading_deg,hdop
```

The legacy `/0N3P0rK` root is intentional. It preserves existing SD/save compatibility.

## Safe-build policy

`FR3K_SAFE_BUILD=1` is set on the `m5cardputer-safe` env in
`platformio.ini`.

- attack/radio tools are absent from the safe root menu
- legacy offensive hotkeys are disabled
- legacy offensive action IDs are rejected
- capture, portal and cracking modes are not initialised at boot
- deauthentication, bidirectional kick, EAPOL TX, PMKID probe, CSA herd
  and authentication-flood settings are forced off

Dormant upstream implementation remains in source for provenance and
compatibility but has no safe-build menu or hotkey launch path.

## v3 lab-gate policy (`m5cardputer` env)

The default env compiles the offensive code path but wires it behind a
runtime gate:

- NVS namespace `fr3klab` stores a 0/1 unlock flag and an 8-bit tool
  bitmask.
- The unlock password is `666`. SHA-1 of the typed input must equal
  `cd3f0c85b158c08a2b113464991810cf2cdfc387`. SHA-1 is implemented in
  firmware (`src/lab/lab_unlock.cpp`) using the RFC 3174 / FIPS 180-4
  reference algorithm; the verify script (`tools/lab_unlock_check.py`)
  re-implements the same algorithm in Python and confirms the
  constant matches.
- The gate is honored at three layers: `Menu::doAction` guard,
  `Menu::tryHotkey` guard, and `main.cpp` setup-time init of
  `Cap::begin / EvilPigMode::init / PigpassMode::init`.
- After unlock the operator can toggle each tool individually on
  `SETTINGS > LAB UNLOCK > LIGHT / AGGRO / EVILPIG / PIGPASS / BLE /
  IR / SPECTRUM / LOOT`.
- `LOCK NOW` action clears the unlock flag and zeroes the mask.

## v3 spectrum-sky

A 13-bar 2.4 GHz RSSI histogram is drawn between the sky gradient and
the cloud layer. When `Cap::RunMode::Light` is running the bars reflect
the per-channel RSSI from the sniffer; otherwise a passive
`WiFi.scanNetworks()` poll every 5 s drives them. Toggle via
`SETTINGS > DEMON > SPECTRUM SKY`.

## v3 Australian IR brand DB

Press `V` in `IR PORT` to load the built-in Australian brand DB
(`src/ir/aus_brand_db.h`): 75 entries across 4 protocols (NEC, NEC42,
SAMSUNG, SONY). Carrier locked to 38 kHz / 940 nm, single-shot only.
Generator: `tools/aus_ir_codes.py`. Self-test: `tools/aus_ir_codes_test.py`.

## v3 demon voice

The four `OINK_*` sequences are replaced by five 2-step pitch contours
(`ACK / HEY / NAH / MUM / OOF`) plus a blunt `SFX::CUNT` jingle.
Default voice word is `CUNT` per operator instruction. Switch via
`SETTINGS > DEMON > SOUND WORD`. Toggle the jingle via `CUNT JINGLE`.

## SD/save compatibility

The NVS namespace and existing `/0N3P0rK` SD directories remain unchanged. Existing personality/progression values are retained. A legacy default name of `Pig` or `Lexi` migrates to `Imp`; custom names are preserved.

## Verification and release files

Run:

```sh
python3 scripts/verify_fr3k.py
python3 tools/aus_ir_codes_test.py
python3 tools/lab_unlock_check.py
```

Release packaging includes:

- `fr3k-cardputer-v3.bin` — default env (lab-gated)
- `fr3k-cardputer-v3-safe.bin` — strict-safe env
- `fr3k-cardputer-v3.elf`, `fr3k-cardputer-v3-safe.elf`
- `fr3k-cardputer-v3.map`, `fr3k-cardputer-v3-safe.map`
- `BUILD_NOTES.md`
- `CHANGELOG_fr3k.md`
- `SHA256SUMS`

## Credits and licence

Original game and architecture: lexilexiko / 0N3P0rK.

fR3k fork: demon renderer, GNSS, logging, safe UI and release packaging.

See `LICENSE`. TinyGPSPlus 1.1.0 is included under its LGPL-2.1-or-later terms as stated in its source headers. M5Stack product names belong to M5Stack. This repository is not affiliated with M5Stack.
