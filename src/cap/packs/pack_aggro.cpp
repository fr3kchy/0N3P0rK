// "AGGRO" pack - the "kill it with fire" knob set. Everything on, shortest
// sane pause, fastest hop. Pairs with the PORKCHOP method so the score-
// and-focus loop has the most permissive parameters; it still picks ONE
// target per tick so the airtime budget stays sane, but each kick is a
// 5-frame bidirectional burst and we hop every 150 ms (~7 channels/sec).
//
// Intended for short, loud sessions on known-busy channels. You'll be
// *very* visible to anything with a WIDS. Porkchop calls this the
// 'rowdy' preset; we call it AGGRO.
#include "pack_ctx.h"

namespace Cap {
namespace Packs {

static const Preset kAggroPreset{
    /* bidirKick  */ true,
    /* eapolTx    */ true,
    /* pmkidProbe */ true,
    /* csaHerd    */ true,    // herd stragglers onto whichever channel we want
    /* authFlood  */ true,    // fall back to auth spam if nothing responds
    /* kickBurst  */ 5,       // 5 frames per burst (5 deauth+5 disassoc per leg)
    /* pauseMs    */ 500,     // 0.5s between bursts on the same target
    /* lockMs     */ 10000,   // 10s lock-on-BSSID
    /* hopMs      */ 150,     // ~7 channels/sec
};

CAP_PACK_REGISTER(aggro, "AGGRO", "PORKCHOP", kAggroPreset)

} // namespace Packs
} // namespace Cap
