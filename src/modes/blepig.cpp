// Lab BLE advertise burst. Own devices only.
#include "blepig.h"
#include "../core/config.h"
#include "../ui/display.h"
#include "../piglet/avatar.h"
#include "../audio/sfx.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <string.h>
#include <stdio.h>

bool BlePigMode::running = false;
uint32_t BlePigMode::bursts = 0;
uint32_t BlePigMode::lastBurstMs = 0;
uint32_t BlePigMode::sessionStart = 0;
char BlePigMode::lastName[20] = "-";

static BLEAdvertising* s_adv = nullptr;
static bool s_bleUp = false;

static const char* kNames[] = {
    "AirPods Pro", "LE_WH-1000XM", "Galaxy Watch", "Mi Band", "Pixel Buds"
};
static const uint8_t kNameCount = 5;

static void applyAdv(uint8_t idx) {
    if (!s_adv) return;
    BLEAdvertisementData data;
    data.setName(kNames[idx % kNameCount]);
    data.setFlags(0x06);
    s_adv->setAdvertisementData(data);
    strncpy(BlePigMode::lastName, kNames[idx % kNameCount], sizeof(BlePigMode::lastName) - 1);
    BlePigMode::lastName[sizeof(BlePigMode::lastName) - 1] = '\0';
}

void BlePigMode::start() {
    bool ok = Display::showConfirmBox(
        "BLE LAB",
        "OWN DEVICES ONLY\nadvertise burst\nTUNE BLE sets speed");
    if (!ok) {
        running = false;
        return;
    }

    WiFi.mode(WIFI_OFF);
    delay(40);

    if (!s_bleUp) {
        BLEDevice::init("OneLPig");
        s_adv = BLEDevice::getAdvertising();
        s_bleUp = (s_adv != nullptr);
    }
    if (!s_adv) {
        Display::showToast("BLE FAIL", 1200);
        running = false;
        return;
    }

    bursts = 0;
    lastBurstMs = 0;
    sessionStart = millis();
    applyAdv(0);
    s_adv->start();
    running = true;
    Avatar::setState(AvatarState::HUNTING);
    Display::showToast("BLE ON", 800);
}

void BlePigMode::stop() {
    if (!running && !s_bleUp) return;
    running = false;
    if (s_adv) s_adv->stop();
    if (s_bleUp) {
        BLEDevice::deinit(false);
        s_adv = nullptr;
        s_bleUp = false;
    }
    Avatar::setState(AvatarState::NEUTRAL);
}

void BlePigMode::update() {
    if (!running) return;

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto keys = M5Cardputer.Keyboard.keysState();
        bool back = M5Cardputer.Keyboard.isKeyPressed('`') ||
                    M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) ||
                    keys.del;
        if (back) {
            stop();
            return;
        }
    }

    uint32_t now = millis();
    uint16_t burst = Config::ble().burstMs;
    if (burst < 50) burst = 50;
    if (now - lastBurstMs < burst) return;
    lastBurstMs = now;

    if (s_adv) s_adv->stop();
    applyAdv((uint8_t)(bursts % kNameCount));
    if (s_adv) s_adv->start();
    bursts++;
    uint16_t hold = Config::ble().advMs;
    if (hold < 50) hold = 50;
    delay(hold);
}

void BlePigMode::getStatusLine(char* out, size_t len) {
    snprintf(out, len, "BLE %lu  %ums",
             (unsigned long)bursts, (unsigned)Config::ble().burstMs);
}

void BlePigMode::draw(M5Canvas& canvas) {
    canvas.fillSprite(UiStyle::BG);
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);
    canvas.setFont(&fonts::Font0);

    canvas.setTextColor(UiStyle::TITLE);
    canvas.drawString("BLE LAB", 4, 2);
    canvas.setTextColor(UiStyle::GREEN);
    canvas.drawString("ON", DISPLAY_W - 20, 2);

    canvas.setTextColor(UiStyle::DIM);
    canvas.drawString("ADV", 4, 20);
    canvas.setTextColor(UiStyle::CYAN);
    canvas.setTextSize(2);
    canvas.drawString(lastName, 4, 32);
    canvas.setTextSize(1);

    char buf[40];
    canvas.setTextColor(UiStyle::TEXT);
    snprintf(buf, sizeof(buf), "BURST %lu", (unsigned long)bursts);
    canvas.drawString(buf, 4, 56);
    snprintf(buf, sizeof(buf), "%ums / adv %ums",
             (unsigned)Config::ble().burstMs, (unsigned)Config::ble().advMs);
    canvas.setTextColor(UiStyle::DIM);
    canvas.drawString(buf, 4, 70);

    uint32_t sec = (millis() - sessionStart) / 1000;
    snprintf(buf, sizeof(buf), "%us", (unsigned)sec);
    canvas.drawString(buf, 4, 84);

    canvas.setTextColor(UiStyle::GOLD);
    canvas.drawString("` exit   TUNE BLE speed", 4, MAIN_H - 10);
}
