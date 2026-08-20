// board/board.h
// Compile-time target + runtime chip info.
// One codebase, one binary per chip (C3 != S3).

#pragma once

#include <Arduino.h>
#include <stdint.h>

#ifndef BUTTON_GPIO_DEFAULT
#define BUTTON_GPIO_DEFAULT 0
#endif

#ifndef BOARD_NAME
#define BOARD_NAME "ESP32"
#endif

namespace Board {

const char* name();
const char* chip();
// Runtime: "Cardputer" or "Cardputer ADV"
const char* modelLabel();
bool isAdv();
// After SD: ADV keeps GPIO 5 as CS; original puts GPIO 5 back as a key row.
void startKeyboard();
uint8_t defaultButtonGpio();
bool gpioAllowed(int gpio);
uint32_t flashBytes();

} // namespace Board
