# fR3k — Demon Farm + GNSS for M5Cardputer

fR3k 2.0.0-fr3k is a safe game/GNSS firmware derived from lexilexiko's 0N3P0rK. It preserves the Tamagotchi farm, world artwork, movement, progression, weather, seasons, trees, collisions, wolf, props, cards, saves and animation timing while replacing the procedural pig with a procedural demon.

The distributable build does not expose active offensive radio launch paths.

## Target hardware

- M5Stack Cardputer / Cardputer ADV
- ESP32-S3, 8 MB flash
- Cardputer Mesh Kit Cap LoRa-1262 with integrated ATGM336H-6N GNSS
- microSD for optional GPS CSV logging and existing save content

## Build

```sh
/home/parrot/.platformio/penv/bin/pio run -e m5cardputer
```

PlatformIO environment: `m5cardputer`

- Platform: `espressif32@6.12.0`
- Board: `m5stack-stamps3`
- Framework: Arduino
- Output: `.pio/build/m5cardputer/firmware.bin`

See `BUILD_NOTES.md` for the verified toolchain, memory use, image size, SHA-256 and exact hardware facts.

## fR3k interface

The safe root menu contains:

1. DEMON — name, demon palette, season, sky, scene layers and life settings
2. GPS — fix telemetry, UART/baud controls, UTC sync, timezone and CSV logging
3. STATUS — board, battery, SD, GPS, logging, safe-build state and version
4. SYSTEM — brightness, sound and dimming
5. FILES — SD/internal file manager inherited from the upstream architecture
6. CONNECT — benign Wi-Fi configuration/scan surface

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

`FR3K_SAFE_BUILD=1` is set in `platformio.ini`.

- attack/radio tools are absent from the safe root menu
- legacy offensive hotkeys are disabled
- legacy offensive action IDs are rejected
- capture, portal and cracking modes are not initialised at boot
- deauthentication, bidirectional kick, EAPOL TX, PMKID probe, CSA herd and authentication-flood settings are forced off

Dormant upstream implementation remains in source for provenance and compatibility but has no safe-build menu or hotkey launch path.

## SD/save compatibility

The NVS namespace and existing `/0N3P0rK` SD directories remain unchanged. Existing personality/progression values are retained. A legacy default name of `Pig` or `Lexi` migrates to `Imp`; custom names are preserved.

## Verification and release files

Run:

```sh
python3 scripts/verify_fr3k.py
```

Release packaging includes:

- `fr3k-cardputer.bin`
- `firmware.elf`
- `firmware.map`
- complete source ZIP
- upstream patch
- `BUILD_NOTES.md`
- `CHANGELOG_fr3k.md`

## Credits and licence

Original game and architecture: lexilexiko / 0N3P0rK.

fR3k fork: demon renderer, GNSS, logging, safe UI and release packaging.

See `LICENSE`. TinyGPSPlus 1.1.0 is included under its LGPL-2.1-or-later terms as stated in its source headers. M5Stack product names belong to M5Stack. This repository is not affiliated with M5Stack.
