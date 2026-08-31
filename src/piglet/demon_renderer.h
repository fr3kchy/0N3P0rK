#pragma once

#include <M5Unified.h>
#include "../piglet/avatar.h"

namespace DemonRenderer {

void draw(M5Canvas& canvas, int16_t feetX, int16_t feetY,
          AvatarState state, bool faceRight, bool blink, bool sniff,
          uint8_t sniffPhase, bool hornPerk, bool tailAlt, bool jumping,
          uint16_t deadBlend);

}  // namespace DemonRenderer
