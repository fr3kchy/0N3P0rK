// "PORKCHOP" pack - the Porkchop-style tuning paired with the PORKCHOP
// capture method: focused single-target bursts (handled inside the method
// itself via scoring), bidirectional kick, EAPOL-Start/Logoff, PMKID probe,
// the shortest sane pause, and a long lock-on-BSSID to ride out M2/M3/M4
// retries before hopping away.
//
// Mirrors M5PORKCHOP's OINK defaults, adapted to our sniffer's timing knobs.
// Pairing this pack with the PORKCHOP method is the recommended setup; it
// also works fine with the PAN method if a user prefers PAN's per-client
// kick loop over PORKCHOP's score-and-focus loop.
#include "pack_ctx.h"

namespace Cap {
namespace Packs {

static const Preset kPorkchopPreset{
    /* bidirKick  */ true,   // AP->STA + STA->AP + deauth + disassoc
    /* eapolTx    */ true,   // pump EAPOL-Start/Logoff
    /* pmkidProbe */ true,   // try to coax a PMKID
    /* csaHerd    */ false,  // not part of PORKCHOP's defaults
    /* authFlood  */ false,  // not part of PORKCHOP's defaults
    /* kickBurst  */ 3,      // 3 frames per burst (matches Porkchop)
    /* pauseMs    */ 900,    // under 1s between bursts
    /* lockMs     */ 15000,  // 15s lock-on-BSSID - well past any EAPOL retry
    /* hopMs      */ 250,    // ~4 channels/sec
};

CAP_PACK_REGISTER(porkchop, "FOCUS", nullptr, kPorkchopPreset)

} // namespace Packs
} // namespace Cap
