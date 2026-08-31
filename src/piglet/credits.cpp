#include "credits.h"
#include "../audio/sfx.h"
#include <string.h>

namespace Credits {

static bool s_on = false;
static uint32_t s_start = 0;
static constexpr uint32_t DURATION_MS = 10000;

static const char* lines[] = {
    "THANK YOU",
    "FOR PLAYING",
    "0N3P0rK",
    "THANKS OCT0SEC",
    "FOR THE SPARK",
    "I TRIED MY BEST",
    "FOR YOU",
    "NEW PROJECTS SOON",
    "STAY CURIOUS",
    "OINK OINK",
};
static constexpr int LINE_N = 10;

void begin() { s_on = false; }

void start() {
    s_on = true;
    s_start = millis();
    SFX::play(SFX::LEVEL_UP);
}

bool isPlaying() {
    if (!s_on) return false;
    if ((millis() - s_start) >= DURATION_MS) {
        s_on = false;
        return false;
    }
    return true;
}

void update() { (void)isPlaying(); }

void draw(M5Canvas& canvas) {
    if (!isPlaying()) return;
    uint32_t elapsed = millis() - s_start;
    int idx = (int)((elapsed * LINE_N) / DURATION_MS);
    if (idx >= LINE_N) idx = LINE_N - 1;

    canvas.fillSprite(0x0000);
    canvas.setTextColor(0xFFE0);
    canvas.setTextSize(2);
    const char* a = lines[idx];
    int16_t tw = (int16_t)(strlen(a) * 12);
    canvas.setCursor((240 - tw) / 2, 52);
    canvas.print(a);

    canvas.setTextSize(1);
    canvas.setTextColor(0x8410);
    canvas.setCursor(70, 100);
    canvas.print("CREDITS");
    // progress bar
    int bar = (int)((elapsed * 200) / DURATION_MS);
    if (bar > 200) bar = 200;
    canvas.fillRect(20, 120, bar, 3, 0x07E0);
}

}  // namespace Credits
