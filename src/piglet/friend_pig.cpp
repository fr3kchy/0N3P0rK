// Companion pig — own life in world space. Never follows the player.
// Drawn BEHIND the player (see avatar drawFrame order).
#include "friend_pig.h"
#include "avatar.h"
#include "mood.h"
#include "trees.h"
#include "../core/xp.h"
#include "../core/config.h"
#include "../audio/sfx.h"
#include <esp_random.h>
#include <math.h>
#include <string.h>

namespace FriendPig {

static constexpr int16_t GROUND_Y = 106;
static constexpr int16_t PX = 3;

// World position (same loop as trees) — NOT glued to the camera
static int16_t s_baseX = 200;
static int16_t s_scroll = 0;
static float   s_walk = 0.f;   // sub-pixel walk accumulator on baseX
static float   s_vx = 0.f;
static bool    s_faceRight = false;
static bool    s_sitting = false;
static bool    s_sniffing = false;
static uint8_t s_hearts = 5;
static uint32_t s_nextAi = 0;
static uint32_t s_bittenUntil = 0;
static uint32_t s_chatCool = 0;
static uint32_t s_actionUntil = 0;
static bool    s_ready = false;

static int16_t worldToScreen(int16_t worldX) {
    int16_t bx = (int16_t)(worldX + s_scroll);
    while (bx > Trees::WORLD_WRAP_HI) bx = (int16_t)(bx - Trees::WORLD_SPAN);
    while (bx < Trees::WORLD_WRAP_LO) bx = (int16_t)(bx + Trees::WORLD_SPAN);
    return bx;
}

static int16_t screenX() {
    return worldToScreen(s_baseX);
}

bool unlocked() { return XP::getLevel() >= 40; }

bool enabled() {
    return unlocked() && Config::personality().friendEnabled;
}

bool isActive() { return enabled() && s_hearts > 0; }

int getFeetX() { return (int)screenX(); }

void begin() {
    // Park somewhere in the world — not on top of typical player start
    s_baseX = (int16_t)(180 + (int)(esp_random() % 200));
    s_scroll = 0;
    s_walk = 0.f;
    s_vx = 0.f;
    s_faceRight = (esp_random() & 1) != 0;
    s_sitting = false;
    s_sniffing = false;
    s_hearts = 5;
    s_nextAi = millis() + 1200;
    s_bittenUntil = 0;
    s_chatCool = 0;
    s_actionUntil = 0;
    s_ready = true;
}

void scroll(int8_t dx) {
    if (!enabled()) return;
    // Identical treadmill to trees — she stays planted in the world
    s_scroll = (int16_t)(s_scroll + dx);
}

void onWolfBitten() {
    if (!isActive()) return;
    if (s_hearts > 0) s_hearts--;
    s_bittenUntil = millis() + 2500;
    s_sitting = false;
    s_sniffing = false;
    s_vx = (esp_random() & 1) ? 1.5f : -1.5f;
    s_faceRight = s_vx > 0;
    SFX::play(SFX::OINK_SQUEAL);
    if (s_hearts == 0) {
        // Leave the area — respawn far away later
        s_baseX = (int16_t)(esp_random() % (uint32_t)Trees::WORLD_SPAN);
        s_nextAi = millis() + 20000;
        s_hearts = 5;
        s_vx = 0;
    }
}

static void aiTick(uint32_t now) {
    if (now < s_nextAi) return;
    s_nextAi = now + 1000 + (esp_random() % 3200);
    if (now < s_bittenUntil) return;

    // 100% independent — never reads player position for steering
    uint8_t r = (uint8_t)(esp_random() % 100);
    if (r < 20) {
        s_sitting = true;
        s_sniffing = false;
        s_vx = 0;
        s_nextAi = now + 2500 + (esp_random() % 3500);
    } else if (r < 35) {
        s_sitting = false;
        s_vx = 0;
        s_sniffing = (esp_random() % 2) == 0;
        s_actionUntil = now + 700 + (esp_random() % 1400);
    } else if (r < 60) {
        s_sitting = false;
        s_sniffing = false;
        s_faceRight = false;
        s_vx = -0.3f - (float)(esp_random() % 30) / 100.f;
    } else if (r < 85) {
        s_sitting = false;
        s_sniffing = false;
        s_faceRight = true;
        s_vx = 0.3f + (float)(esp_random() % 30) / 100.f;
    } else {
        s_sitting = false;
        s_sniffing = false;
        s_vx = 0;
    }
}

void update() {
    if (!enabled()) return;
    if (!s_ready) begin();
    uint32_t now = millis();
    aiTick(now);
    if (now >= s_actionUntil) s_sniffing = false;

    // Walk in WORLD space only
    s_walk += s_vx;
    while (s_walk >= 1.f) {
        s_baseX = (int16_t)(s_baseX + 1);
        s_walk -= 1.f;
    }
    while (s_walk <= -1.f) {
        s_baseX = (int16_t)(s_baseX - 1);
        s_walk += 1.f;
    }

    // Wrap world — may leave the screen entirely (you can run away)
    while (s_baseX >= Trees::WORLD_SPAN)
        s_baseX = (int16_t)(s_baseX - Trees::WORLD_SPAN);
    while (s_baseX < 0)
        s_baseX = (int16_t)(s_baseX + Trees::WORLD_SPAN);

    // Chat only if paths cross — she still does not walk to you
    int sx = screenX();
    if (sx >= -20 && sx <= 260) {
        int pig = Avatar::getCurrentX() + 20;
        int dist = abs(pig - sx);
        if (dist < 30 && now > s_chatCool && !s_sitting) {
            s_chatCool = now + 12000;
            static const char* lines[] = {
                "HI FRIEND", "OINK OINK", "NICE FARM", "BYE BYE",
                "WOLF BAD", "SNACK?", "U R COOL", "I LIVE HERE"
            };
            Mood::say(lines[esp_random() % 8]);
        }
    }
}

void draw(M5Canvas& canvas, int16_t yOffset) {
    if (!isActive()) return;
    int16_t sx = screenX();
    // Off-screen — not drawn (not glued to view)
    if (sx < -30 || sx > 270) return;

    int16_t feetY = (int16_t)(GROUND_Y + yOffset);
    bool walking = (fabsf(s_vx) > 0.08f) && !s_sitting;
    bool sniff = s_sniffing && !s_sitting && !walking;
    Avatar::drawCompanion(canvas, sx, feetY, s_faceRight, walking, s_sitting, sniff);
    if (s_hearts <= 2) {
        canvas.setTextColor(0xF800);
        canvas.setTextSize(1);
        canvas.drawString("<3", sx - 4, feetY - 40);
    }
}

}  // namespace FriendPig
