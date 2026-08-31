#include "demon_renderer.h"

#include "../core/config.h"
#include "weather.h"
#include <stdint.h>

namespace DemonRenderer {

static constexpr int16_t PX = 3;

struct Palette {
    uint16_t outline, body, highlight, shadow, horn, wing, eye, pupil, mouth, hoof, blush;
};

// Existing on-disk skin indices are preserved; only their visual interpretation changes.
static const Palette PALETTES[PIG_SKIN_COUNT] = {
    {0x3004,0xD124,0xFA49,0x780E,0xFFE0,0xA00F,0xFFF0,0x0000,0xFFFF,0x2104,0xF98C}, // CRIMSON
    {0x5000,0xF3A0,0xFD20,0xB180,0xFFF3,0xE200,0xFFFF,0x4100,0xFFFF,0x4208,0xFBE0}, // EMBER
    {0x2104,0x8410,0xBDF7,0x4A49,0xE73C,0x632C,0x07FF,0x0000,0xFFFF,0x3186,0xA514}, // ASH
    {0x1803,0x5B8C,0x9D34,0x3267,0xC618,0x4A69,0x5FEA,0xF800,0xFFE0,0x2104,0x7BEF}, // UNDEAD
    {0x2104,0x9CF3,0xD69A,0x528A,0xFFFF,0x7BEF,0xFFFF,0x0000,0xFFFF,0x3186,0xBDF7}, // RETRO
    {0x0000,0x3186,0x6B4D,0x18C3,0x9CF3,0x2104,0xFD20,0xFFFF,0xF800,0x1082,0x632C}, // SHADOW
    {0x780F,0xF81F,0xFD9F,0xB00F,0xFFFF,0xD01F,0x07FF,0x0000,0xFFFF,0x5809,0xFCB8}, // CANDY
    {0x8200,0xFE60,0xFFF1,0xC480,0xFFFF,0xD5A0,0xFFFF,0x4100,0xF800,0x5140,0xFCC0}, // GOLD
    {0x3100,0x8A85,0xC548,0x5942,0xD69A,0x7202,0xFE60,0x2104,0xFFFF,0x3186,0x8B43}, // DUST
};

void draw(M5Canvas& canvas, int16_t ox, int16_t oy,
          AvatarState state, bool faceRight, bool blink, bool sniff,
          uint8_t sniffPhase, bool hornPerk, bool tailAlt, bool jumping,
          uint16_t deadBlend) {
    uint8_t skin = Config::personality().pigSkin;
    if (skin >= PIG_SKIN_COUNT) skin = 0;
    Palette p = PALETTES[skin];
    if (Avatar::isThunderFlashing()) {
        auto invert = [](uint16_t c) -> uint16_t { return (uint16_t)(~c); };
        p.outline = invert(p.outline); p.body = invert(p.body);
        p.highlight = invert(p.highlight); p.shadow = invert(p.shadow);
        p.horn = invert(p.horn); p.eye = invert(p.eye);
        p.pupil = invert(p.pupil); p.mouth = invert(p.mouth);
        p.blush = invert(p.blush);
    }

    auto pixel = [&](int lx, int ly, uint16_t col) {
        const int x = faceRight ? ox + lx * PX : ox - (lx + 1) * PX;
        const int normalY = oy + ly * PX;
        const int deadY = oy - (ly + 20) * PX;
        const int y = normalY + ((deadY - normalY) * (int)deadBlend) / 256;
        if (x < -PX || x >= 240 || y < -PX || y >= 135) return;
        canvas.fillRect(x, y, PX, PX, col);
    };
    auto block = [&](int x, int y, int w, int h, uint16_t col) {
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx) pixel(x + xx, y + yy, col);
    };

    const bool happy = state == AvatarState::HAPPY;
    const bool excited = state == AvatarState::EXCITED || jumping;
    const bool sleepy = state == AvatarState::SLEEPY;
    const bool sad = state == AvatarState::SAD;
    const bool angry = state == AvatarState::ANGRY;
    const bool hunting = state == AvatarState::HUNTING;
    const bool wet = Weather::isRaining();
    const int hornLift = wet ? 1 : (hornPerk ? -1 : 0);
    const int step = tailAlt ? 1 : 0;

    // Ground shadow retains the original footprint and motion anchor.
    if (deadBlend < 200) {
        for (int dx = -8; dx <= 8; dx += 2) canvas.drawPixel(ox + dx * 2, oy + 1, p.shadow);
    }

    // Arrow-tipped tail, animated by the existing tail-wiggle/walk phase.
    pixel(-9, -10 + step, p.outline); pixel(-10, -11 + step, p.body);
    pixel(-11, -12 - step, p.body); pixel(-12, -12 - step, p.outline);
    pixel(-13, -13 - step, p.outline); pixel(-14, -12 - step, p.outline);
    pixel(-13, -11 - step, p.outline); pixel(-12, -12 - step, p.body);

    // Compact chibi body: same feet centre and approximate collision footprint.
    block(-7, -13, 14, 1, p.outline);
    block(-8, -12, 16, 7, p.outline);
    block(-7, -12, 14, 7, p.body);
    block(-5, -12, 7, 2, p.highlight);
    block(-6, -7, 12, 2, p.shadow);

    // Tiny folded bat wings.
    pixel(-8, -11, p.outline); pixel(-9, -12, p.wing); pixel(-10, -13, p.outline);
    pixel(-9, -10, p.wing); pixel(-10, -9, p.outline);
    pixel(7, -11, p.outline); pixel(8, -12, p.wing); pixel(9, -13, p.outline);
    pixel(8, -10, p.wing); pixel(9, -9, p.outline);

    // Head and pointed ears.
    block(-5, -18, 11, 1, p.outline);
    block(-6, -17, 13, 8, p.outline);
    block(-5, -17, 11, 8, p.body);
    block(-3, -17, 5, 2, p.highlight);
    pixel(-7, -16, p.outline); pixel(-8, -17, p.outline); pixel(-7, -15, p.body);
    pixel(7, -16, p.outline); pixel(8, -17, p.outline); pixel(7, -15, p.body);

    // Clearly separated ivory horns. Ear-twitch state raises both tips.
    pixel(-4, -19 + hornLift, p.outline); pixel(-4, -20 + hornLift, p.horn);
    pixel(-3, -21 + hornLift, p.horn); pixel(-2, -20 + hornLift, p.outline);
    pixel(4, -19 + hornLift, p.outline); pixel(4, -20 + hornLift, p.horn);
    pixel(3, -21 + hornLift, p.horn); pixel(2, -20 + hornLift, p.outline);

    // Expressive eyes preserve blink, sleepy, sad, angry, hunting and excited states.
    int eyeY = -15;
    if (blink || sleepy) {
        block(-3, eyeY, 2, 1, p.outline); block(2, eyeY, 2, 1, p.outline);
    } else {
        pixel(-3, eyeY - 1, p.outline); pixel(-2, eyeY - 1, p.eye);
        pixel(-3, eyeY, p.eye); pixel(-2, eyeY, p.pupil);
        pixel(2, eyeY - 1, p.eye); pixel(3, eyeY - 1, p.outline);
        pixel(2, eyeY, p.pupil); pixel(3, eyeY, p.eye);
        if (excited) { pixel(-2, eyeY - 2, p.eye); pixel(2, eyeY - 2, p.eye); }
        if (sad) { pixel(-4, eyeY - 2, p.outline); pixel(3, eyeY - 2, p.outline); }
        if (angry || hunting) { pixel(-3, eyeY - 2, p.outline); pixel(2, eyeY - 2, p.outline); }
    }

    // Nose flare reuses sniff timing without changing the animation state machine.
    const int sniffPush = sniff ? (sniffPhase == 1 ? 1 : 0) : 0;
    pixel(0 + sniffPush, -13, p.outline);
    if (sniff) { pixel(-1 + sniffPush, -13, p.mouth); pixel(1 + sniffPush, -13, p.mouth); }

    // Mischievous mouth/fangs.
    if (sad) {
        pixel(-1, -11, p.outline); pixel(0, -12, p.outline); pixel(1, -11, p.outline);
    } else if (angry) {
        block(-2, -12, 5, 1, p.outline); pixel(-1, -11, p.mouth); pixel(1, -11, p.mouth);
    } else if (excited) {
        block(-2, -12, 5, 2, p.outline); pixel(-1, -11, p.mouth); pixel(1, -11, p.mouth);
    } else {
        pixel(-2, -12, p.outline); pixel(-1, -11, p.outline); pixel(0, -11, p.outline);
        pixel(1, -11, p.outline); pixel(2, -12, p.outline);
        if (happy || hunting) { pixel(-1, -12, p.mouth); pixel(1, -12, p.mouth); }
    }
    pixel(-2, -11, p.horn); pixel(2, -11, p.horn);
    if (happy || excited) { pixel(-5, -12, p.blush); pixel(5, -12, p.blush); }

    // Cloven feet retain the original walk cadence and ground contact.
    auto foot = [&](int x, int lift) {
        block(x, -5, 3, 3 + lift, p.body);
        pixel(x, -2 + lift, p.hoof); pixel(x + 2, -2 + lift, p.hoof);
        pixel(x + 1, -1 + lift, p.outline);
    };
    foot(-6, 1 - step); foot(-2, step); foot(2, 1 - step); foot(6, step);
}

}  // namespace DemonRenderer
