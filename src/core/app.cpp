#include "app.h"
#include "../ui/display.h"
#include "../ui/menu.h"
#include "../ui/loot_menu.h"
#include "../ui/settings_menu.h"
#include "../modes/evilpig.h"
#include "../modes/pigpass.h"
#include "../modes/blepig.h"
#include "../piglet/avatar.h"
#include "../piglet/mood.h"
#include "../audio/sfx.h"
#include <M5Cardputer.h>

namespace App {

static AppMode s_mode = AppMode::FARM;
static bool s_g0Was = false;

void begin() {
    s_mode = AppMode::FARM;
    Menu::begin();
    pinMode(0, INPUT_PULLUP);
    s_g0Was = digitalRead(0) == LOW;
}

AppMode mode() { return s_mode; }

const char* modeName() {
    switch (s_mode) {
        case AppMode::FARM:     return "FARM";
        case AppMode::MENU:     return "MENU";
        case AppMode::ATTACK:   return "ATTACK";
        case AppMode::LOOT:     return "LOOT";
        case AppMode::WIFI:     return "WIFI";
        case AppMode::PIG:      return "PIG";
        case AppMode::TUNE:     return "TUNE";
        case AppMode::EVILPIG:  return "EVILPIG";
        case AppMode::PIGPASS:  return "PIGPASS";
        case AppMode::BLE:      return "BLE";
        default:                return "?";
    }
}

void setMode(AppMode m) {
    if (s_mode == m) return;
    if (s_mode == AppMode::LOOT) LootMenu::hide();
    if (s_mode == AppMode::PIG || s_mode == AppMode::TUNE) SettingsMenu::hide();
    if (s_mode == AppMode::EVILPIG && EvilPigMode::isRunning()) EvilPigMode::stop();
    if (s_mode == AppMode::PIGPASS && PigpassMode::isRunning()) PigpassMode::stop();
    if (s_mode == AppMode::BLE && BlePigMode::isRunning()) BlePigMode::stop();
    s_mode = m;
    Menu::onEnter(m);
    if (m == AppMode::LOOT) LootMenu::show();
    if (m == AppMode::EVILPIG) EvilPigMode::start();
    if (m == AppMode::PIGPASS) PigpassMode::start();
    if (m == AppMode::BLE) BlePigMode::start();
    SFX::play(m == AppMode::FARM ? SFX::MODE_EXIT : SFX::MODE_ENTER);
}

// Same idle roam as OnePork:
//   , left hold    / right hold
//   ; jump         SPACE attack-hop
//   . sit hold
// F feed / P pet stay as Tamagotchi extras (no farm hotkey clash).
static void farmPoll() {
    bool left  = M5Cardputer.Keyboard.isKeyPressed(',');
    bool right = M5Cardputer.Keyboard.isKeyPressed('/');
    bool jumpKey = M5Cardputer.Keyboard.isKeyPressed(';');
    bool attackKey = M5Cardputer.Keyboard.isKeyPressed(' ');
    bool sitKey = M5Cardputer.Keyboard.isKeyPressed('.');

    static bool idleJumpWas = false;
    static bool idleAttackWas = false;
    static uint32_t idleMoveMs = 0;
    bool jumpEdge = jumpKey && !idleJumpWas;
    bool attackEdge = attackKey && !idleAttackWas;
    idleJumpWas = jumpKey;
    idleAttackWas = attackKey;

    const bool locked = Avatar::isControlLocked() || Avatar::isPlayDead();

    if (!locked && sitKey) {
        Avatar::setSitting(!left && !right && !jumpKey && !attackKey);
    }

    uint32_t nowMove = millis();
    if (nowMove - idleMoveMs >= 12) {
        idleMoveMs = nowMove;
        static int lastHold = 0;
        int hold = 0;
        if (!locked && left && !right) hold = -1;
        else if (!locked && right && !left) hold = 1;
        if (hold != 0) Avatar::playerWalkHold(hold);
        else if (lastHold != 0) Avatar::playerWalkHold(0);
        lastHold = hold;
    }

    if (!locked && jumpEdge && !Avatar::isJumping() && !Avatar::isAttackHopping()) {
        Avatar::setPlayDead(false);
        Avatar::setSitting(false);
        Avatar::cuteJump();
        Avatar::tryStompTree();
        Avatar::setState(AvatarState::HAPPY);
        Mood::play();
    }
    if (!locked && attackEdge && !Avatar::isAttackHopping() && !Avatar::isJumping()) {
        Avatar::setPlayDead(false);
        Avatar::setSitting(false);
        Avatar::attackHop();
        Avatar::setState(AvatarState::HUNTING);
        Mood::play();
    }
}

void loop() {
    bool g0 = digitalRead(0) == LOW;
    if (g0 && !s_g0Was) Display::toggleScreenPower();
    s_g0Was = g0;
    if (Display::isScreenForcedOff()) return;

    if (s_mode == AppMode::FARM) farmPoll();

    if (s_mode == AppMode::LOOT) {
        LootMenu::update();
        if (!LootMenu::isActive()) setMode(AppMode::MENU);
    } else if (s_mode == AppMode::EVILPIG) {
        EvilPigMode::update();
        if (!EvilPigMode::isRunning()) setMode(AppMode::MENU);
    } else if (s_mode == AppMode::PIGPASS) {
        PigpassMode::update();
        if (!PigpassMode::isRunning()) setMode(AppMode::MENU);
    } else if (s_mode == AppMode::BLE) {
        BlePigMode::update();
        if (!BlePigMode::isRunning()) setMode(AppMode::MENU);
    } else if (s_mode == AppMode::PIG || s_mode == AppMode::TUNE) {
        SettingsMenu::update();
        if (!SettingsMenu::isActive()) setMode(AppMode::MENU);
    } else if (s_mode == AppMode::MENU) {
        Menu::update();
    }

    if (!M5Cardputer.Keyboard.isChange()) return;
    if (!M5Cardputer.Keyboard.isPressed()) return;

    Display::resetDimTimer();

    if (s_mode == AppMode::LOOT || s_mode == AppMode::EVILPIG ||
        s_mode == AppMode::PIGPASS || s_mode == AppMode::BLE ||
        s_mode == AppMode::PIG || s_mode == AppMode::TUNE) return;
    if (s_mode == AppMode::MENU) return;  // ` / backspace handled in Menu::update

    Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
    bool back = M5Cardputer.Keyboard.isKeyPressed('`') ||
                M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) ||
                st.del;
    char typed = 0;
    for (char c : st.word) {
        if (c == '`' || c == 27) back = true;
        else if (typed == 0) typed = c;
    }

    if (s_mode == AppMode::FARM) {
        if (back) {
            setMode(AppMode::MENU);
            return;
        }
        if (typed == 'f' || typed == 'F') Mood::feed();
        else if (typed == 'p' || typed == 'P') Mood::pet();
        return;
    }

    if (back) {
        setMode(AppMode::MENU);
        return;
    }

    Menu::handleKey(typed, st.enter, st.del, st.fn);
}

}  // namespace App
