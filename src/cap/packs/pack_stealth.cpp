// "STEALTH" pack - zero deauth. Only the PMKID probe runs (open auth +
// association), and only if the user opted into it in the radio menu. This
// is the radio-equivalent of DONOHAM mode: you sit on a channel, watch
// EAPOL traffic, and gently knock on doors with auth frames to coax a PMKID
// out of any AP that volunteers one in its M1.
//
// Pair it with PMKIDONLY or the default PORKCHOP method depending on
// whether you want active EAPOL harvest (PORKCHOP) or just the quiet
// passive collection that the dispatcher falls back to when bidirKick /
// authFlood are both off.
//
// hopMs is short because we want to sweep every channel quickly; pauseMs
// matches the PMKID probe interval so a probe actually fires between hops.
#include "pack_ctx.h"

namespace Cap {
namespace Packs {

static const Preset kStealthPreset{
    /* bidirKick  */ false,  // absolutely no broadcast/targeted deauth
    /* eapolTx    */ false,  // do not flood EAPOL-Start/Logoff
    /* pmkidProbe */ true,   // the whole point - coax PMKID out of M1
    /* csaHerd    */ false,  // clients don't need to be herded
    /* authFlood  */ false,  // no auth spam
    /* kickBurst  */ 1,      // ignored - no kick happens, kept for completeness
    /* pauseMs    */ 2000,   // >= pmkidProbe interval, gives the probe room
    /* lockMs     */ 8000,   // short - we don't really want to dwell
    /* hopMs      */ 400,    // sweep all channels in ~5s
};

CAP_PACK_REGISTER(stealth, "STEALTH", "PORKCHOP", kStealthPreset)

} // namespace Packs
} // namespace Cap
