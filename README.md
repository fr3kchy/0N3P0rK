# 0N3P0rK

**v1.2.8** — M5Cardputer / Cardputer ADV firmware: a living pig on the farm, plus a Wi‑Fi lab for handshake capture, spectrum, PigPass crack, BLE toys, IR, loot sync.

Think Tamagotchi first. The radio is in the barn.

### What’s new in 1.2.8
- **Scene split:** `sky` / `ground` / `trees` / FX modules — easier seasons and themes
- **Seasons:** **CITY** (lv 25) and **DESERT** (lv 30) — urban props, palms, cactus, sandstorm
- **Seasonal props** (lv 35+): hive + bees, snowman, sleeping fox, storm campfire, city cat, desert skull — once per game-day, spawn off-screen
- **Friend pig** (lv 40): companion on the farm, own AI, wolf can bite her too · toggle **FRIEND**
- **Cards table** (lv 45): table on the map · jump at table → stub “cards not ready”
- **Credits** (lv 50): unskippable ~10s thank-you roll
- **PigPass:** PCAP / 22000 tabs, larger file list, scene suspend while open
- **Web site:** cleaner public pages · gallery loads images from `docs/gallery/` automatically

*(Secret menu codes are not listed here — set them in your private notes / game.)*

---

## Hardware

| | |
| --- | --- |
| Boards | **M5Cardputer** (original) and **M5Cardputer ADV** |
| MCU | ESP32-S3 (StampS3), 240 MHz |
| Flash | 8 MB — large app partition, **512 KB** internal LittleFS (not used for loot) |
| Screen | 240 × 135 ST7789 |
| SD | SPI: CS 12, MOSI 14, MISO 39, SCK 40 — **all user files on SD** |

Same `.bin` for original and ADV. Full flash erase once when changing partitions from older builds.

---

## Flash

```text
esptool.py --chip esp32s3 --port COMx write_flash 0x0 0N3P0rK_v1.2.8_m5cardputer.bin
```

or PlatformIO:

```text
pio run -t upload --upload-port COMx
```

or **M5Launcher** with a `*Launcher*.bin`.

**Web installer:** [lexilexiko.github.io/0N3P0rK](https://lexilexiko.github.io/0N3P0rK/) — Chrome / Edge / Opera.

Build:

```text
pio run
```

Release names: `0N3P0rK_v1.2.8_*_Full.bin` · `0N3P0rK_v1.2.8_*_M5Launcher.bin`

---

## First minutes

1. FAT32 SD in the slot before boot (loot / talk / wordlists).
2. Farm boots with the pig. **SETTINGS** from the menu.
3. **RADIO** for capture · **PIGPASS** for offline crack · **LOOT** for files on SD.
4. Level the pig: skins, seasons, props, friend, table unlock as you play.

---

## Farm & pig

- Day / night sky, weather, seasons, trees / produce, wolf visitor
- Mood monologues, **TALK SEC** interval, **LIFE** while tools run
- **SCENE** layers, **ANIM TEST** lab, **PROPS** / **FRIEND** toggles (when unlocked)
- XP to **level 50** (flat late-game curve after early levels)

### Unlock roadmap (by level)
| Level | Unlock |
| ---: | --- |
| 5+ | skins / gold apples (as before) |
| 15 / 18 | RETRO / NOIR seasons |
| 25 | **CITY** |
| 30 | **DESERT** |
| 35 | seasonal **props** |
| 40 | **friend** pig |
| 45 | **cards table** (stub) |
| 50 | **credits** |

---

## Radio & tools

- Handshake capture (methods / packs), spectrum, deauth lab options
- PigPass: PCAP + hashcat **22000** tabs, wordlists on SD
- Loot browser, file manager (**SD only**), BLE / IR / USB SD modes as in 1.2.x

---

## SD layout (typical)

```text
/handshakes/   captures
/wordlists/    for PigPass
/talk/         monologue lines (optional)
```

---

## Legal

For education and authorized testing only. You are responsible for how you use the radio tools.

License: see `LICENSE`.

Not affiliated with M5Stack.

---

## Credits

Thanks to the community, **Oct0sec** inspiration on the handshake path, and everyone who tested builds.

**0N3P0rK** — oink responsibly.
