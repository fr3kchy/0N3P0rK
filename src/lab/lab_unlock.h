// fR3k v3 lab unlock gate.
//
// Persists a single byte (0/1) in NVS namespace "fr3klab" plus an 8-bit
// per-tool bitmask. Unlock succeeds when the user-typed ASCII string hashes
// (sha-1) to kUnlockHash. The hash itself is baked at compile time from
// the literal password "666" - the firmware never stores the cleartext.
//
// The gate is runtime-only: the same `m5cardputer` binary is shipped as the
// distributable, but the menu refuses to expose the offensive tools until
// Lab::isUnlocked() returns true. A second env `m5cardputer-safe` keeps the
// legacy FR3K_SAFE_BUILD=1 shape for users who want the v2 strict default.
#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace Lab {

// 8 tool bits - add new ones by appending before TOOL_COUNT.
enum Tool : uint8_t {
    TOOL_LIGHT    = 0,   // passive channel listen (Cap::startLight)
    TOOL_AGGRO    = 1,   // aggressive hunt (Cap::startAggressive)
    TOOL_EVILPIG  = 2,   // lab portal (EvilPigMode)
    TOOL_PIGPASS  = 3,   // offline WPA lab (PigpassMode)
    TOOL_BLE      = 4,   // BLE frame spam (BlePigMode)
    TOOL_IR       = 5,   // IR blast (IrPortMode)
    TOOL_SPECTRUM = 6,   // 2.4 GHz spectrum view
    TOOL_LOOT     = 7,   // loot / wpa-sec / pwncrack access
    TOOL_COUNT    = 8,
};

// Lower-case ASCII hex of sha-1("666") (40 chars + NUL). Defined in the
// matching .cpp so a single source of truth exists; the verify script
// (`tools/lab_unlock_check.py`) re-derives this from a re-implementation
// of the in-firmware sha1() to catch drift.
extern const char kUnlockHash[41];

// Bitmask helpers
static constexpr uint8_t TOOL_MASK_ALL = 0xFFu;

// Load any persisted state from NVS. Safe to call repeatedly; idempotent.
// Called once from main.cpp after Config::init().
void begin();

// True when the user has successfully entered the unlock password this
// session OR it persisted from a previous boot.
bool isUnlocked();

// Current per-tool bitmask. After a successful unlock this is TOOL_MASK_ALL
// unless the user has explicitly turned individual tools off via the
// LAB settings page. When !isUnlocked() the mask is meaningless.
uint8_t toolMask();

// True if the lab is unlocked AND the named tool is on in the current mask.
// Equivalent to `isUnlocked() && (toolMask() & (1u << t))` - provided so
// call sites read naturally.
bool isToolEnabled(Tool t);

// Try the user-typed ASCII password against the baked hash. On success
// persists the unlock flag, sets it to TOOL_MASK_ALL, and returns true.
// On failure returns false; the prior state is untouched. Empty / null
// input is rejected.
bool unlock(const char* ascii);

// Clear the unlock flag, set the tool mask to 0, persist. Used by the
// LAB settings page "LOCK NOW" action and by a successful wipe.
void lock();

// Flip one tool bit in the persisted mask. No-op when locked.
void setTool(Tool t, bool on);

// Human-readable short tag for a tool (e.g. "LIGHT", "AGG"). Used by the
// STATUS page to render the active mask compactly.
const char* toolName(Tool t);

// Pack the active mask into a compact human-readable form like
// "LIGHT AGG EP PP BLE IR SPEC LOOT" (omits disabled). Output buf must
// hold at least 96 bytes.
void formatActiveMask(char* out, size_t len);

}  // namespace Lab