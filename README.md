# 0N3P0rK

Tamagotchi pig + barn on **M5Cardputer**.

The pig is a pet. Capture / HASHES / PWNCRACK are a toolbox. They do not puppeteer her.

| | |
|--|--|
| **Version** | **0.1.0** |
| **Board** | M5Cardputer (ESP32-S3, 8 MB) |
| **Storage** | SD card, OnePork layout |

**Legal:** use only on networks you own or have written permission to test.

## Flash

```
pio run -e m5cardputer
pio run -e m5cardputer -t upload
```

## Farm (same as OnePork)

| Key | Action |
|-----|--------|
| `,` / `/` | walk hold |
| `;` | jump |
| `.` | sit hold |
| Space | attack hop |
| F | feed |
| P | pet |
| ` | menu |
| G0 | screen off |

## Menu

`;` / `.` move, Enter select, `` ` `` back.

- **ATTACK** — light / aggressive / stop
- **LOOT** — one screen, tabs **WPASEC** / **PWNCRACK** (`,` `/`). **S** sync, **T** test, Enter detail
- **WIFI** — home SSID/pass for sync (no web AP)
- **PIG** — name, skin, season, sky, wolf, trees, bright, sound, feed

## SD card

```
/loot/wpa-sec/wpasec_key.txt
/loot/wpa-sec/wpasec_results.txt
/loot/wpa-sec/wpasec_uploaded.txt
/loot/wpa-sec/*.pcap
/loot/pwncrack/key.txt
/loot/pwncrack/results.txt
/loot/pwncrack/uploaded.txt
/loot/pwncrack/*.22000
```

Drop API keys in those folders. Home WiFi is in WIFI menu.

No web UI. Keys are not entered in a browser.
