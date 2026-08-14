#include "settings_menu.h"
#include "display.h"
#include "../core/config.h"
#include "../piglet/scene_layers.h"
#include "../piglet/wolf.h"
#include "../audio/sfx.h"
#include <M5Cardputer.h>
#include <string.h>
#include <stdio.h>

namespace SettingsMenu {

enum class Kind : uint8_t { TOGGLE, VALUE, TEXT };

struct Item {
    const char* label;
    Kind kind;
    uint8_t id;
    int minV;
    int maxV;
    int step;
};

static const Item SCENE[] = {
    {"NAME",     Kind::TEXT,   0,  0, 0, 0},
    {"SKIN",     Kind::VALUE,  1,  0, PIG_SKIN_COUNT - 1, 1},
    {"SEASON",   Kind::VALUE,  2,  0, SEASON_MODE_COUNT - 1, 1},
    {"SKY",      Kind::VALUE,  3,  0, SKY_MODE_COUNT - 1, 1},
    {"SCROLL",   Kind::VALUE,  4,  1, 10, 1},
    {"LIFE",     Kind::TOGGLE, 5,  0, 1, 1},
    {"ALL LAYERS", Kind::TOGGLE, 6, 0, 1, 1},
    {"WOLF",     Kind::TOGGLE, 7,  0, 1, 1},
    {"TREES",    Kind::TOGGLE, 8,  0, 1, 1},
    {"WEATHER",  Kind::TOGGLE, 9,  0, 1, 1},
    {"GRASS",    Kind::TOGGLE, 10, 0, 1, 1},
    {"SHOW PIG", Kind::TOGGLE, 11, 0, 1, 1},
    {"SEASON FX",Kind::TOGGLE, 12, 0, 1, 1},
    {"MOOD",     Kind::TOGGLE, 13, 0, 1, 1},
    {"ANIM TEST",Kind::TOGGLE, 14, 0, 1, 1},
    {"BRIGHT",   Kind::VALUE,  15, 10, 100, 10},
    {"SOUND",    Kind::VALUE,  16, 0, 5, 1},
};
static const uint8_t SCENE_N = sizeof(SCENE) / sizeof(SCENE[0]);

static const Item RADIO[] = {
    {"HOP MS",   Kind::VALUE,  0,  50, 2000, 50},
    {"LOCK MS",  Kind::VALUE,  1,  0, 15000, 500},
    {"LOCK HS",  Kind::TOGGLE, 2,  0, 1, 1},
    {"DEAUTH",   Kind::TOGGLE, 3,  0, 1, 1},
    {"RND MAC",  Kind::TOGGLE, 4,  0, 1, 1},
    {"ATK RSSI", Kind::VALUE,  5,  -90, -50, 5},
    {"HOP SET",  Kind::VALUE,  6,  0, HOP_SET_COUNT - 1, 1},
};
static const uint8_t RADIO_N = sizeof(RADIO) / sizeof(RADIO[0]);

static const Item BLE[] = {
    {"BLE BURST", Kind::VALUE, 0, 50, 500, 50},
    {"ADV TIME",  Kind::VALUE, 1, 50, 200, 25},
};
static const uint8_t BLE_N = sizeof(BLE) / sizeof(BLE[0]);

static const char* const H_SCENE[] = {
    "TYPE NAME. ENT SAVE.",
    "SKIN OF THE HOG.",
    "AUTO OR LOCK A SEASON.",
    "AUTO DUSK / DAY / NIGHT.",
    "WALK SPEED AT THE EDGES.",
    "SHE LIVES WHILE YOU WORK.",
    "MASTER: FULL SCENE OR BLANK.",
    "RANDOM WOLF VISITOR.",
    "FRUIT TREES AND DROPS.",
    "RAIN SNOW CLOUDS BIRDS.",
    "GRASS / DIRT FLOOR.",
    "DRAW THE PIG BODY.",
    "LEAVES BANKS BUTTERFLIES.",
    "SPEECH BUBBLE.",
    "IDLE: CYCLE FACES.",
    "SCREEN GLOW.",
    "0 = MUTE."
};
static const char* const H_RADIO[] = {
    "HOW LONG YOU SIT ON A CH.",
    "HOLD CHANNEL AFTER EAPOL.",
    "LOCK WHEN HANDSHAKE LANDS.",
    "KICK CLIENTS ON AGGRO / EP.",
    "NEW MAC EACH ATTACK START.",
    "SKIP WEAK APS FOR KICK.",
    "ALL / PRI 1-6-11 FIRST / CORE."
};
static const char* const H_BLE[] = {
    "MS BETWEEN BLE BURSTS.",
    "MS EACH ADVERTISEMENT."
};

static bool s_active = false;
static bool s_keyWas = false;
static bool s_editing = false;
static bool s_text = false;
static SettingsPage s_page = SettingsPage::SCENE;
static uint8_t s_idx = 0;
static uint8_t s_scroll = 0;
static char s_edit[32];
static const uint8_t VIS = 4;

static const Item* items(uint8_t* n) {
    if (s_page == SettingsPage::RADIO) { *n = RADIO_N; return RADIO; }
    if (s_page == SettingsPage::BLE) { *n = BLE_N; return BLE; }
    *n = SCENE_N;
    return SCENE;
}

static bool allLayersOn() {
    return SceneLayers::pig && SceneLayers::grass && SceneLayers::trees &&
           SceneLayers::sky && SceneLayers::weather && SceneLayers::seasonFx &&
           SceneLayers::mood && SceneLayers::wolf;
}

static const char* skinName(uint8_t s) {
    switch ((PigSkin)s) {
        case PigSkin::CLASSIC: return "CLASSIC";
        case PigSkin::BLUSH:   return "BLUSH";
        case PigSkin::HOG:     return "HOG";
        case PigSkin::ZOMBIE:  return "ZOMBIE";
        case PigSkin::RETRO:   return "RETRO";
        default: return "?";
    }
}
static const char* seasonName(uint8_t s) {
    switch ((SeasonMode)s) {
        case SeasonMode::AUTO:   return "AUTO";
        case SeasonMode::SPRING: return "SPRING";
        case SeasonMode::SUMMER: return "SUMMER";
        case SeasonMode::AUTUMN: return "AUTUMN";
        case SeasonMode::WINTER: return "WINTER";
        case SeasonMode::RETRO:  return "RETRO";
        default: return "?";
    }
}
static const char* skyName(uint8_t s) {
    switch ((SkyMode)s) {
        case SkyMode::AUTO:  return "AUTO";
        case SkyMode::DAY:   return "DAY";
        case SkyMode::NIGHT: return "NIGHT";
        default: return "?";
    }
}
static const char* hopSetName(uint8_t s) {
    switch ((HopSet)s) {
        case HopSet::ALL:      return "ALL 1-13";
        case HopSet::PRIORITY: return "PRI 1-6-11";
        case HopSet::CORE:     return "CORE 1-6-11";
        default: return "?";
    }
}

static int getValue(const Item& it) {
    PersonalityConfig& p = Config::personality();
    RadioConfig& r = Config::radio();
    BleConfig& b = Config::ble();
    if (s_page == SettingsPage::SCENE) {
        switch (it.id) {
            case 1: return p.pigSkin;
            case 2: return p.seasonMode;
            case 3: return p.skyMode;
            case 4: return p.scrollSpeed;
            case 5: return p.freeLife ? 1 : 0;
            case 6: return allLayersOn() ? 1 : 0;
            case 7: return (p.wolfEnabled && SceneLayers::wolf) ? 1 : 0;
            case 8: return (p.fruitTreesAmbient && SceneLayers::trees) ? 1 : 0;
            case 9: return SceneLayers::weather ? 1 : 0;
            case 10: return SceneLayers::grass ? 1 : 0;
            case 11: return SceneLayers::pig ? 1 : 0;
            case 12: return SceneLayers::seasonFx ? 1 : 0;
            case 13: return SceneLayers::mood ? 1 : 0;
            case 14: return p.animTest ? 1 : 0;
            case 15: return p.brightness;
            case 16: return p.soundLevel;
            default: return 0;
        }
    }
    if (s_page == SettingsPage::RADIO) {
        switch (it.id) {
            case 0: return r.hopMs;
            case 1: return r.lockMs;
            case 2: return r.lockOnHs ? 1 : 0;
            case 3: return r.deauth ? 1 : 0;
            case 4: return r.randomMac ? 1 : 0;
            case 5: return r.minRssi;
            case 6: return r.hopSet;
            default: return 0;
        }
    }
    return it.id == 0 ? b.burstMs : b.advMs;
}

static void formatValue(const Item& it, char* out, size_t len, bool editing) {
    if (it.kind == Kind::TEXT) {
        const char* n = s_text ? s_edit : Config::personality().name;
        snprintf(out, len, editing || s_text ? ">%s" : "%s", n);
        return;
    }
    if (it.kind == Kind::TOGGLE) {
        snprintf(out, len, getValue(it) ? "YES" : "NO");
        return;
    }
    char raw[20];
    raw[0] = '\0';
    if (s_page == SettingsPage::SCENE) {
        if (it.id == 1) strncpy(raw, skinName((uint8_t)getValue(it)), sizeof(raw) - 1);
        else if (it.id == 2) strncpy(raw, seasonName((uint8_t)getValue(it)), sizeof(raw) - 1);
        else if (it.id == 3) strncpy(raw, skyName((uint8_t)getValue(it)), sizeof(raw) - 1);
        else snprintf(raw, sizeof(raw), "%d", getValue(it));
    } else if (s_page == SettingsPage::RADIO && it.id == 6) {
        strncpy(raw, hopSetName((uint8_t)getValue(it)), sizeof(raw) - 1);
    } else if (s_page == SettingsPage::RADIO && it.id == 5) {
        snprintf(raw, sizeof(raw), "%d", getValue(it));
    } else {
        snprintf(raw, sizeof(raw), "%d", getValue(it));
    }
    raw[sizeof(raw) - 1] = '\0';
    if (editing) snprintf(out, len, "[%s]", raw);
    else strncpy(out, raw, len - 1);
    out[len - 1] = '\0';
}

static bool setValue(const Item& it, int v) {
    PersonalityConfig& p = Config::personality();
    RadioConfig& r = Config::radio();
    BleConfig& b = Config::ble();
    if (v < it.minV) v = it.maxV;
    if (v > it.maxV) v = it.minV;

    if (s_page == SettingsPage::SCENE) {
        switch (it.id) {
            case 1: {
                if (v == (int)PigSkin::ZOMBIE && !Config::isZombieSkinUnlocked()) {
                    Display::showToast("ZOMBIE LOCKED", 1200);
                    return false;
                }
                p.pigSkin = (uint8_t)v;
                break;
            }
            case 2: p.seasonMode = (uint8_t)v; break;
            case 3: p.skyMode = (uint8_t)v; break;
            case 4: p.scrollSpeed = (uint8_t)v; break;
            case 5: p.freeLife = v != 0; break;
            case 6:
                SceneLayers::setAll(v != 0);
                if (v == 0) Wolf::reset();
                break;
            case 7:
                p.wolfEnabled = v != 0;
                SceneLayers::wolf = v != 0;
                if (v == 0) Wolf::reset();
                break;
            case 8:
                p.fruitTreesAmbient = v != 0;
                SceneLayers::trees = v != 0;
                break;
            case 9: SceneLayers::weather = v != 0; break;
            case 10: SceneLayers::grass = v != 0; break;
            case 11: SceneLayers::pig = v != 0; break;
            case 12: SceneLayers::seasonFx = v != 0; break;
            case 13: SceneLayers::mood = v != 0; break;
            case 14: p.animTest = v != 0; break;
            case 15:
                p.brightness = (uint8_t)v;
                M5.Display.setBrightness(p.brightness * 255 / 100);
                break;
            case 16: p.soundLevel = (uint8_t)v; break;
            default: return false;
        }
        Config::save();
        return true;
    }
    if (s_page == SettingsPage::RADIO) {
        switch (it.id) {
            case 0: r.hopMs = (uint16_t)v; break;
            case 1: r.lockMs = (uint16_t)v; break;
            case 2: r.lockOnHs = v != 0; break;
            case 3: r.deauth = v != 0; break;
            case 4: r.randomMac = v != 0; break;
            case 5: r.minRssi = (int8_t)v; break;
            case 6: r.hopSet = (uint8_t)v; break;
            default: return false;
        }
        Config::save();
        return true;
    }
    if (it.id == 0) b.burstMs = (uint16_t)v;
    else b.advMs = (uint16_t)v;
    Config::save();
    return true;
}

static void keepVisible(uint8_t n) {
    if (s_idx < s_scroll) s_scroll = s_idx;
    if (s_idx >= s_scroll + VIS) s_scroll = (uint8_t)(s_idx - VIS + 1);
    if (s_scroll + VIS > n && n >= VIS) s_scroll = (uint8_t)(n - VIS);
}

void show(SettingsPage page) {
    s_active = true;
    s_page = page;
    s_idx = 0;
    s_scroll = 0;
    s_editing = false;
    s_text = false;
    s_keyWas = true;
}

void hide() {
    s_active = false;
    s_editing = false;
    s_text = false;
}

bool isActive() { return s_active; }
SettingsPage page() { return s_page; }

const char* bottomHint() {
    if (s_text) return "type  ENT save  ` cancel";
    if (s_editing) return ";/. change  ENT done";
    uint8_t n = 0;
    const Item* it = items(&n);
    if (it && s_idx < n) {
        if (it[s_idx].kind == Kind::TOGGLE) return "ENT yes/no  ;/.  ` back";
        if (it[s_idx].kind == Kind::TEXT) return "ENT type name";
        return "ENT edit  ;/.  ` back";
    }
    return ";/.  ENT  ` back";
}

void update() {
    if (!s_active) return;
    if (!M5Cardputer.Keyboard.isChange()) return;
    bool pressed = M5Cardputer.Keyboard.isPressed();
    if (!pressed) {
        s_keyWas = false;
        return;
    }
    if (s_keyWas) return;
    s_keyWas = true;

    auto keys = M5Cardputer.Keyboard.keysState();
    bool up = M5Cardputer.Keyboard.isKeyPressed(';');
    bool down = M5Cardputer.Keyboard.isKeyPressed('.');
    bool esc = M5Cardputer.Keyboard.isKeyPressed('`') ||
               M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) ||
               keys.del;

    uint8_t n = 0;
    const Item* list = items(&n);
    if (!list || n == 0) return;
    const Item& cur = list[s_idx < n ? s_idx : 0];

    if (s_text) {
        if (keys.enter) {
            PersonalityConfig& p = Config::personality();
            strncpy(p.name, s_edit, sizeof(p.name) - 1);
            p.name[sizeof(p.name) - 1] = '\0';
            if (!p.name[0]) strncpy(p.name, "Lexi", sizeof(p.name) - 1);
            Config::save();
            s_text = false;
            SFX::play(SFX::CONFIRM);
            Display::showToast("NAME SAVED", 900);
            return;
        }
        if (esc) {
            s_text = false;
            SFX::play(SFX::BACK_NAV);
            return;
        }
        if (keys.del) {
            size_t L = strlen(s_edit);
            if (L) s_edit[L - 1] = '\0';
            return;
        }
        for (char c : keys.word) {
            if (c < 32 || c >= 127) continue;
            size_t L = strlen(s_edit);
            if (L + 1 < sizeof(s_edit) && L < 16) {
                s_edit[L] = c;
                s_edit[L + 1] = '\0';
            }
        }
        return;
    }

    if (esc) {
        if (s_editing) {
            s_editing = false;
            SFX::play(SFX::BACK_NAV);
            return;
        }
        hide();
        return;
    }

    if (up || down) {
        if (s_editing && cur.kind == Kind::VALUE) {
            int next = getValue(cur) + (up ? cur.step : -cur.step);
            if (setValue(cur, next)) SFX::play(SFX::CLICK);
            return;
        }
        s_editing = false;
        if (up && s_idx > 0) s_idx--;
        else if (down && s_idx + 1 < n) s_idx++;
        keepVisible(n);
        SFX::play(SFX::MENU_CLICK);
        return;
    }

    if (!keys.enter) return;

    if (cur.kind == Kind::TOGGLE) {
        int next = getValue(cur) ? 0 : 1;
        if (setValue(cur, next)) {
            SFX::play(SFX::CONFIRM);
            Display::showToast(next ? "YES" : "NO", 700);
        }
        return;
    }
    if (cur.kind == Kind::TEXT) {
        strncpy(s_edit, Config::personality().name, sizeof(s_edit) - 1);
        s_edit[sizeof(s_edit) - 1] = '\0';
        s_text = true;
        SFX::play(SFX::MENU_CLICK);
        return;
    }
    s_editing = !s_editing;
    SFX::play(SFX::MENU_CLICK);
}

void draw(M5Canvas& canvas) {
    const uint16_t UI_BG = 0x2145, UI_PANEL = 0x3A8A, UI_TITLE = 0xFFE0;
    const uint16_t UI_TEXT = 0xEF5D, UI_DIM = 0x9CD3, UI_SEL = 0xFDB6;
    canvas.fillSprite(UI_BG);

    const char* title = "SCENE";
    if (s_page == SettingsPage::RADIO) title = "RADIO";
    else if (s_page == SettingsPage::BLE) title = "BLE";

    canvas.setTextDatum(top_center);
    canvas.setTextSize(2);
    canvas.setTextColor(UI_TITLE);
    canvas.drawString(title, DISPLAY_W / 2, 2);
    canvas.drawLine(10, 20, DISPLAY_W - 10, 20, UI_TITLE);

    uint8_t n = 0;
    const Item* list = items(&n);
    canvas.setTextDatum(top_left);
    canvas.setTextSize(2);
    const int y0 = 24;
    const int lh = 18;
    for (uint8_t i = 0; i < VIS && (s_scroll + i) < n; i++) {
        uint8_t idx = s_scroll + i;
        int y = y0 + i * lh;
        bool sel = (idx == s_idx);
        if (sel) {
            canvas.fillRect(5, y - 2, DISPLAY_W - 10, lh, UI_SEL);
            canvas.fillRect(5, y - 2, 3, lh, UI_TITLE);
            canvas.setTextColor(UI_BG);
        } else {
            canvas.fillRect(5, y - 1, DISPLAY_W - 10, lh - 2, UI_PANEL);
            canvas.setTextColor(UI_TEXT);
        }
        canvas.drawString(list[idx].label, 12, y);
        char val[22];
        formatValue(list[idx], val, sizeof(val), sel && s_editing);
        canvas.setTextDatum(top_right);
        canvas.drawString(val, DISPLAY_W - 10, y);
        canvas.setTextDatum(top_left);
    }

    canvas.setTextSize(1);
    canvas.setTextColor(UI_DIM);
    if (s_scroll > 0) canvas.drawString("^", DISPLAY_W - 12, 22);
    if (s_scroll + VIS < n) canvas.drawString("v", DISPLAY_W - 12, y0 + (VIS - 1) * lh);

    const char* const* hints = H_SCENE;
    if (s_page == SettingsPage::RADIO) hints = H_RADIO;
    else if (s_page == SettingsPage::BLE) hints = H_BLE;
    if (s_idx < n) {
        canvas.setTextColor(UI_TITLE);
        canvas.setTextDatum(top_center);
        canvas.drawString(hints[s_idx], DISPLAY_W / 2, MAIN_H - 10);
        canvas.setTextDatum(top_left);
    }
}

}  // namespace SettingsMenu
