// Live registry for capture methods. The actual table is filled by static
// initialisers in each method_*.cpp via CAP_METHOD_REGISTER(); this file
// just owns the storage and the public accessors.
//
// Capacity is generous but bounded (PSRAM-free device, all RAM is internal).
// 16 methods covers any realistic taxonomy (broadcast kick, targeted kick,
// PMKID probe, KARMA, mana/lorcon-style, custom RF behaviour, etc.) and
// still leaves room.
#include "method_ctx.h"
#include <string.h>

namespace Cap {
namespace Methods {

static const uint8_t CAPACITY = 16;
static Entry   s_table[CAPACITY];
static uint8_t s_count = 0;

void add(const Entry& e) {
    if (s_count >= CAPACITY) return;
    // Last write wins for duplicate names — protects against the same
    // method accidentally being registered twice via two .cpp files.
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

void resetAll() {
    for (uint8_t i = 0; i < s_count; i++) {
        if (s_table[i].reset) s_table[i].reset();
    }
}

} // namespace Methods
} // namespace Cap