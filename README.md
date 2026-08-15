# 0N3P0rK

**Это мой проект.** Автор: [lexilexiko](https://github.com/lexilexiko).  
Tamagotchi-свинка + сарай на **M5Cardputer** (ESP32-S3, 8 MB).

Свинка живёт сама. Capture / LOOT / IR / BLE — ящик с инструментами. Они ею не управляют.

| | |
|--|--|
| **Проект** | **0N3P0rK** — мой |
| **Версия** | **v0.1.0** |
| **Релиз** | [v0.1.0](https://github.com/lexilexiko/0N3P0rK/releases/tag/v0.1.0) |
| **Плата** | M5Cardputer |
| **SD** | `/0N3P0rK/` |

**Legal:** только свои сети или те, на которые есть письменное разрешение.

---

## Тост

Спасибо **основателю** — **0ct0** и [M5PORKCHOP](https://github.com/0ct0sec/M5PORKCHOP).  
Без этой идеи свинка на Cardputer не родилась бы. Респект.

Спасибо ферме и настроению, которые меня зацепили в [OnePork](https://github.com/lexilexiko/OnePork).

**Но дальше я переделал всё с нуля.**  
Не форк. Не рескин. Не «ещё один porkchop с другим именем».  
Новый код, новый сарай, свои руки, свой ритм.

**Подтверждаю: 0N3P0rK — мой.**

```
        ^__^
        (oo)\_______
        (__)\       )\/\
            ||----w |
            ||     ||
     0N3P0rK · lexilexiko
```

---

## Скачайте то, что меня вдохновило

Это **не** этот репозиторий. Это предки. Их стоит увидеть.

| Что | Кто | Ссылка |
|-----|-----|--------|
| **M5PORKCHOP** — оригинал, основатель | **0ct0** | https://github.com/0ct0sec/M5PORKCHOP |
| **OnePork** — фан-пакет / ферма, которая меня завела | lexilexiko | https://github.com/lexilexiko/OnePork |
| Поддержать основателя | 0ct0 | https://buymeacoffee.com/0ct0 |

Потом возвращайтесь сюда: **это уже другой сарай.**

---

## Что умеет 0N3P0rK

### Ферма (свинка)

- Ходит, прыгает, сидит, бьёт — как в OnePork по кнопкам
- Сама бродит, прячется от волка, ест падающие фрукты (кормим мы её руками не умеем)
- **5 сердец** слева · **время / день-ночь / сезон** по центру · **еда %** справа
- Волк кусает **1 сердце**. Голод падает сам. Ночью экран темнее (−20, пол 10), утром светлее (+20, потолок 100)
- LIFE: сама живёт или вы рулите
- Монологи: читаемые шутки, leet очень редко

### ATTACK

| Пункт | Что делает |
|-------|------------|
| **LIGHT** | Тихий снифф на канале. Кольца INCOMING. Имя сети на HUD |
| **AGGRO** | Хоп 1–13, kick, ловля. Кольца OUTGOING |
| **EVILPIG** | Лабораторный портал. Только свои сети |
| **PIGPASS** | Оффлайн WPA: wordlist / mask на SD |
| **BLE** | Сырые кадры Apple / Win / Android. Свои устройства |
| **IR PORT** | Сначала регион **NA / EU**, потом Space — fire. GPIO44. Файлы с SD |
| **SPECTRUM** | 2.4 sweep: лепестки + водопад. ENT — сеть, клиенты, пакеты. Space — kick |
| **STOP** | Радио спать |

### LOOT

Один мешок. Вкладки **WPA-SEC** / **PWNCRACK**.

- Захват: `SSID_BSSID.pcap` + companion `.txt` (как в OnePork по имени сети)
- Один файл на сеть, без 5–7 копий
- **S** sync · **T** test · **ENT** детали и пароль если есть
- Pwncrack: конверт в `.hc22000`, видно upload / potfile, `[OK]` только с паролем

### SET

**SYSTEM** (яркость, звук, dim) · **RADIO** · **BLE** · **CONNECT** (скан + пароль домашней сети для sync) · **USB SD** (карта как диск на ПК, как в Launcher).

---

## Кнопки на ферме

| Клавиша | Действие |
|---------|----------|
| `,` / `/` | идти (удерживать) |
| `;` | прыжок |
| `.` | сидеть (удерживать) |
| Space | атака |
| `` ` `` / Esc | меню / назад |

В меню: `;` / `.` вверх-вниз, Enter выбрать, Esc назад.

IR: `;` / `.` регион, Enter — готов, **Space** — огонь, **R** — NA/EU, **E** — файл с SD.

---

## Собрать / прошить

```text
pio run -e m5cardputer
pio run -e m5cardputer -t upload
```

Исходники. Готовый `.bin` в репозиторий не кладу — собирайте сами.

**Релиз:** https://github.com/lexilexiko/0N3P0rK/releases/tag/v0.1.0

---

## SD

Рукопожатия в одной папке. Ключи и логи — у каждого сервиса свои.

```text
/0N3P0rK/
  hs/                  все рукопожатия (.pcap .22000 .txt)
  wpa-sec/
    key.txt            ключ WPA-SEC
    results.txt        potfile
    uploaded.txt
  pwncrack/
    key.txt            ключ pwncrack.org
    results.txt
    uploaded.txt
  pigpass/
  Passworld/
  evilpig/
  ir/
```

Старые `.pcap` / `.22000` из `wpa-sec/` и `pwncrack/` переедут в `hs/` при загрузке.  
Ключи кладите файлами на карту. Веб-морды для ключей нет.

---

## Чего здесь нет (намерено)

Лабораторный радио-узел OnePork / M5PORKCHOP: **OINK / WARHOG / SPECTRUM / XFER** — не копировал.  
0N3P0rK — свинка и свой ящик. Не весь porkchop целиком.

---

## Toast (EN)

Thanks to founder **0ct0** for [M5PORKCHOP](https://github.com/0ct0sec/M5PORKCHOP).  
The Cardputer pig starts there.

Download what inspired me: [M5PORKCHOP](https://github.com/0ct0sec/M5PORKCHOP) and [OnePork](https://github.com/lexilexiko/OnePork).

Then know this: **I remade everything from scratch.**  
**0N3P0rK is my project.** — [lexilexiko](https://github.com/lexilexiko)

Own / authorized networks only. MIT. Donate to 0ct0 if the original pig paid your rent in joy: https://buymeacoffee.com/0ct0
