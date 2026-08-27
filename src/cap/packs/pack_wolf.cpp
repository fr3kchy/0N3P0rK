// "WOLF" pack - the predator preset: everything PAN does, plus CSA
// herding and auth-flood, on top of the shortest possible pause and the
// fastest hop. Pairs with the PAN capture method (so it gets the full
// PMKID+EAPOL+bidir kick stack already bundled in PAN's method).
//
// Name is a nod to src/piglet/wolf.cpp - this is the "big bad" knob set
// to the OURS/PAN "three little pigs" packs in the radio menu.
#include "pack_ctx.h"

namespace Cap {
namespace Packs {

static const Preset kWolfPreset{
    /* bidirKick  */ true,   // kick in both directions
    /* eapolTx    */ true,   // pump EAPOL-Start/Logoff
    /* pmkidProbe */ true,   // try to coax a PMKID
    /* csaHerd    */ true,   // new: broadcast CSA to move clients
    /* authFlood  */ true,   // new: hammer the AP with auth frames
    /* kickBurst  */ 5,      // up from PAN's 3 - 5 frames per burst
    /* pauseMs    */ 600,    // down from PAN's 1500 - 0.6s between bursts
    /* lockMs     */ 12000,  // a bit longer than PAN's 10s - ride out retries
    /* hopMs      */ 150,    // down from PAN's 250 - ~6-7 channels/sec
};

CAP_PACK_REGISTER(wolf, "LOUD", "CLIENTS", kWolfPreset)

} // namespace Packs
} // namespace Cap