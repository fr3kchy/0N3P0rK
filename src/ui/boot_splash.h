#pragma once

// Isolated boot splash. Owns its sprite + font state.
// Any key skips. Restores pig walk/pos and Font0 before return.
void runBootSplash();
