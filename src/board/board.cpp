// board/board.cpp
#include "board.h"
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <driver/gpio.h>
#include <memory>
#include "utility/Keyboard/KeyboardReader/IOMatrix.h"
#include "utility/Keyboard/KeyboardReader/TCA8418.h"
#include <esp_system.h>

namespace Board {

static bool s_adv = false;

const char* name() {
    return BOARD_NAME;
}

const char* modelLabel() {
    return s_adv ? "Cardputer ADV" : "Cardputer";
}

bool isAdv() { return s_adv; }

const char* chip() {
    const char* m = ESP.getChipModel();
    return m ? m : "ESP32";
}

uint8_t defaultButtonGpio() {
    return (uint8_t)BUTTON_GPIO_DEFAULT;
}

bool gpioAllowed(int gpio) {
    if (gpio < 0) return false;
#if CONFIG_IDF_TARGET_ESP32C3
    return gpio <= 21;
#elif CONFIG_IDF_TARGET_ESP32C6
    return gpio <= 30;
#elif CONFIG_IDF_TARGET_ESP32S3
    return gpio <= 48;
#elif CONFIG_IDF_TARGET_ESP32S2
    return gpio <= 46;
#else
    if (gpio >= 6 && gpio <= 11) return false;
    return gpio <= 39;
#endif
}

uint32_t flashBytes() {
    return (uint32_t)ESP.getFlashChipSize();
}

static bool gpio89LooksLikeAdv() {
    pinMode(8, INPUT_PULLDOWN);
    pinMode(9, INPUT_PULLDOWN);
    delayMicroseconds(80);
    bool hi = digitalRead(8) && digitalRead(9);
    gpio_reset_pin((gpio_num_t)8);
    gpio_reset_pin((gpio_num_t)9);
    return hi;
}

void startKeyboard() {
    auto b = M5.getBoard();
    s_adv = (b == m5::board_t::board_M5CardputerADV);
    if (!s_adv) s_adv = gpio89LooksLikeAdv();

    if (s_adv) {
        M5Cardputer.Keyboard.begin(
            std::unique_ptr<KeyboardReader>(new TCA8418KeyboardReader()));
        Serial.println("[KB] Cardputer ADV TCA8418");
    } else {
        M5Cardputer.Keyboard.begin(
            std::unique_ptr<KeyboardReader>(new IOMatrixKeyboardReader()));
        Serial.println("[KB] Cardputer IO matrix");
    }
}

} // namespace Board
