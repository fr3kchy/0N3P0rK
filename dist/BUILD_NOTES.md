# fR3k Cardputer Build Notes (v3.0.4-fr3k-lab)

## Provenance

- Upstream: `https://github.com/lexilexiko/0N3P0rK`
- Upstream branch: `Methodik`
- Upstream base commit: `4985112`
- fR3k version: `3.0.4-fr3k-lab` (default env), `3.0.4-fr3k-safe` (safe env)
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
| `m5cardputer`      | 2,090,528 | 79.8 % | `6fcf418fd095046c3b1c3f3fb827c82aea8a1ac4fa5934ca6c3c468a60ceee04` |
| `m5cardputer-safe` | 2,089,168 | 79.7 % | `9c5fff4b4ee82378febb38e75e54bf231b231feac41a71a1398f81f52183df04` |

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

## v3.0.1 follow-up (UI responsiveness + click sound + demon words)

- New `SFX::playNav()` — queue-bypassing nav tap (50-60 ms two-note
  pair) fired directly via `M5.Speaker.tone()`. The audio task pumps
  the second note in the next `update()` tick. Fixes the perceived
  lag where a visual UI update was trailed by an audible click.
- All 39 `SFX::play(SFX::CLICK)` and `SFX::play(SFX::MENU_CLICK)` call
  sites migrated to `SFX::playNav()`. Dispatcher arms in `sfx.cpp`
  demoted to no-ops for backwards source compat only.
- `applyMuteMask()` no longer calls `stop()`. The IR blast path
  silenced the active sequence only — pending personality events
  fire after the mute clears.
- `SFX::update()`: keep personality events queued through a mute
  window. The previous code drained the audio queue on
  `setMuted(true)`, which dropped any personality event that was
  queued just before the mute toggle.
- New nav-tap variant table `NAV_ACK / NAV_HEY / NAV_NAH / NAV_MUM /
  NAV_OOF / NAV_CUNT` keyed off `Config::personality().voiceWord`.
  Default `voiceWord == VOICE_CUNT` produces the NAV_CUNT tap on
  every UI keypress.
- `scripts/verify_fr3k.py` extended: walks every .cpp/.h in `src/`
  (except `sfx.cpp/.h`) and fails on any lingering
  `SFX::play(SFX::CLICK)` or `SFX::play(SFX::MENU_CLICK)`.

## Build verification (v3.0.1)

- `pio run -e m5cardputer`        : PASS
- `pio run -e m5cardputer-safe`   : PASS
- `python3 scripts/verify_fr3k.py`: PASS (incl. new click-call-site
  migration gate)
- `python3 tools/aus_ir_codes_test.py` : PASS
- `python3 tools/lab_unlock_check.py`  : PASS
- `git diff --check`             : PASS

## v3.0.2 follow-up (per-screen audio gate)

- New `SFX::setMenuMode(bool)` and `SFX::setMinimalTap(bool)` runtime
  API. When `setMenuMode(true)` is in effect (i.e. operator is in MENU
  or ATTACK state), every audio entry point short-circuits:
  - `SFX::play()` returns early
  - `SFX::playPersonality()` returns early
  - `SFX::playCuntJingle()` returns early
  - `SFX::playNav()` returns early (unless `s_minimalTap` is on)
  - `SFX::update()` skips the sequence pump + the nav-tap pump
- `App::setMode()` calls `SFX::setMenuMode(m != AppMode::FARM)` so
  every state transition drives the gate from the current mode.
  FARM is the only non-menu working screen; MENU and ATTACK are
  both root-menu shapes in v3. Personality voice still plays on FARM
  and in every in-game mode (IR, spectrum, GPS, etc).
- New `MENU SOUND` settings row in the SCENE page (default OFF).
  When OFF: silent menu. When ON: plays the configured demon word
  on every nav. Operator-controlled.
- New `MENU TAP` settings row (default OFF). When MENU SOUND is OFF,
  optionally still play a 30 ms single piezo blip on every keypress.
- New `PersonalityConfig::menuSound` and `PersonalityConfig::menuMinimalTap`
  fields, persisted to NVS (`menusnd` / `menutap` keys).
- `SFX::init()` seeds the new `s_inMenu` and `s_minimalTap` static
  state to false so the gate is well-defined at boot.

## v3.0.3 follow-up (spectrum-sky removed)

- The 13-bar RSSI histogram overlay that drew between the sky gradient
  and the cloud layer is no longer rendered. `main.cpp` no longer
  calls `SpectrumSky::begin()`. `src/piglet/avatar.cpp` no longer
  calls `SpectrumSky::drawBackground()`. The source in
  `src/piglet/spectrum_sky.{h,cpp}` is preserved for a future
  re-enable (a one-line change in the avatar draw path).
- `PersonalityConfig::spectrumSky` is forced to `false` on every
  load. The settings row id 23 (SPECTRUM SKY) is hard-coded to 0 in
  `getValue()` and toasts "SPECTRUM SKY OFF (v3.0.3)" if the
  operator toggles it. The row is preserved in the UI so a future
  re-enable can recover the value.
- `src/cap/sniffer.cpp` and `src/cap/sniffer.h` keep the per-channel
  RSSI tracking infrastructure (`getRssi13()` / `rssiForChannel()` /
  `noteRssi()`) — the sniffer still fills the table; the sky is
  just no longer reading it.
- `verify_fr3k.py` extended with a `has_call()` helper that ignores
  `//` comment lines, then asserts no live call sites for
  `SpectrumSky::begin()`, `SpectrumSky::drawBackground()`, or
  `SpectrumSky::setEnabled()`.

## Build verification (v3.0.2 / v3.0.3)

- `pio run -e m5cardputer`        : PASS
- `pio run -e m5cardputer-safe`   : PASS
- `python3 scripts/verify_fr3k.py`: PASS (incl. spectrum-sky
  removal gate, per-screen audio gate, click-call-site gate)
- `python3 tools/aus_ir_codes_test.py` : PASS
- `python3 tools/lab_unlock_check.py`  : PASS
- `git diff --check`             : PASS