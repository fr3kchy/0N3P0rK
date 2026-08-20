#include "boot_splash.h"
#include "display.h"
#include "../piglet/avatar.h"
#include "../audio/sfx.h"
#include "../board/board.h"
#include "../build_info.h"
#include <M5Cardputer.h>
#include <math.h>
#include <stdio.h>

static bool wantSkip() {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isPressed()) return true;
    if (M5Cardputer.BtnA.isPressed()) return true;
    return false;
}

static void restoreGfx() {
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(TL_DATUM);
    M5.Display.setTextWrap(false);
    M5.Display.setTextColor(UiStyle::TEXT);
}

void runBootSplash() {
    const int savedX = Avatar::getCurrentX();
    SFX::play(SFX::OINK_HAPPY);
    Avatar::setFacingRight();
    Avatar::setGrassMoving(true, false, true);

    M5Canvas splash(&M5.Display);
    splash.setColorDepth(8);
    const bool ok = splash.createSprite(DISPLAY_W, DISPLAY_H);

    static const char kTitle[] = "0N3P0rK";
    const int n = 7;
    const int pigFrom = -40;
    const int pigTo = 96;
    const int frames = 70;
    bool skipped = false;

    auto finish = [&]() {
        if (ok) splash.deleteSprite();
        Avatar::setManualWalk(false);
        Avatar::setGrassMoving(false, true, true);
        Avatar::setX(savedX);
        restoreGfx();
    };

    if (!ok) {
        M5.Display.fillScreen(UiStyle::BG);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextSize(2);
        M5.Display.setTextDatum(middle_center);
        M5.Display.setTextColor(UiStyle::PINK);
        M5.Display.drawString(kTitle, DISPLAY_W / 2, 52);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(UiStyle::TEXT);
        M5.Display.drawString("v" ON3PORK_VERSION, DISPLAY_W / 2, 80);
        M5.Display.setTextColor(UiStyle::DIM);
        M5.Display.drawString(Board::modelLabel(), DISPLAY_W / 2, 100);
        uint32_t t0 = millis();
        while ((millis() - t0) < 1600) {
            if (wantSkip()) break;
            delay(16);
        }
        finish();
        return;
    }

    for (int f = 0; f <= frames; f++) {
        if (wantSkip()) {
            skipped = true;
            break;
        }
        float t = (float)f / (float)frames;
        float e = t * t * (3.0f - 2.0f * t);
        int px = pigFrom + (int)((float)(pigTo - pigFrom) * e);
        Avatar::setX(px);
        Avatar::setManualWalk(true);
        Avatar::draw(splash);

        int trail = px - 8;
        splash.setFont(&fonts::Font0);
        splash.setTextSize(2);
        splash.setTextDatum(TL_DATUM);
        splash.setTextColor(UiStyle::GOLD);
        for (int i = 0; i < n; i++) {
            float wave = sinf((float)f * 0.38f + (float)i * 0.85f) * 3.5f;
            int lx = trail - (n - 1 - i) * 14;
            int ly = 34 + (int)wave;
            splash.drawChar(lx, ly, (uint16_t)(uint8_t)kTitle[i]);
        }

        splash.setTextSize(1);
        splash.setTextColor(UiStyle::DIM);
        splash.drawString(Board::modelLabel(), 6, 4);
        splash.setTextColor(UiStyle::TEXT);
        char sub[24];
        snprintf(sub, sizeof(sub), "v%s", ON3PORK_VERSION);
        int vw = splash.textWidth(sub);
        splash.drawString(sub, DISPLAY_W - vw - 6, 4);
        splash.setTextDatum(TL_DATUM);
        splash.pushSprite(0, 0);
        delay(28);
        yield();
    }

    if (!skipped) delay(200);
    finish();
}
