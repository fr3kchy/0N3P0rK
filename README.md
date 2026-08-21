# 0N3P0rK

Tamagotchi pig living on a tiny farm, plus a barn of lab tools, for **M5Cardputer** and **M5Cardputer ADV**.

One firmware image. The board is detected with `M5.getBoard()` only — GPIO 8/9 are never probed (on ADV those pins are the keyboard).

**v1.2** · public · MIT · [lexilexiko](https://github.com/lexilexiko)

The on-device UI is **English ASCII** (Font0 6×8). No Cyrillic on the screen.

> Radio modes are a **lab tool for networks you own or are explicitly authorized to test**. Capabilities are not permission.

---

## What it is

Plug in the Cardputer, put an SD card in the slot, and you get a pig on a scrolling farm. She walks, eats, talks, levels up, and can turn into a zombie if you let her starve (or a wolf gets her). Behind the farm is a barn menu: sniff, capture, offline crack, BLE toys, IR, spectrum, loot sync.

Think Tamagotchi first. The radio is in the barn.

---

## Hardware we run on

| | |
| --- | --- |
| Boards | **M5Cardputer** (original) and **M5Cardputer ADV** |
| MCU | ESP32-S3 (StampS3), 240 MHz |
| Flash | 8 MB (this image) |
| Screen | 240 × 135 ST7789, farm uses a top bar + main field + bottom bar |
| Keyboard | Original: 74HC138 matrix. ADV: TCA8418 |
| USB | CDC serial (`ARDUINO_USB_MODE=0`, CDC on boot). Pick the COM yourself (VID `303A`) |
| SD | dedicated SPI: CS 12, MOSI 14, MISO 39, SCK 40. GPIO 5 is held HIGH only while the card mounts (1.2-style fix), then keys are restored |

**Original Cardputer:** after SD, the keyboard matrix is started again with `Keyboard.begin()`.

**Cardputer ADV:** GPIO 5 stays HIGH after mount. The firmware does **not** re-init the TCA8418 I2C keyboard.

Same `.bin` for both. Do not flash the wrong chip family — this is StampS3 / Cardputer only.

---

## How to flash

1. USB into the Cardputer. In Device Manager (Windows) pick **your** COM port (VID `303A`). The firmware does not hard-code a port.
2. Put a FAT32 SD card in the slot **before** you boot, if you want loot / talk / wordlists.
3. Flash with any of:

```text
esptool.py --chip esp32s3 --port COMx write_flash 0x0 0N3P0rK_v1.2_m5cardputer.bin
```

or PlatformIO:

```text
pio run -t upload --upload-port COMx
```

or M5Launcher / the usual Cardputer launcher, pointing at the release `.bin`.

Build from source (PlatformIO, `espressif32@6.12.0`, Arduino):

```text
pio run
```

Artifact: `.pio/build/m5cardputer/firmware.bin`  
Release name: `0N3P0rK_v1.2_m5cardputer.bin`

---

## First minutes (how to use)

1. Power on. A short splash: the pig runs, speech bubble **0N3P0rK**. Any key skips it.
2. You land on the **farm**. She walks if **LIFE** is on (default).
3. `` ` `` / `~` / Esc opens the **barn menu**. Same key goes back. **Backspace minimizes**, it does not exit.
4. `;` / `.` move up / down in lists. **Enter** selects.
5. Hotkeys from the farm (defaults, remappable in **SET → KEYS**):

| Key | Opens |
| --- | --- |
| `R` | RADIO (last radio page) |
| `A` | AGGRO |
| `L` | LIGHT |
| `P` | PIGPASS |
| `E` | EVILPIG |
| `B` | BLE |
| `I` | IR PORT |
| `S` | SPECTRUM |
| `H` | LOOT |

---

## Farm

The pig lives here. Top bar: hearts, food apple, level, sky/season, battery. Bottom bar: dirt fringe + name.

### Move her

| Key | Action |
| --- | --- |
| `,` hold | walk left |
| `/` hold | walk right |
| `;` | jump (airborne stomp on a tree/bush) |
| Space | attack hop |
| `.` hold | sit |

**LIFE ON** (PIG settings): she walks, jumps, hides on her own. **LIFE OFF**: you steer.

**ANIM TEST** (PIG): `-` / `=` cycles demo poses on the farm.

### Hunger, hearts, zombie

- Hunger ticks down. When it hits empty, she loses a **heart**.
- Eat fruit / berries from the farm to fill hunger. A full stomach can add a heart (up to 5).
- **0 hearts** (wolf **or** hunger) → she becomes a **zombie**. No countdown toast — it is a surprise.
- **5 hearts** again → she turns back into a normal pig by herself ("PIG AGAIN").
- You may change skin by hand only if she has **at least 1 heart**. Locked skins are skipped in the cycle.

A leftover empty stomach used to eat the new heart instantly. After a zombify / a new heart, hunger is buffered so she does not flop straight back.

### Wolf

A night visitor. If **WOLF EAT** is on, a bite at 0 hearts can stash a random handshake into `/0N3P0rK/wolf/` (never keys). Hit the wolf or turn Am off to get loot back.

### Trees and bushes

Always three plants, each on its own lane so they do not grow out of one spot:

| Lane | What | Drops |
| --- | --- | --- |
| Left | fruit tree (changes with the season) | apples / cherries / cones |
| Center | decorative (willow in spring, street lamp in NOIR, snowy tree in winter — never a second fir) | none |
| Right | berry bush | berries, every season |

They scroll with the grass. Jump on one three times to knock it down; decor and the bush grow back. The bush does **not** collapse by itself — only a kick. Berries (and fruit) fall, then grow again, then fall again.

### Seasons and sky

**PIG → SEASON** or AUTO (AUTO rotates spring → summer → autumn → winter every 15 minutes; RETRO and NOIR stay manual).

| Season | Farm look |
| --- | --- |
| SPRING | sakura (left), weeping willow (center), bush (right), fresh grass |
| SUMMER | apple tree, classic tree, bush |
| AUTUMN | old apple tree, fall colors, leaves |
| WINTER | fir (left), snowy classic (center), frost |
| RETRO | silver-screen black and white (unlock) |
| NOIR | night alley, stars, sodium lamp, moths (unlock) |

**SKY:** AUTO (clock / a living cycle), DAY, or NIGHT. NOIR is always night.

### XP and secrets

XP is a Tamagotchi grind. Each level costs more than the last:

| Level-up | XP needed |
| ---: | ---: |
| 1 → 2 | 100 |
| 2 → 3 | 250 |
| 3 → 4 | 500 |
| 4 → 5 | 1000 |
| 5 → 6 | 2500 |
| 6 → 7 | 5000 |
| 7 → 8 | 7500 |
| after that | +2500 each time |

You earn XP by picking fruit, scaring the wolf, care (pet / feed, rate-limited), handshakes, PIGPASS, EVILPIG, night survive, and so on.

| Unlock | At |
| --- | --- |
| BLUSH skin | lv 5 |
| SHADOW skin | lv 8 |
| Gold apples on the fruit tree | lv 10 |
| CANDY skin | lv 12 |
| RETRO skin + RETRO season | lv 15 |
| NOIR season | lv 18 |
| GOLD skin | lv 20 |

**PIG → CODE** — type `l3xik0` to unlock every skin and season at once.

Skins: CLASSIC, BLUSH, HOG, ZOMBIE, RETRO, SHADOW, CANDY, GOLD.

She talks in a bubble (Font0). Drop your own lines in `/0N3P0rK/talk/` (see SD below).

---

## Barn menu

`` ` `` from the farm. Four roots: **ATTACK**, **LOOT**, **PIG**, **SET**.

### ATTACK

| Item | What it does |
| --- | --- |
| LIGHT | Quiet sniff on the **current** channel. Incoming rings on the snout. UI stays calm. |
| AGGRO | Hop channels 1–13, kick, catch EAPOL / PMKID. Outgoing rings. SSID hunt. |
| EVILPIG | Lab portal: clone **your own** net, loot, kick. `ENT` clone, `V` loot, `D` kick. |
| PIGPASS | Offline WPA from a wordlist or mask on the SD. Handshakes in `/handshakes/`, lists in `/Passworld/`. |
| BLE | Apple / Win / Android frames. Own devices. `;` / `.` family. |
| IR PORT | Point at a TV. `SPC` fire, `R` NA/EU, `E` file from `/0N3P0rK/ir`. |
| SPECTRUM | 2.4 GHz sweep, lobes, waterfall. `ENT` lock, `SPC` kick, `W` wake. |
| STOP | Radio sleep. Loot on the card stays. |

### LOOT

One bag for **wpa-sec** and **pwncrack.org**. `,` / `.` switch tab. `S` sync all pending, `U` upload the selected file only, `T` test (scroll with `;` / `.`). Bottom bar cycles the key hints.

Put API keys in **SET →** the matching key fields (they live in net NVS, not `pig.cfg`).

### PIG

Name, skin, season, sky, LIFE, scene layers (trees / grass / wolf / weather…), ANIM TEST, **CODE**, WOLF / WOLF EAT.

### SET

1. **SYSTEM** — brightness, sound, dim after / dim level  
2. **STATUS** — LVL, XP bar, board, battery, SD, Wi-Fi, WPA/PWN keys present, version. `;` / `.` scroll  
3. **RADIO** — pack (STOCK / OURS / PAN), hop, lock, deauth, RSSI, MAC, PMKID, CSA, pcap…  
4. **BLE** — burst / adv time  
5. **CONNECT** — home Wi-Fi for sync (scan, type **only the password**)  
6. **KEYS** — farm / menu shortcuts. `ENT` to bind, Backspace to clear  
7. **USB SD** — the SD card as a disk on the PC. Eject on the PC, then `` ` ``

---

## SD card

Root folder is always `/0N3P0rK/`. Created on boot if missing.

```
/0N3P0rK
  handshakes/     pcap + 22000 captures
  wpa-sec/        key.txt  results.txt  uploaded.txt
  pwncrack/       key.txt  results.txt  uploaded.txt
  evilpig/        creds.csv
  pigpass/        cracked.txt  checkpoint
  Passworld/      wordlists for PIGPASS
  ir/             IR files
  wolf/           what the wolf stole (handshakes, never keys)
  talk/           custom pig lines
```

**talk** — one file per mood, one line = one bubble, `#` starts a comment, keep lines around 24 characters:

`idle.txt` `happy.txt` `hungry.txt` `sad.txt` `sleepy.txt` `fed.txt` `pet.txt` `play.txt` `bird.txt`

---

## Keys (global)

| Key | Action |
| --- | --- |
| `` ` `` / `~` / Esc (code 27) | Back / leave the mode |
| Backspace | Minimize the window, **not** exit |
| `;` / `.` | Up / down in lists (STATUS too) |
| Enter | Select |
| `,` / `/` | Walk on the farm |

Change binds in **SET → KEYS**.

---

## Build notes

- PlatformIO env `m5cardputer`, board `m5stack-stamps3`
- `espressif32@6.12.0`, Arduino framework
- Partition table: `partitions.csv`
- USB: `-UARDUINO_USB_MODE` then `-DARDUINO_USB_MODE=0`, `ARDUINO_USB_CDC_ON_BOOT=1`
- Version string is injected by `scripts/pre_build.py` from `custom_version` in `platformio.ini`

---

## v1.2

- LOOT opens fast (no SD compact / extra file reads on every enter)
- WPA-SEC and PwnCrack upload every pending file, up to 8 MB each, with a live bar
- `D` deletes the selected capture; list columns stay put
- Compact no longer eats `.pcap` when a `.22000` exists for the same AP

## v1.1

- Farm flora scattered on a longer loop; smash here, grow off-screen
- Winter snow banks: cloud-like puffs, spawn while snowing, trampled path stays
- EvilPig / PigPass: cycling key hints, marquee for long names
- LOOT: `U` sends one handshake; `T` test log scrolls

---

## License

MIT. Copyright (c) 2026 lexilexiko.

Inspired by M5PORKCHOP (0ct0) and the OnePork farm package. Those projects remain their authors'. **0N3P0rK** is a separate work.
