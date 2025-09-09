#include <WiFi.h>
#include "esp_log.h"
#include "node_config.h"

static const char *TAG = "wifi";

void wifi_event_handler(WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Station joined AP");
    }
}

void wifi_setup_start(void) {
    WiFi.mode(WIFI_AP);
    WiFi.onEvent(wifi_event_handler);
    WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, AP_MAX_CONN);
    ESP_LOGI(TAG, "AP SSID=%s; IP address: %s", AP_SSID, WiFi.softAPIP().toString().c_str());
}
