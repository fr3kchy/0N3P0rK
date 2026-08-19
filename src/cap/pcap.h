// cap/pcap.h
// PCAP classic file format (LINKTYPE_IEEE802_11_RADIOTAP = 127).
// Compatible with wpa-sec.stanev.org, pwncrack, hcxpcapng, hashcat -m 22000.
//
// Each saved .pcap file has:
//   1 x PCAP file header (24 bytes)
//   N x (PCAP packet header (16 bytes) + radiotap header (8 bytes) + 802.11 frame)
//
// We always write radiotap in front of the raw 802.11 frame because the
// promiscuous callback gives us the 802.11 frame without any radiotap, and
// a lot of tools (hcxpcapng, tshark) expect it.

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

namespace Cap {
namespace Pcap {

// Magic, version, snaplen 65535, linktype 127 (IEEE 802.11 + radiotap).
struct __attribute__((packed)) FileHeader {
    uint32_t magic;        // 0xA1B2C3D4
    uint16_t versionMajor; // 2
    uint16_t versionMinor; // 4
    int32_t  thiszone;     // 0
    uint32_t sigfigs;      // 0
    uint32_t snaplen;      // 65535
    uint32_t linktype;     // 127
};

// Per-packet header. ts is millis() since boot.
struct __attribute__((packed)) PacketHeader {
    uint32_t tsSec;
    uint32_t tsUsec;
    uint32_t inclLen;     // captured length
    uint32_t origLen;     // original length
};

// Minimal radiotap header (8 bytes). No optional fields.
static const uint8_t RADIOTAP_HEADER[8] = {
    0x00, 0x00,
    0x08, 0x00,
    0x00, 0x00, 0x00, 0x00
};

static const size_t RADIOTAP_LEN = sizeof(RADIOTAP_HEADER);
static const size_t RADIOTAP_FAT_LEN = 16;

// FLAGS + RATE + CHANNEL + DBM_ANTSIGNAL. wpa-sec / hcxpcapng like this.
inline uint8_t buildRadiotap(uint8_t* out, uint8_t ch, int8_t rssi, bool fat) {
    if (!out) return 0;
    if (!fat) {
        memcpy(out, RADIOTAP_HEADER, RADIOTAP_LEN);
        return (uint8_t)RADIOTAP_LEN;
    }
    uint32_t present = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 5);
    out[0] = 0;
    out[1] = 0;
    out[2] = (uint8_t)RADIOTAP_FAT_LEN;
    out[3] = 0;
    memcpy(out + 4, &present, 4);
    out[8] = 0x00;   // flags: no FCS at end
    out[9] = 0x02;   // rate 1 Mbps
    uint16_t freq = (ch == 14) ? 2484 : (uint16_t)(2407 + 5 * ch);
    out[10] = (uint8_t)(freq & 0xFF);
    out[11] = (uint8_t)(freq >> 8);
    out[12] = 0xA0;  // 2 GHz + CCK
    out[13] = 0x00;
    out[14] = (uint8_t)rssi;
    out[15] = 0;
    return (uint8_t)RADIOTAP_FAT_LEN;
}

} // namespace Pcap
} // namespace Cap