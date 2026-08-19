// sync/sync_manager.cpp
#include "sync_manager.h"
#include "wpasec.h"
#include "pwncrack.h"
#include "../net/ap_sta.h"
#include "../cap/sniffer.h"
#include <WiFi.h>
#include <string.h>

namespace SyncManager {

enum Phase {
    PHASE_IDLE,
    PHASE_SWITCHING_TO_STA,
    PHASE_WAITING_FOR_CONNECTION,
    PHASE_UPLOADING,
    PHASE_SWITCHING_TO_AP,
    PHASE_DONE
};

static struct {
    bool running;
    bool apSwitchSent;
    bool staSwitchSent;
    bool wantApAfter;
    Phase phase;
    SyncTarget target;
    SyncStatus status;
    char message[128];
    char apiKey[65];
    int progress;
    unsigned long lastStateChange;
    unsigned long timeout;
} s_state;

static void setMessage(const char* msg) {
    if (!msg) {
        s_state.message[0] = '\0';
        return;
    }
    strncpy(s_state.message, msg, sizeof(s_state.message) - 1);
    s_state.message[sizeof(s_state.message) - 1] = '\0';
}

static void enterPhase(Phase p, SyncStatus st, const char* msg, int progress, unsigned long timeoutMs) {
    s_state.phase = p;
    s_state.status = st;
    if (msg) setMessage(msg);
    s_state.progress = progress;
    s_state.lastStateChange = millis();
    s_state.timeout = millis() + timeoutMs;
}

void start(SyncTarget target, const char* apiKey) {
    if (s_state.running) return;
    if (!Net::hasStaCreds()) {
        s_state.running = false;
        s_state.phase = PHASE_IDLE;
        s_state.status = SYNC_DONE_FAILURE;
        setMessage("No home WiFi saved");
        s_state.progress = 0;
        return;
    }

    if (Cap::isRunning()) Cap::stop();

    memset(&s_state, 0, sizeof(s_state));
    s_state.running = true;
    s_state.target = target;
    // Keep 0n3Pork W3b up. APSTA lets the phone stay.
    s_state.wantApAfter = false;
    strncpy(s_state.apiKey, apiKey ? apiKey : "", sizeof(s_state.apiKey) - 1);

    if (Net::staLinked()) {
        enterPhase(PHASE_UPLOADING, SYNC_UPLOADING, "Uploading...", 40, 180000);
    } else if (Net::cfg().mode == Net::Mode::APSTA) {
        // AP+STA already on, just wait for the link to come up.
        // Some firmwares need an explicit WiFi.begin() after mode switch.
        if (Net::cfg().staSsid[0]) {
            WiFi.begin(Net::cfg().staSsid, Net::cfg().staPass);
        }
        enterPhase(PHASE_WAITING_FOR_CONNECTION, SYNC_CONNECTING,
                   "Waiting for WiFi...", 20, 45000);
    } else {
        enterPhase(PHASE_SWITCHING_TO_STA, SYNC_CONNECTING,
                   "Joining WiFi, AP stays up...", 5, 30000);
    }
    Serial.printf("[SYNC] start %s\n", target == SYNC_WPASEC ? "wpa-sec" : "pwncrack");
}

void stop() {
    if (!s_state.running && s_state.phase == PHASE_IDLE) return;
    s_state.running = false;
    s_state.phase = PHASE_IDLE;
    s_state.status = SYNC_IDLE;
    s_state.message[0] = '\0';
    (void)0;
    Serial.println("[SYNC] cancelled");
}

bool isRunning() {
    return s_state.running;
}

SyncState getStatus() {
    SyncState out;
    out.status = s_state.status;
    out.progress = s_state.progress;
    strncpy(out.message, s_state.message, sizeof(out.message) - 1);
    out.message[sizeof(out.message) - 1] = '\0';
    return out;
}

static void runUpload() {
    // One last safety net: if we got here without a real STA link, try
    // joinHome() once before paying the TLS cost.
    if (!Net::staLinked() && Net::hasStaCreds()) {
        Serial.println("[SYNC] pre-upload: STA not linked, trying joinHome()");
        if (!Net::joinHome(15000)) {
            setMessage("WiFi connect failed");
            enterPhase(PHASE_DONE, SYNC_DONE_FAILURE, nullptr, 100, 0);
            s_state.running = false;
            return;
        }
    }
    if (!Net::staLinked()) {
        setMessage("No WiFi link");
        enterPhase(PHASE_DONE, SYNC_DONE_FAILURE, nullptr, 100, 0);
        s_state.running = false;
        return;
    }

    bool ok = false;
    char msg[128];
    msg[0] = '\0';

    if (s_state.target == SYNC_WPASEC) {
        if (!WPASec::canSync()) {
            snprintf(msg, sizeof(msg), "Not enough heap for TLS");
        } else {
            WPASecSyncResult r = WPASec::syncCaptures(s_state.apiKey);
            ok = r.success;
            if (ok) {
                snprintf(msg, sizeof(msg), "WPA-Sec: up %u skip %u fail %u cracked %u",
                         r.uploaded, r.skipped, r.failed, r.cracked);
                if (r.error[0]) {
                    snprintf(msg, sizeof(msg), "WPA-Sec: up %u, %s", r.uploaded, r.error);
                }
            } else {
                snprintf(msg, sizeof(msg), "WPA-Sec: %s", r.error[0] ? r.error : "failed");
            }
        }
    } else {
        if (!Pwncrack::canSync()) {
            snprintf(msg, sizeof(msg), "Not enough heap");
        } else {
            PwncrackSyncResult r = Pwncrack::syncCaptures(s_state.apiKey);
            ok = r.success;
            if (ok) {
                snprintf(msg, sizeof(msg), "Pwncrack: up %u skip %u fail %u cracked %u",
                         r.uploaded, r.skipped, r.failed, r.cracked);
                if (r.error[0]) {
                    snprintf(msg, sizeof(msg), "Pwncrack: up %u, %s", r.uploaded, r.error);
                }
            } else {
                snprintf(msg, sizeof(msg), "Pwncrack: %s", r.error[0] ? r.error : "failed");
            }
        }
    }

    setMessage(msg);
    s_state.apSwitchSent = false;
    if (s_state.wantApAfter) {
        enterPhase(PHASE_SWITCHING_TO_AP,
                   ok ? SYNC_DONE_SUCCESS : SYNC_DONE_FAILURE,
                   nullptr, 90, 15000);
    } else {
        enterPhase(PHASE_DONE,
                   ok ? SYNC_DONE_SUCCESS : SYNC_DONE_FAILURE,
                   nullptr, 100, 0);
        s_state.running = false;
    }
}

void loop() {
    if (!s_state.running) return;

    unsigned long now = millis();
    Net::Status wifi = Net::status();

    switch (s_state.phase) {
        case PHASE_SWITCHING_TO_STA: {
            // OnePork's recipe: switch to APSTA first, give the link 5s.
            // If that fails, fall back to joinHome() which drops the AP
            // and goes pure STA. AP-only is never useful for sync.
            if (wifi.mode == Net::Mode::APSTA) {
                enterPhase(PHASE_WAITING_FOR_CONNECTION, SYNC_CONNECTING,
                           "Waiting for WiFi...", 20, 45000);
                break;
            }
            if (!s_state.staSwitchSent) {
                if (now - s_state.lastStateChange < 400) break;
                if (Net::setMode(Net::Mode::APSTA)) {
                    s_state.staSwitchSent = true;
                    s_state.lastStateChange = now;
                } else {
                    setMessage("AP+STA switch failed");
                    enterPhase(PHASE_DONE, SYNC_DONE_FAILURE, nullptr, 0, 0);
                    s_state.running = false;
                }
            } else if (now - s_state.lastStateChange > 5000 &&
                       !Net::staLinked()) {
                // APSTA path didn't link in 5s — drop the AP and go pure STA.
                Serial.println("[SYNC] APSTA link slow, falling back to joinHome()");
                if (Net::joinHome(15000)) {
                    enterPhase(PHASE_UPLOADING, SYNC_UPLOADING,
                               "Uploading...", 40, 180000);
                } else {
                    setMessage("WiFi connect failed");
                    enterPhase(PHASE_DONE, SYNC_DONE_FAILURE, nullptr, 0, 0);
                    s_state.running = false;
                }
            } else if (now > s_state.timeout) {
                setMessage("WiFi switch timeout");
                enterPhase(PHASE_DONE, SYNC_DONE_FAILURE, nullptr, 0, 0);
                s_state.running = false;
            }
            break;
        }

        case PHASE_WAITING_FOR_CONNECTION:
            if (Net::staLinked()) {
                enterPhase(PHASE_UPLOADING, SYNC_UPLOADING, "Uploading...", 40, 180000);
            } else if (now - s_state.lastStateChange > 12000 &&
                       !Net::staLinked()) {
                // 12s of APSTA waiting is plenty. Drop AP and join pure STA.
                Serial.println("[SYNC] APSTA wait long, falling back to joinHome()");
                if (Net::joinHome(15000)) {
                    enterPhase(PHASE_UPLOADING, SYNC_UPLOADING,
                               "Uploading...", 40, 180000);
                } else {
                    setMessage("WiFi connect failed");
                    enterPhase(PHASE_DONE, SYNC_DONE_FAILURE, nullptr, 0, 0);
                    s_state.running = false;
                }
            } else if (now > s_state.timeout) {
                setMessage("WiFi connect timeout");
                enterPhase(PHASE_DONE, SYNC_DONE_FAILURE, nullptr, 0, 0);
                s_state.running = false;
            }
            break;

        case PHASE_UPLOADING:
            runUpload();
            break;

        case PHASE_SWITCHING_TO_AP:
            if (wifi.mode == Net::Mode::AP) {
                enterPhase(PHASE_DONE, s_state.status, nullptr, 100, 0);
                s_state.running = false;
            } else if (!s_state.apSwitchSent) {
                Net::setMode(Net::Mode::AP);
                s_state.apSwitchSent = true;
            } else if (now > s_state.timeout) {
                Net::setMode(Net::Mode::AP);
                s_state.timeout = now + 10000;
            }
            break;

        case PHASE_DONE:
            s_state.running = false;
            break;

        default:
            break;
    }
}

} // namespace SyncManager
