#pragma once
#include <WiFi.h>
#include <esp_wifi.h>

namespace WiFiUtils {
inline void hardReset() {
    WiFi.disconnect(false, false);
}
inline void shutdown() {
    WiFi.disconnect(false, false);
}
inline void stopPromiscuous() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
}
}
