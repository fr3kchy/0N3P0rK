# 0N3P0rK

## https://lexilexiko.github.io/0N3P0rK/

Tamagotchi pig living on a tiny farm, plus a barn of lab tools, for **M5Cardputer** and **M5Cardputer ADV**.

One firmware image. The board is detected with `M5.getBoard()` only — GPIO 8/9 are never probed (on ADV those pins are the keyboard).

**v1.2.7** · public · MIT · [lexilexiko](https://github.com/lexilexiko)

The on-device UI is **English ASCII** (Font0 6×8). No Cyrillic on the screen.

> Radio modes are a **lab tool for networks you own or are explicitly authorized to test**. Capabilities are not permission.

---

## What it is

Plug in the Cardputer, put an SD card in the slot, and you get a pig on a scrolling farm. She walks, eats, talks, levels up, and can turn into a zombie if you let her starve (or a wolf gets her). Behind the farm is a barn menu: sniff, capture, offline crack, BLE toys, IR, spectrum, loot sync.

Think Tamagotchi first. The radio is in the barn.

**v1.2.7** keeps the handshake-first radio from 1.2.5.x, tightens **SD-only** storage (internal flash is no longer a user filesystem), shrinks the unused LittleFS partition, refreshes pig monologues, and polishes the **web installer** site.

---

## Hardware we run on

| | |
| --- | --- |
| Boards | **M5Cardputer** (original) and **M5Cardputer ADV** |
| MCU | ESP32-S3 (StampS3), 240 MHz |
| Flash | 8 MB — large app partition, **512 KB** internal LittleFS (not used for loot) |
| Screen | 240 × 135 ST7789, farm uses a top bar + main field + bottom bar |
| Keyboard | Original: 74HC138 matrix. ADV: TCA8418 |
| USB | CDC serial (`ARDUINO_USB_MODE=0`, CDC on boot). Pick the COM yourself (VID `303A`) |
| SD | dedicated SPI: CS 12, MOSI 14, MISO 39, SCK 40. GPIO 5 is held HIGH only while the card mounts, then keys are restored |

**Original Cardputer:** after SD, the keyboard matrix is started again with `Keyboard.begin()`.

**Cardputer ADV:** GPIO 5 stays HIGH after mount. The firmware does **not** re-init the TCA8418 I2C keyboard.

Same `.bin` for both. Do not flash the wrong chip family — this is StampS3 / Cardputer only.

**Partitions (v1.2.7):** app ~6.8 MB · internal `spiffs`/LittleFS **512 KB** · coredump reserved. All handshakes, talk, wordlists, and the file manager live on **SD**. After changing partitions, do a **full flash erase** once when upgrading from older builds.

---

## How to flash

1. USB into the Cardputer. In Device Manager (Windows) pick **your** COM port (VID `303A`). The firmware does not hard-code a port.
2. Put a FAT32 SD card in the slot **before** you boot, if you want loot / talk / wordlists.
3. Flash with any of:

```text
esptool.py --chip esp32s3 --port COMx write_flash 0x0 0N3P0rK_v1.2.7_m5cardputer.bin
```

or PlatformIO:

```text
pio run -t upload --upload-port COMx
```

or **M5Launcher** / the usual Cardputer launcher, pointing at a `*Launcher*.bin` release.

**Web installer:** [lexilexiko.github.io/0N3P0rK](https://lexilexiko.github.io/0N3P0rK/) — Chrome / Edge / Opera, lists every `.bin` under `docs/firmware/` by **extension** (Direct / Full vs Launcher labels).

Build from source (PlatformIO, `espressif32@6.12.0`, Arduino):

```text
pio run
```

Artifact: `.pio/build/m5cardputer/firmware.bin`  
Release names: `0N3P0rK_v1.2.7_*_Full.bin` (USB / web) · `0N3P0rK_v1.2.7_*_M5Launcher.bin` (launcher)

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
| `F` | FILES |

---

## Farm

The pig lives here. Top bar: hearts, food apple, level, sky/season, battery. Bottom bar: dirt fringe + status.

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
- **0 hearts** (wolf **or** hunger) → she becomes a **zombie**.
- **5 hearts** again → she turns back into a normal pig by herself ("PIG AGAIN").
- You may change skin by hand only if she has **at least 1 heart**.

### Wolf

A night visitor. If **WOLF EAT** is on, a bite at 0 hearts can stash a random handshake into `/0N3P0rK/wolf/` (never keys).

### Trees and bushes

| Lane | What | Drops |
| --- | --- | --- |
| Left | fruit tree (changes with the season) | apples / cherries / cones |
| Center | decorative | none |
| Right | berry bush | berries |

### Seasons and sky

**PIG → SEASON** or AUTO (spring → summer → autumn → winter; RETRO and NOIR stay manual).

| Season | Farm look |
| --- | --- |
| SPRING | sakura, willow, bush |
| SUMMER | apple tree, classic tree, bush |
| AUTUMN | fall colors, leaves |
| WINTER | fir, snow, frost |
| RETRO | black and white (unlock) |
| NOIR | night alley, stars, lamp (unlock) |

**SKY:** AUTO, DAY, or NIGHT. NOIR is always night.

### XP and unlocks

| Unlock | At |
| --- | --- |
| BLUSH skin | lv 5 |
| SHADOW skin | lv 8 |
| Gold apples | lv 10 |
| CANDY skin | lv 12 |
| RETRO skin + season | lv 15 |
| NOIR season | lv 18 |
| GOLD skin | lv 20 |

**PIG → CODE** — type `l3xik0` to unlock every skin and season.

Skins: CLASSIC, BLUSH, HOG, ZOMBIE, RETRO, SHADOW, CANDY, GOLD.

### Talk (monologues)

Snout bubbles: short English lines (hacker / game flavor). Built-in lists + optional SD extras.

| | |
| --- | --- |
| **PIG → TALK SEC** | Interval **2…10** seconds between automatic lines (default **5**) |
| **PIG → MOOD** | Speech bubbles on / off |
| SD path | `/0N3P0rK/talk/` — `idle.txt`, `happy.txt`, `hungry.txt`, `sad.txt`, `sleepy.txt`, `fed.txt`, `pet.txt`, `play.txt`, `bird.txt` |

One line = one bubble. Max ~24 characters. Lines starting with `#` are comments. Up to 16 extra lines per mood file.

---

## Barn menu

`` ` `` from the farm. Roots: **ATTACK**, **LOOT**, **PIG**, **SET**.

### ATTACK

| Item | What it does |
| --- | --- |
| LIGHT | Quiet sniff on the **current** channel. No hop, no deauth. |
| AGGRO | Hop 1–13, kick via **method + pack**, catch EAPOL / PMKID. |
| EVILPIG | Lab portal on **your own** net. |
| PIGPASS | Offline WPA from wordlist / mask on SD. |
| BLE | Apple / Win / Android frames. |
| IR PORT | IR blast; files in `/0N3P0rK/ir`. |
| SPECTRUM | 2.4 GHz sweep. `ENT` lock, `A` hunt, **`D` HS depth**. |
| FILES | Browse **SD only**. **`X`** delete (Y confirm). |
| STOP | Radio sleep. |

### Capture: LIGHT vs AGGRO

| | LIGHT | AGGRO |
| --- | --- | --- |
| Hop | No | Yes |
| Deauth | Off | On if DEAUTH enabled |
| **Z** session skip | Yes | Yes |
| Bar tag | `L` | `A` (`*` = locked) |

**Z** skips the current focus until the next LIGHT/AGGRO start (lock → kick → bar SSID → last HS → pin). Toast `SKIP <name>` or `SKIP NONE`.

### Bottom bar (radio live)

**Left** — real focus SSID only (never MAC). Else `SCAN#ch`.

**Right** example: `A* F/F &3 #06`

| Piece | Meaning |
| --- | --- |
| `A`/`L`/`P` | Aggressive / Light / Pinned |
| `*` | Lock after EAPOL |
| pack/method letters | Active pack & method |
| **`&N`** | Unique networks with saved HS/PMKID |
| `#ch` | Channel |

### LOOT

wpa-sec + pwncrack. `S` sync all, `U` one file, `Q` results only, `D` delete, `R` reload.

### PIG

Name, skin, season, sky, LIFE, layers, **MOOD**, **TALK SEC**, CODE, WOLF.

### SET

SYSTEM · STATUS · **RADIO** · BLE · CONNECT · KEYS · USB SD

---

## RADIO

Packs apply a preset of knobs + optional default method. Manual tweaks become **CUSTOM**.

Menu order (top): **PACK** → **HS METHOD** → **RESET** (stock radio, next to the method) → FALLBACK → kick / hop knobs…

### Packs (examples)

| Pack | Typical method | Idea |
| --- | --- | --- |
| STOCK | (keep) | Factory knobs |
| SOFT | ALL | Light pressure |
| NORMAL | CLIENTS | Prefer APs with stations |
| FOCUS | FOCUS | Score-and-stick (Porkchop-style) |
| LOUD | CLIENTS | Harder CLIENTS pressure |
| MAX | FOCUS | Loud / fast |
| QUIET | (keep) | PMKID probe, minimal TX |

### Methods

| Method | Behavior |
| --- | --- |
| ALL | Kick eligible APs on channel |
| CLIENTS | Prefer APs with stations |
| FOCUS | Score one AP (RSSI, clients, data activity); lock |
| HERD / CSA | Channel-switch style where enabled |
| AUTO | Rotate methods on FALLBACK timer |

### Key knobs

| Setting | Role |
| --- | --- |
| **RESET** | Restore STOCK radio (next to HS METHOD) |
| **HS DEPTH** | PAIR / +M3 / FULL — when lock may release |
| **DEPTH HOLD** | Extra hold after pair if depth > PAIR |
| **DATA ACT** | FOCUS prefers live data, not beacon-only |
| **STRICT LK** | No retarget while locked |
| **LOCK MS** | Park time after EAPOL |
| **ATK RSSI** | Ignore weaker APs |
| **HOP MS** / hop set | Dwell & channel set |
| **DEAUTH** | Master kick enable |
| **PMKID** probe | Auth/assoc without deauth |
| BIDIR / EAPOL TX / CSA / AUTH FLOOD | Kick / coax stack |
| JITTER / COOLDOWN / SCORE THR | Anti-WIDS gap, per-AP cool-down, FOCUS threshold |

**Capture files on SD:** `.pcap` / `.cap` frames + hashcat **`.22000`** (`WPA*01` PMKID, `WPA*02` EAPOL). Writers avoid wiping good files when possible; prefer real ESSID for crackable lines.

---

## SPECTRUM

| Key | Action |
| --- | --- |
| `;` / `.` | Select |
| `F` | Filter ALL / VULN / SOFT / **HID** |
| `ENT` | Lock |
| `A` | Hunt (pinned Cap) |
| `SPC` | Kick (LOCK) |
| `W` | Wake clients |
| **`D`** | HS depth: PAIR → +M3 → FULL |
| `` ` `` | Back |

Hidden SSIDs show as `<HIDDEN>` until a probe response reveals the name. Hunt uses BSSID either way.

---

## PIGPASS (offline crack)

Loads capture + wordlist / mask from SD.

| Captures | Wordlists |
| --- | --- |
| `.22000` (hashcat 22000) | `.txt` / `.lst` / `.dict` in `/0N3P0rK/Passworld/` |
| `.pcap` / `.cap` | On-device **MASK GEN** |

WPA / WPA2 PSK (PBKDF2). Password length 8…63. Results under `/0N3P0rK/pigpass/`. Checkpoint can resume after reboot.

---

## FILES

SD card only (v1.2.7). Internal MEM is not offered.

| Key | Action |
| --- | --- |
| `;` / `.` | Navigate |
| Enter | Open |
| `,` | Parent |
| `V` | Reminds **SD CARD ONLY** (no internal volume) |
| `N` | New file |
| **`X`** then **Y** | Delete file |

---

## SD layout

```
/0N3P0rK
  handshakes/     pcap + 22000 / pmkid
  wpa-sec/        key.txt  results.txt  uploaded.txt
  pwncrack/       key.txt  results.txt  uploaded.txt
  evilpig/        creds.csv
  pigpass/        cracked.txt
  Passworld/      wordlists
  ir/             IR files
  wolf/           wolf stash (handshakes only)
  talk/           pig monologue lines
```

---

## Web site (docs)

GitHub Pages: **Information · Installation · Gallery · Donate**.

- Firmware list by **`.bin` extension** (directory listing and/or GitHub API on the default branch). Optional `firmware/files.json` if the host has no listing.
- Labels **[Direct / Full]** vs **[Launcher]**.
- **README** link uses the repo default branch (`…/0N3P0rK#readme`), not a hard-coded `main` tree.
- **Donate** button placeholder at the bottom (link later).

---

## Build notes

- PlatformIO env `m5cardputer`, board `m5stack-stamps3`
- `espressif32@6.12.0`, Arduino framework
- `board_build.partitions = partitions.csv` · `board_build.filesystem = littlefs` (512 KB slot; user data on SD)
- Version from `scripts/pre_build.py` + `custom_version` in `platformio.ini` → **1.2.7**

---

## Release notes

### v1.2.7

Cosmetics, storage layout, and site polish on the 1.2.5.x capture base.

**Pig & UI**
- New English monologues (tech / game flavor: wifi tail, Diablo snout, handshake jokes, …)
- **PIG → TALK SEC** — monologue interval **2…10 s** (default 5); bubble duration follows the interval
- **RADIO → RESET** moved next to **HS METHOD** (easy to find)

**Storage & partitions**
- Internal LittleFS / `spiffs` partition reduced to **512 KB** (was ~5 MB); app partition enlarged
- Loot / handshakes / talk / wordlists remain on **SD only**
- **FILES** no longer switches to internal MEM (`V` → SD CARD ONLY)

**Web installer (`docs/`)**
- Full English site: Information, Installation, Gallery, Donate
- Discovers firmware by **`.bin` format**; Direct/Full vs Launcher labels
- README button targets the **default branch** documentation

**Still from 1.2.5.6 (capture core)**
- Packs & methods, FOCUS scoring, DATA ACT / STRICT LK / DEPTH HOLD / HS DEPTH
- Session **Z** skip, SSID status bar, **`&N`** handshake count
- Spectrum **`D`** depth + HID / `<HIDDEN>`
- Safer pcap / `.22000` write habits

**Parked for later**
- ROAD street mode, TX power knob, MP3 player, donate payment URL

---

### v1.2.5.6

Handshake-first radio pass: catch more real handshakes, clearer status, skip stuck targets, fewer junk files on SD.

**Capture & methods**
- Pack presets wire method + radio knobs (QUIET / SOFT / NORMAL / FOCUS / LOUD / MAX-style)
- FOCUS scoring: RSSI EMA, clients, optional **data-frame activity**, PMF penalty
- RADIO: **DATA ACT**, **STRICT LK**, **DEPTH HOLD** (+ **HS DEPTH** PAIR / +M3 / FULL)
- Session **Z** skip until next LIGHT/AGGRO start; correct target resolution; beacon-table drop; empty-MAC fix for `00:…` BSSIDs

**Bottom bar**
- Left: focus **SSID only** (lock / pin / last HS) or `SCAN#ch` — no hop flicker, no MAC-as-name
- Right: **`&N`** = unique networks with saved HS/PMKID (not raw EAPOL counter)

**Spectrum**
- **`D`** cycles PAIR / +M3 / FULL for hunt; live `Cap::setHsDepth`
- Progress line M1…M4 + selected depth
- `<HIDDEN>` label + HID filter

**Storage**
- Pcap: verify size on card after header; scrub sub-header stubs (0–23 bytes) so LOOT stays clean
- `.22000` short-write guards kept

---

### v1.2.5

- Potfile path aligned with v1.2.1
- LOOT `Q` = results only; `U` uploads and pulls `results.txt`

### v1.2.1

- WPA-SEC / PwnCrack potfile status fixes
- LOOT `R` reload; `U` pulls potfile after upload

### v1.2

- Faster LOOT open

---

## License

MIT. Lab use only on networks you own or may test.
