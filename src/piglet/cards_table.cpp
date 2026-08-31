// Cards table on the farm (lv 45+). Game stub: monologue only.
#include "cards_table.h"
#include "avatar.h"
#include "mood.h"
#include "../core/xp.h"
#include "../core/config.h"
#include "../audio/sfx.h"
#include <esp_random.h>

namespace CardsTable {

static constexpr int16_t GROUND_Y = 106;
static int16_t s_worldX = 200;
static int16_t s_scroll = 0;
static uint32_t s_cool = 0;
static bool s_ready = false;

bool unlocked() {
    // Level gate + SCENE toggle (CARDS knobs in settings)
    return XP::getLevel() >= 45 && Config::personality().cardsEnabled;
}

static int16_t screenX() {
    int16_t x = (int16_t)(s_worldX + s_scroll);
    while (x > 280) x = (int16_t)(x - 300);
    while (x < -40) x = (int16_t)(x + 300);
    return x;
}

void begin() {
    // Park slightly off to the side so you walk into it
    s_worldX = 200;
    s_scroll = 0;
    s_cool = 0;
    s_ready = true;
}

void scroll(int8_t dx) {
    if (!unlocked()) return;
    s_scroll = (int16_t)(s_scroll + dx);
}

void update() {
    if (!unlocked()) return;
    if (!s_ready) begin();

    int16_t sx = screenX();
    int pig = Avatar::getCurrentX() + 20;
    int dist = abs(pig - (int)sx);
    bool atTable = dist < 36;
    bool jump = Avatar::isJumping() && Avatar::getJumpLiftPx() > 2;

    if (atTable && jump && millis() > s_cool) {
        s_cool = millis() + 5000;
        // Stub until card game ships
        Mood::say("CARDS NOT READY");
        SFX::play(SFX::MENU_CLICK);
        Avatar::setState(AvatarState::SAD);
    }
}

void draw(M5Canvas& canvas, int16_t yOffset) {
    if (!unlocked()) return;
    int16_t x = screenX();
    int16_t y = GROUND_Y + yOffset;
    // Bigger pixel table (~1.5x)
    // legs
    canvas.fillRect(x - 14, y - 16, 4, 16, 0x8200);
    canvas.fillRect(x + 11, y - 16, 4, 16, 0x8200);
    // top board
    canvas.fillRect(x - 18, y - 22, 38, 7, 0x9A40);
    canvas.drawRect(x - 18, y - 22, 38, 7, 0x7200);
    canvas.fillRect(x - 18, y - 22, 38, 2, 0xC408); // edge highlight
    // deck of cards
    canvas.fillRect(x - 6, y - 28, 12, 8, 0xF800);
    canvas.fillRect(x - 5, y - 27, 10, 6, 0xFFFF);
    canvas.fillRect(x - 4, y - 26, 3, 3, 0xF800); // pip
    // side stack
    canvas.fillRect(x + 8, y - 26, 6, 5, 0x001F);
    canvas.drawRect(x + 8, y - 26, 6, 5, 0x0000);
}



}  // namespace CardsTable
