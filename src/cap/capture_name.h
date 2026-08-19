// cap/capture_name.h
// Capture names: SSID_BSSID.pcap / _hs.22000. No sidecar .txt.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

namespace CapName {

void sanitizeSsid(const char* ssid, char* out, size_t outLen);
void buildStem(const char* ssid, const uint8_t bssid[6], char* out, size_t outLen);
void prettyMac(const char* hex12, char out[18]);

bool extractBssidHex(const char* name, char hex13[13]);
bool extractSsidFromName(const char* name, char ssid[33]);
bool readCompanionSsid(const char* dir, const char* captureName, char ssid[33]);
void writeCompanionSsid(const char* dir, const char* stemOrName, const char* ssid);
bool ssidFromMgmt(const uint8_t* frame, uint16_t len, char ssid[33]);
bool metaFrom22000Line(const char* line, char hex13[13], char ssid[33]);
bool metaFrom22000File(const char* dir, const char* name, char hex13[13], char ssid[33]);
bool hexToMac(const char* hex12, uint8_t out[6]);
bool sameSsid(const char* a, const char* b);

} // namespace CapName
