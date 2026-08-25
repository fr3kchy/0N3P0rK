// Live registry for radio packs. The actual table is filled by static
// initialisers in each pack_*.cpp via CAP_PACK_REGISTER(); this file just
// owns the storage and the public accessors. Mirrors
// src/cap/methods/method_registry.cpp exactly.
#include "pack_ctx.h"
#include <string.h>

namespace Cap {
namespace Packs {

static const uint8_t CAPACITY = 16;
static Entry   s_table[CAPACITY];
static uint8_t s_count = 0;

void add(const Entry& e) {
    if (s_count >= CAPACITY) return;
    // Last write wins for duplicate names — protects against the same
    // pack accidentally being registered twice via two .cpp files.
    for (uint8_t i = 0; i < s_count; i++) {
        if (strcmp(s_table[i].name, e.name) == 0) {
            s_table[i] = e;
            return;
        }
    }
    s_table[s_count++] = e;
}

const Entry* table(uint8_t* outCount) {
    if (outCount) *outCount = s_count;
    return s_table;
}

uint8_t count() { return s_count; }

const Entry* findByName(const char* name) {
    if (!name) return nullptr;
    for (uint8_t i = 0; i < s_count; i++) {
        if (strcmp(s_table[i].name, name) == 0) return &s_table[i];
    }
    return nullptr;
}

const char* name(uint8_t idx) {
    return idx < s_count ? s_table[idx].name : nullptr;
}

} // namespace Packs
} // namespace Cap
