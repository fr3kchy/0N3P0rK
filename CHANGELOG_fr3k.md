# CHANGELOG — fR3k Cardputer 2.0.0-fr3k

## fR3k branding

- Replaced the boot splash speech bubble, root menu title, status title, version identity, USB MSC vendor and credits presentation with fR3k branding.
- Changed the default companion name from `Pig` to `Imp`; existing `Pig`/`Lexi` defaults migrate to `Imp`.
- Preserved `/0N3P0rK` SD paths and the `onelpig` NVS namespace so existing saves, settings and SD data remain compatible.
- Rewrote built-in companion speech as benign fR3k demon/farm/weather dialogue.

## Demon renderer

- Added a procedural, pixel-grid demon renderer that plugs into the existing avatar draw wrapper.
- Added visible horns, pointed ears, tail, expressive eyes, mischievous mouth and palette-based shading.
- Preserved the existing footprint, anchor, direction mirroring, idle/walk cadence, blink, sniff, jump, sleep, mood, play-dead, tail/ear animation and companion drawing paths.
- Preserved weather/thunder reactions: rain lowers the horns and thunder inverts the demon palette.
- Reinterpreted all nine persisted skin indices as demon palettes: CRIMSON, EMBER, ASH, UNDEAD, RETRO, SHADOW, CANDY, GOLD and DUST.
- Kept progression and zombie/undead unlock semantics intact.

## GPS

- Added non-blocking UART/NMEA GNSS support using TinyGPSPlus 1.1.0.
- Targets the Cardputer Mesh Cap GPS connection: host RX GPIO15 and host TX GPIO13.
- Added AUTO detection between 115200 and 9600 baud using checksum-valid NMEA sentences.
- Added fresh/stale fix handling and fix-acquired/fix-lost notifications.
- Added latitude, longitude, altitude, UTC, speed, course, cardinal heading, satellite count, HDOP and fix age.
- Added `G+`/`G~`/`G-` farm HUD indicator.
- Added a GPS details/configuration page with enable, baud, logging, UTC sync and timezone controls.
- Added optional GPS-driven system UTC clock synchronisation and a 15-minute-step timezone offset.

## GPS logging

- Added optional CSV logging to `/0N3P0rK/gps/track.csv`.
- Records timestamp, latitude, longitude, altitude, satellites, speed, heading and HDOP.
- Writes at a bounded two-second interval only with a fresh fix.
- Refuses logging without a current fix or mounted SD card and disables logging on SD write failure.

## UI and diagnostics

- Added fR3k status rows for board, battery/charging, SD, GPS, CSV logging, safe-radio state and version.
- Added low-battery notification with hysteresis.
- Preserved existing brightness, sound and dimming controls.
- Preserved farm art, seasons, weather, trees, wolf, collisions, cards, props, progression, saves and animation timing.

## Radio safety

- Added `FR3K_SAFE_BUILD=1` to the distributable environment.
- Removed attack/radio tools from the safe root menu.
- Disabled legacy action hotkeys in the safe build.
- Added a compile-time-constant guard that rejects offensive action IDs.
- Prevented network capture, portal and cracking modes from initialising at boot.
- Forced legacy deauthentication, bidirectional kick, EAPOL TX, PMKID probe, CSA herd and authentication-flood settings off at configuration load.
- Retained dormant upstream implementation only for provenance/compatibility; it has no safe-build launch path.

## Compatibility notes

- Target environment remains `m5cardputer` with `m5stack-stamps3`, Arduino and `espressif32@6.12.0`.
- SD root and NVS namespace remain unchanged.
- The delivered BIN is an application image produced by PlatformIO; it is not a merged factory image.
- No firmware was flashed and no physical GNSS/SD runtime test was claimed.
