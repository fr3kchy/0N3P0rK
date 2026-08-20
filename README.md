# 0N3P0rK

Tamagotchi pig + barn for **M5Cardputer** and **M5Cardputer ADV**.  
One binary for both boards. The model is read from `M5.getBoard()` — GPIO 8/9 are never probed.

Author: [lexilexiko](https://github.com/lexilexiko)  
License: MIT

> Radio modes are for **your own / explicitly authorized** networks only. Lab tool, not other people's Wi‑Fi.

---

## Features

### Farm
- The pig walks, eats fruit, hides, scratches
- Hunger, mood, hearts. **5 wolf bites** → zombie; **5 fruit hearts** cure her. A heart is added when hunger hits 100%
- Seasons, sky, grass, trees, weather, birds
- Wolf visitor. **WOLF EAT** moves a random handshake into `/0N3P0rK/wolf` (never keys). Hit the wolf / turn Am off to get loot back
- Battery in the top bar
- Speech bubble (Font0, ASCII). Custom lines: drop files in `/0N3P0rK/talk/`
- Boot splash: **0N3P0rK**

### Menu

**ATTACK**
| Item | What it does |
| --- | --- |
| LIGHT | Quiet sniff on the current channel |
| AGGRO | Hop 1–13, kick, catch EAPOL / PMKID |
| EVILPIG | Lab portal: clone your own net, loot, kick |
| PIGPASS | Offline WPA from a wordlist / mask on SD |
| BLE | Apple / Win / Android frames (own devices) |
| IR PORT | IR at the TV. `SPC` fire, `R` NA/EU, `E` file from `/0N3P0rK/ir` |
| SPECTRUM | 2.4 sweep, lobes, waterfall. `ENT` lock, `SPC` kick |
| STOP | Radio sleep. Loot on the card stays |

**LOOT** — one bag: WPA-SEC and pwncrack.org. `S` sync, `T` test.

**PIG** — name, skin, season, sky, scene layers, LIFE (she walks herself) / manual control, ANIM TEST (`-` / `=` on the farm).

**SET**
1. **SYSTEM** — brightness, sound, dim
2. **RADIO** — pack, hop, deauth, PMKID, CSA, pcap…
3. **BLE** — burst / adv time
4. **CONNECT** — home Wi‑Fi for sync (scan, type pass)
5. **KEYS** — farm hotkeys
6. **USB SD** — card as a disk on the PC. Eject, then `` ` ``

---

## Keys

| Key | Action |
| --- | --- |
| `` ` `` / `~` / Esc | Back / leave the mode |
| Backspace | Minimize the window, **not** exit |
| `;` / `.` | Up / down in lists |
| Enter | Select |
| `R` | RADIO (default; remappable) |
| `A` `L` `P` `E` `B` `I` `S` `H` | AGGRO / LIGHT / PIGPASS / EVILPIG / BLE / IR / SPECTRUM / LOOT |

Change binds in **SET → KEYS**. `ENT` to set, Backspace to clear.

---

## SD card

Root: `/0N3P0rK/`

```
/0N3P0rK
  handshakes/     pcap + 22000
  wpa-sec/        key.txt  results.txt  uploaded.txt
  pwncrack/       key.txt  results.txt  uploaded.txt
  evilpig/        creds.csv
  pigpass/        cracked.txt  checkpoint
  Passworld/      wordlists for PIGPASS
  ir/             IR files
  wolf/           what the wolf stole
  talk/           custom pig lines
```

**talk** — one file per mood, one line = one bubble, `#` starts a comment, ~24 chars:

`idle.txt` `happy.txt` `hungry.txt` `sad.txt` `sleepy.txt` `fed.txt` `pet.txt` `play.txt` `bird.txt`

---

## Firmware

One `.bin` for Cardputer and ADV.

1. USB into the Cardputer, pick the COM yourself (Device Manager, VID `303A`)
2. M5Launcher / `esptool` / PlatformIO `pio run -t upload --upload-port COMx`
3. SD card in the slot **before** boot

Build from source:

```
pio run
```

Artifact: `.pio/build/m5cardputer/firmware.bin`  
Release: `0N3P0rK_v2.0_m5cardputer.bin`

PlatformIO: `espressif32@6.12.0`, Arduino, ESP32-S3 8MB.

---

## v2.0

- Splash and serial: **0N3P0rK v2.0**
- Font0 like 1.6
- More pig lines + `/talk` folder
- SD like 1.2 (GPIO 5 HIGH only during mount), Cardputer / ADV keys restored after SD
- Zombie after 5 bites, battery in the top bar, RADIO hotkey, ANIM TEST

---

## Hardware

- Original M5Cardputer (74HC138 matrix) and Cardputer ADV (TCA8418)
- After SD: original runs `Keyboard.begin()` again; ADV keeps GPIO 5 HIGH and does **not** re-init I2C
- GPIO 8/9 are left alone — on ADV that is the keyboard

Inspired by M5PORKCHOP (0ct0) and the OnePork farm package.  
Those projects remain their authors'. **0N3P0rK** is a separate work.
