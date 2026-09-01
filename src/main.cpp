// fR3k - demon Tamagotchi + GPS on M5Cardputer / Cardputer ADV

#include <M5Cardputer.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include "build_info.h"
#include "core/config.h"
#include "core/xp.h"
#include "core/app.h"
#include "ui/display.h"
#include "ui/menu.h"
#include "piglet/avatar.h"
#include "gps/gps_service.h"
#include "piglet/mood.h"
#include "audio/sfx.h"
#include "storage/littlefs_ops.h"
#include "board/board.h"
#include "net/ap_sta.h"
#include "cap/sniffer.h"
#include "lab/lab_unlock.h"
#include "telemetry/telemetry.h"
#include "sync/wpasec.h"  // fR3k v3.0.4: preload() the cache at boot
#include "piglet/spectrum_sky.h"
#include "modes/evilpig.h"
#include "modes/pigpass.h"

static void preInitWiFiDriverEarly() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, false);
    WiFi.setSleep(false);
    delay(50);
}

static void setupHeapLayout() {
    size_t beforeLargest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    Serial.printf("[BOOT] pre-fence largest=%u\n", (unsigned)beforeLargest);

    static constexpr size_t kFenceSize = 80000;
    void* fence = heap_caps_malloc(kFenceSize, MALLOC_CAP_8BIT);
    preInitWiFiDriverEarly();
    if (fence) heap_caps_free(fence);

    Serial.printf("[BOOT] post-fence free=%u largest=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void setup() {
    Serial.begin(115200);
    delay(120);
    Serial.println();
    Serial.printf(">>> %s v%s build=%s\n", FR3K_NAME, FR3K_VERSION, FR3K_BUILD);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    pinMode(0, INPUT_PULLUP);

    setupHeapLayout();
    yield();

    if (!Storage::begin()) {
        Serial.println("[BOOT] LittleFS failed - continuing");
    }
    Board::startKeyboard();
    Serial.printf("[BOOT] board=%s\n", Board::modelLabel());

    Config::init();
    Lab::begin();
    XP::begin();
    M5.Display.setBrightness(Config::personality().brightness * 255 / 100);

    Display::init();
    SFX::init();
    Avatar::init();
    GpsService::begin();
    Telemetry::begin();
    // fR3k v3.0.4: warm the WPA-sec cracked+uploaded cache so the
    // first LootMenu open is instant. SD must be up; we just did
    // Storage::begin() above. Safe to skip if SD is missing.
    WPASec::preload();
    // fR3k v3.0.3: SpectrumSky::begin() removed. The spectrum overlay
    // is off by default; the source in src/piglet/spectrum_sky.cpp is
    // preserved for a future re-enable.
    Display::showBootSplash();
    Display::refreshBrightness();
    Mood::init();
    Menu::begin();
    App::begin();
    yield();

#if !FR3K_SAFE_BUILD
    // Offensive subsystems are gated at compile time in safe builds and at
    // runtime in the default v3 build. The default env ships the code so
    // the runtime gate can take over; we still skip the heavy init() while
    // the lab is locked, so the menu refuses every offensive action.
    if (Lab::isUnlocked()) {
        Net::begin();
        Storage::loadKeysIntoNet();
        Cap::begin();
        EvilPigMode::init();
        PigpassMode::init();
    }
#endif

    Net::Status s = Net::status();
    Serial.printf("[BOOT] wifi mode ssid=%s ip=%s\n", s.ssid, s.ip);
    Serial.printf("[BOOT] demon=%s ready safe=%u lab=%u\n",
                  Config::personality().name,
                  (unsigned)FR3K_SAFE_BUILD,
                  (unsigned)Lab::isUnlocked());
}

static unsigned long s_lastHeapLog = 0;

void loop() {
    M5Cardputer.update();
    App::loop();
    Display::update();
    SFX::update();
    XP::tick();
    GpsService::loop();
    Telemetry::sample();
#if !FR3K_SAFE_BUILD
    Cap::loop();
#endif

    if (millis() - s_lastHeapLog > 30000) {
        s_lastHeapLog = millis();
        Serial.printf("[LOOP] heap=%u largest=%u safe=%u\n",
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                      (unsigned)FR3K_SAFE_BUILD);
    }
    delay(5);
    yield();
}
