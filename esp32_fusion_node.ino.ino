#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <WiFi.h>
#include <cJSON.h> // Naya: JSON banane ke liye

#include "node_config.h"
#include "storage.h"`
#include "wifi_setup.h"
#include "radio.h"
#include "mesh.h"
#include "onion.h"
#include "dtn.h"

static const char *TAG = "main";


WiFiClient g_phone_client;


static void phone_server_task(void *arg);

void setup() {
    Serial.begin(115200);
    delay(1000); 
    
    Serial.println("DEBUG: Starting setup...");

   
    storage_init();
    Serial.println("DEBUG: Storage initialized.");
    
    wifi_setup_start();
    Serial.println("DEBUG: Wi-Fi setup started.");
    
    Serial.println("DEBUG: Initializing radio...");
    radio_init();
    Serial.println("DEBUG: Radio initialized successfully.");
    
    mesh_init();
    Serial.println("DEBUG: Mesh initialized.");
    
    dtn_init();
    Serial.println("DEBUG: DTN initialized.");

    
    xTaskCreate(phone_server_task, "phone_srv", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Secure Fusion Node ready. NodeID=%s", NODE_ID);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}


static void phone_server_task(void *arg) {
    WiFiServer server(18080);
    server.begin();
    ESP_LOGI("phone", "Listening for phone on tcp://%s:18080", WiFi.softAPIP().toString().c_str());

    while (1) {
        if (server.hasClient()) {
            if (g_phone_client && g_phone_client.connected()) {
                g_phone_client.stop();
            }
            g_phone_client = server.available();
            ESP_LOGI("phone", "Phone connected!");

            
            cJSON *id_payload = cJSON_CreateObject();
            if (id_payload) {
                cJSON_AddStringToObject(id_payload, "type", "NODE_ID");
                cJSON_AddStringToObject(id_payload, "id", NODE_ID);
                char *id_str = cJSON_PrintUnformatted(id_payload);
                if (id_str) {
                   g_phone_client.write(id_str);
                    g_phone_client.write('\n'); // Delimiter ke liye
                    ESP_LOGI("phone", "Sent Node ID to phone: %s", id_str);
                    free(id_str);
                }
                cJSON_Delete(id_payload);
            }
        }

        if (g_phone_client && g_phone_client.connected()) {
            if (g_phone_client.available()) {
                uint8_t buf[ONION_MAX_BYTES];
                int r = g_phone_client.read(buf, sizeof(buf));

                if (r > 0) {
                    int off = 0;
                    uint8_t dlen = buf[off++];
                    if (dlen >= 64 || (dlen + 1) > r) {
                        ESP_LOGE("phone", "Invalid destination length from phone.");
                        continue;
                    }
                    char dest[64] = {0};
                    memcpy(dest, buf + off, dlen);
                    off += dlen;
                    uint8_t *inner = buf + off;
                    size_t inner_len = r - off;

                    const char *route[8];
                    size_t route_len = 0;
                    if (!mesh_choose_route(dest, route, &route_len)) {
                        ESP_LOGW("phone", "No route to %s, queueing for DTN", dest);
                        dtn_enqueue(dest, inner, inner_len);
                        continue;
                    }

                    uint8_t onion[ONION_MAX_BYTES];
                    size_t onion_len = 0;
                    if (!onion_build(route, route_len, inner, inner_len, onion, &onion_len)) {
                        ESP_LOGE("phone", "onion_build failed");
                        for(size_t i = 0; i < route_len; i++) free((void*)route[i]);
                        continue;
                    }

                    ESP_LOGI("phone", "Sending onion to first hop: %s", route[0]);
                    radio_send(route[0], onion, onion_len);

                    for(size_t i = 0; i < route_len; i++) free((void*)route[i]);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
