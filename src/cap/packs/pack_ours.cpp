// "OURS" pack — light/quiet tuning paired with the OURS capture method.
// All knobs below equal Preset{}'s own defaults; spelled out explicitly
// so the file is a complete, copy-pasteable template for a new pack.
#include "pack_ctx.h"

namespace Cap {
namespace Packs {

static const Preset kOursPreset{
    /* bidirKick  */ false,
    /* eapolTx    */ false,
    /* pmkidProbe */ false,
    /* csaHerd    */ false,
    /* authFlood  */ false,
    /* kickBurst  */ 2,
    /* pauseMs    */ 1200,
    /* lockMs     */ 8000,
    /* hopMs      */ 300,
};

CAP_PACK_REGISTER(ours, "OURS", "OURS", kOursPreset)

} // namespace Packs
} // namespace Cap
