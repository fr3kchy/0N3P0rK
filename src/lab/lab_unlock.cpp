// fR3k v3 lab unlock gate implementation.
// Runtime gate behind the v2 safe build. Persists `unlocked` (0/1) and
// `mask` (uint8) in NVS namespace "fr3klab". Unlock by sha-1 match against
// the compiled-in hash; hash itself is baked from "666" - see lab_unlock.h.

#include "lab_unlock.h"
#include <Preferences.h>
#include <string.h>
#include <stdio.h>

namespace Lab {

static bool s_unlocked = false;
static uint8_t s_mask = 0;
static Preferences s_prefs;

static const char* nvsNamespace() { return "fr3klab"; }

// Compiled-in sha-1("666"). The verify script (`tools/lab_unlock_check.py`)
// re-derives this from a re-implementation of the in-firmware sha1() so
// any drift is caught at CI time. Lower-case hex; 40 chars + NUL.
const char kUnlockHash[41] =
    "cd3f0c85b158c08a2b113464991810cf2cdfc387";

void begin() {
    if (s_prefs.begin(nvsNamespace(), false)) {
        s_unlocked = s_prefs.getUChar("unlock", 0) != 0;
        s_mask = s_prefs.getUChar("mask", 0);
        s_prefs.end();
    } else {
        // NVS unreachable; fall back to a clean locked state so the menu
        // refuses every offensive tool. Better than false-positives.
        s_unlocked = false;
        s_mask = 0;
    }
}

bool isUnlocked() { return s_unlocked; }
uint8_t toolMask() { return s_mask; }

bool isToolEnabled(Tool t) {
    if (t >= TOOL_COUNT) return false;
    if (!s_unlocked) return false;
    return (s_mask & (1u << t)) != 0;
}

const char* toolName(Tool t) {
    switch (t) {
        case TOOL_LIGHT:    return "LIGHT";
        case TOOL_AGGRO:    return "AGG";
        case TOOL_EVILPIG:  return "EP";
        case TOOL_PIGPASS:  return "PP";
        case TOOL_BLE:      return "BLE";
        case TOOL_IR:       return "IR";
        case TOOL_SPECTRUM: return "SPEC";
        case TOOL_LOOT:     return "LOOT";
        default:            return "?";
    }
}

void formatActiveMask(char* out, size_t len) {
    if (!out || len == 0) return;
    out[0] = '\0';
    if (!s_unlocked) {
        snprintf(out, len, "LOCKED");
        return;
    }
    size_t off = 0;
    for (uint8_t i = 0; i < TOOL_COUNT; i++) {
        if (!(s_mask & (1u << i))) continue;
        const char* tag = toolName((Tool)i);
        if (off == 0) {
            off += snprintf(out + off, len - off, "%s", tag);
        } else {
            off += snprintf(out + off, len - off, " %s", tag);
        }
        if (off >= len - 1) break;
    }
    if (off == 0) snprintf(out, len, "NONE");
}

void lock() {
    s_unlocked = false;
    s_mask = 0;
    if (s_prefs.begin(nvsNamespace(), false)) {
        s_prefs.putUChar("unlock", 0);
        s_prefs.putUChar("mask", 0);
        s_prefs.end();
    }
}

void setTool(Tool t, bool on) {
    if (t >= TOOL_COUNT) return;
    if (!s_unlocked) return;  // nothing to toggle while locked
    uint8_t bit = (uint8_t)(1u << t);
    if (on) s_mask |= bit;
    else    s_mask = (uint8_t)(s_mask & ~bit);
    if (s_prefs.begin(nvsNamespace(), false)) {
        s_prefs.putUChar("mask", s_mask);
        s_prefs.end();
    }
}

// Build the canonical lowercase hex of an MD5/sha-1 digest into out. The
// Arduino mbedtls wrapper exposes mbedtls_md5/sha1 via a context API; we
// use the same path as the rest of the codebase: mbedtls_md5_update()
// isn't available without pulling a header, so we hand-roll a tiny
// SHA-1 in pure C99 here instead - keeps the lab_unlock pair dependency-
// free and lets the verify script re-implement the same hash in Python.
//
// SHA-1 implementation below is the classic 32-bit little-endian version
// from RFC 3174 / FIPS 180-4, used in countless firmware projects. Public
// domain reference. Output is 20 raw bytes -> caller encodes to hex.
static const uint32_t kSha1K[4] = { 0x5A827999u, 0x6ED9EBA1u, 0x8F1BBCDCu, 0xCA62C1D6u };

static uint32_t rol(uint32_t x, uint8_t n) { return (x << n) | (x >> (32 - n)); }

static void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
    uint32_t h0 = 0x67452301u, h1 = 0xEFCDAB89u, h2 = 0x98BADCFEu,
             h3 = 0x10325476u, h4 = 0xC3D2E1F0u;
    // Pre-processing: append 0x80, pad with zeros until length % 64 == 56,
    // then append the 64-bit big-endian bit length.
    uint8_t block[64];
    uint32_t w[80];
    size_t total = len;
    uint64_t bitlen = (uint64_t)len * 8u;
    // Process each complete 64-byte block from the input.
    size_t i = 0;
    while (i + 64 <= len) {
        for (int j = 0; j < 16; j++) {
            w[j] = ((uint32_t)data[i + j * 4 + 0] << 24) |
                   ((uint32_t)data[i + j * 4 + 1] << 16) |
                   ((uint32_t)data[i + j * 4 + 2] << 8) |
                   ((uint32_t)data[i + j * 4 + 3]);
        }
        for (int j = 16; j < 80; j++) {
            w[j] = rol(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int j = 0; j < 80; j++) {
            uint32_t f, k;
            if (j < 20)      { f = (b & c) | ((~b) & d);           k = kSha1K[0]; }
            else if (j < 40) { f = b ^ c ^ d;                      k = kSha1K[1]; }
            else if (j < 60) { f = (b & c) | (b & d) | (c & d);    k = kSha1K[2]; }
            else             { f = b ^ c ^ d;                      k = kSha1K[3]; }
            uint32_t t = rol(a, 5) + f + e + k + w[j];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
        i += 64;
    }
    // Final block: copy remaining bytes + append 0x80 + zero pad + length.
    size_t rem = total - i;
    memset(block, 0, sizeof(block));
    if (rem) memcpy(block, data + i, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        // No room for length - process this block then start another.
        for (int j = 0; j < 16; j++) {
            w[j] = ((uint32_t)block[j * 4 + 0] << 24) |
                   ((uint32_t)block[j * 4 + 1] << 16) |
                   ((uint32_t)block[j * 4 + 2] << 8) |
                   ((uint32_t)block[j * 4 + 3]);
        }
        for (int j = 16; j < 80; j++) {
            w[j] = rol(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int j = 0; j < 80; j++) {
            uint32_t f, k;
            if (j < 20)      { f = (b & c) | ((~b) & d);           k = kSha1K[0]; }
            else if (j < 40) { f = b ^ c ^ d;                      k = kSha1K[1]; }
            else if (j < 60) { f = (b & c) | (b & d) | (c & d);    k = kSha1K[2]; }
            else             { f = b ^ c ^ d;                      k = kSha1K[3]; }
            uint32_t t = rol(a, 5) + f + e + k + w[j];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
        memset(block, 0, sizeof(block));
    }
    // Append 64-bit big-endian bit length.
    block[56] = (uint8_t)(bitlen >> 56);
    block[57] = (uint8_t)(bitlen >> 48);
    block[58] = (uint8_t)(bitlen >> 40);
    block[59] = (uint8_t)(bitlen >> 32);
    block[60] = (uint8_t)(bitlen >> 24);
    block[61] = (uint8_t)(bitlen >> 16);
    block[62] = (uint8_t)(bitlen >> 8);
    block[63] = (uint8_t)(bitlen);
    for (int j = 0; j < 16; j++) {
        w[j] = ((uint32_t)block[j * 4 + 0] << 24) |
               ((uint32_t)block[j * 4 + 1] << 16) |
               ((uint32_t)block[j * 4 + 2] << 8) |
               ((uint32_t)block[j * 4 + 3]);
    }
    for (int j = 16; j < 80; j++) {
        w[j] = rol(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1);
    }
    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (int j = 0; j < 80; j++) {
        uint32_t f, k;
        if (j < 20)      { f = (b & c) | ((~b) & d);           k = kSha1K[0]; }
        else if (j < 40) { f = b ^ c ^ d;                      k = kSha1K[1]; }
        else if (j < 60) { f = (b & c) | (b & d) | (c & d);    k = kSha1K[2]; }
        else             { f = b ^ c ^ d;                      k = kSha1K[3]; }
        uint32_t t = rol(a, 5) + f + e + k + w[j];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    // Emit big-endian digest.
    out[0]  = (uint8_t)(h0 >> 24); out[1]  = (uint8_t)(h0 >> 16);
    out[2]  = (uint8_t)(h0 >> 8);  out[3]  = (uint8_t)(h0);
    out[4]  = (uint8_t)(h1 >> 24); out[5]  = (uint8_t)(h1 >> 16);
    out[6]  = (uint8_t)(h1 >> 8);  out[7]  = (uint8_t)(h1);
    out[8]  = (uint8_t)(h2 >> 24); out[9]  = (uint8_t)(h2 >> 16);
    out[10] = (uint8_t)(h2 >> 8);  out[11] = (uint8_t)(h2);
    out[12] = (uint8_t)(h3 >> 24); out[13] = (uint8_t)(h3 >> 16);
    out[14] = (uint8_t)(h3 >> 8);  out[15] = (uint8_t)(h3);
    out[16] = (uint8_t)(h4 >> 24); out[17] = (uint8_t)(h4 >> 16);
    out[18] = (uint8_t)(h4 >> 8);  out[19] = (uint8_t)(h4);
}

static void toHex(const uint8_t* bin, size_t n, char* out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2 + 0] = hex[(bin[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[bin[i] & 0x0F];
    }
    out[n * 2] = '\0';
}

bool unlock(const char* ascii) {
    if (!ascii || !ascii[0]) return false;
    size_t n = strlen(ascii);
    uint8_t digest[20];
    char hex[41];
    sha1((const uint8_t*)ascii, n, digest);
    toHex(digest, 20, hex);
    // String compare is case-insensitive (kUnlockHash is lower-case but
    // tolerate operator typos on hex chars).
    for (size_t i = 0; i < 40; i++) {
        char a = hex[i];
        char b = kUnlockHash[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    s_unlocked = true;
    s_mask = TOOL_MASK_ALL;
    if (s_prefs.begin(nvsNamespace(), false)) {
        s_prefs.putUChar("unlock", 1);
        s_prefs.putUChar("mask", s_mask);
        s_prefs.end();
    }
    return true;
}

}  // namespace Lab