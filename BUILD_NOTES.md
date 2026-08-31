# fR3k Cardputer Build Notes

## Provenance

- Upstream: `https://github.com/lexilexiko/0N3P0rK`
- Upstream branch: `Methodik`
- Upstream commit: `e77278731283dc3147ff5a79b271d62866f2720f`
- fR3k version: `2.0.0-fr3k`
- Build profile: safe game/GNSS distributable (`FR3K_SAFE_BUILD=1`)

## Toolchain

- PlatformIO Core: `6.1.19`
- Platform: `espressif32@6.12.0`
- Environment: `m5cardputer`
- Board definition: `m5stack-stamps3`
- Target MCU: ESP32-S3, 240 MHz, 8 MB flash
- Framework: Arduino
- Arduino-ESP32: `2.0.17` (`framework-arduinoespressif32 3.20017.241212`)
- ESP-IDF base: `v4.4.7`
- TinyGPSPlus: `1.1.0`, vendored under `lib/TinyGPSPlus`

## GNSS hardware

This build targets the M5Stack Cardputer Mesh Kit's Cap LoRa-1262 / ATGM336H-6N GNSS interface, not the Cardputer's HY2.0/Grove port.

- Cardputer ADV UART RX: `GPIO15`, connected to Cap `GPS_TX`
- Cardputer ADV UART TX: `GPIO13`, connected to Cap `GPS_RX`
- UART format: `8N1`
- Default/fast baud: `115200`
- Fallback baud: `9600`
- AUTO mode: starts at 115200 and alternates with 9600 every four seconds until a checksum-valid NMEA sentence is observed

M5Stack's official Mesh Kit documentation identifies G13/G15 as the Cap GPS UART and states that the integrated GNSS defaults to 115200 bps at 8N1.[1] The Cardputer-Adv is an ESP32-S3FN8 device with 8 MB flash, a 240×135 display, microSD, a 1750 mAh battery and the EXT 2.54-14P bus.[2] M5Stack's Arduino GNSS example also uses TinyGPSPlus-style UART parsing at 115200 bps, while documenting that module-specific pins must be selected for the attached hardware.[3]

## Build

Exact command:

```sh
/home/parrot/.platformio/penv/bin/pio run -e m5cardputer
```

Result: `SUCCESS`

- PlatformIO reported RAM: `187,168 / 327,680 bytes` (`57.1%`)
- PlatformIO reported application flash: `2,077,177 / 2,621,440 bytes` (`79.2%`)
- App image size: `2,077,536 bytes`
- Image: ESP32-S3, 8 MB header, 80 MHz, DIO, checksum valid, validation hash valid
- `fr3k-cardputer.bin` SHA-256: `9ac1bcde58bf1ce5f6da891bb47bd89f140362dff06398a4919230e355684f1f`

Clean upstream baseline at the same commit built successfully with RAM `186,640 bytes` and application flash `2,054,929 bytes`. fR3k therefore adds 528 bytes of static RAM and 22,248 bytes of application flash.

Compiler output contained no source-code diagnostics. The build emitted the upstream board-definition warning that `ARDUINO_USB_MODE` is redefined, plus deprecation warnings from PlatformIO invoking legacy esptool option spellings. These warnings did not affect image validation.

## GNSS behaviour

- The loop pumps at most 256 UART bytes per pass.
- A fix is stale after five seconds.
- HUD states: `G+` fix, `G~` searching, `G-` disabled.
- Details page exposes latitude, longitude, altitude, speed, course/cardinal heading, satellites, HDOP, fix age, UTC, local time and active baud.
- GPS UTC synchronisation is optional and refreshes at most hourly.
- Timezone is configurable in 15-minute increments.
- CSV logging requires both a fresh fix and mounted SD storage.
- Logging interval: two seconds.
- Compatibility-preserving path: `/0N3P0rK/gps/track.csv`.

## Safe-build controls

- Network/capture/portal modes are not initialised at boot.
- The safe root menu contains only DEMON, GPS, STATUS, SYSTEM, FILES and CONNECT.
- Offensive radio hotkeys return disabled in the safe build.
- Any legacy offensive action ID is rejected with `DISABLED: SAFE BUILD`.
- Legacy radio TX settings are forced false when configuration loads.
- Dormant upstream radio implementation remains in source for provenance/compatibility but has no active safe-build menu or hotkey launch path.

## Verification performed

- Clean upstream baseline build: PASS
- Final PlatformIO build: PASS
- Source release gates (`scripts/verify_fr3k.py`): PASS
- `git diff --check`: PASS
- ESP32-S3 image checksum and validation hash: PASS
- ELF symbols found: `DemonRenderer::draw`, `GpsService::loop`, `GpsService::snapshot`, `Storage::appendGpsCsv`
- Legacy `drawPixelPigDetailed` symbol absent from the linked ELF
- Binary strings found: fR3k branding, GPS fix/loss notifications, GPS logging controls and `RADIO TX OFF`
- Legacy `/0N3P0rK` storage strings intentionally retained for save/SD compatibility
- Physical GNSS fix, SD append and on-device display behaviour: not hardware-tested in this build session

## Sources

[1] https://docs.m5stack.com/en/core/Cardputer_Mesh_Kit
[2] https://docs.m5stack.com/en/core/Cardputer-Adv
[3] https://docs.m5stack.com/en/arduino/projects/module/module_gps_v2.0
