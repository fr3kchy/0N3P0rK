// "PAN" pack — aggressive tuning paired with the PAN capture method:
// bidirectional kick, EAPOL-Start/Logoff, PMKID probe, tighter lock/hop.
#include "pack_ctx.h"

namespace Cap {
namespace Packs {

static const Preset kPanPreset{
    /* bidirKick  */ true,
    /* eapolTx    */ true,
    /* pmkidProbe */ true,
    /* csaHerd    */ false,
    /* authFlood  */ false,
    /* kickBurst  */ 3,
    /* pauseMs    */ 1500,
    /* lockMs     */ 10000,
    /* hopMs      */ 250,
};

CAP_PACK_REGISTER(pan, "NORMAL", nullptr, kPanPreset)

} // namespace Packs
} // namespace Cap
