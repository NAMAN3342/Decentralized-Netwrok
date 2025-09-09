#include "dtn.h"
#include "node_config.h"
#include "radio.h"
#include "mesh.h"
#include "onion.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <Arduino.h> // For FreeRTOS functions

static const char* TAG = "DTN";
typedef struct { char dest[32]; uint8_t *buf; size_t len; } item_t;
static item_t Q[DTN_MAX_ITEMS];
static int QN = 0;

static void dtn_task(void *arg);

void dtn_init(void) {
    xTaskCreate(dtn_task, "dtn_task", 4096, NULL, 3, NULL);
}

bool dtn_enqueue(const char *dest, const uint8_t *payload, size_t len) {
    if (QN >= DTN_MAX_ITEMS) return false;
    Q[QN].buf = (uint8_t*)malloc(len);
    memcpy(Q[QN].buf, payload, len);
    Q[QN].len = len;
    strncpy(Q[QN].dest, dest, 31);
    QN++;
    return true;
}

static void dtn_task(void *arg) {
    while (1) {
        if (QN > 0) {
            const char *route[8];
            size_t rl = 0;
            if (mesh_choose_route(Q[0].dest, route, &rl)) {
                ESP_LOGI(TAG, "Route found for queued message to %s.", Q[0].dest);
                uint8_t outbuf[ONION_MAX_BYTES];
                size_t outl = 0;
                if (onion_build(route, rl, Q[0].buf, Q[0].len, outbuf, &outl)) {
                    radio_send(route[0], outbuf, outl);
                }
                for(size_t i = 0; i < rl; i++) free((void*)route[i]);
                
                free(Q[0].buf);
                for (int i = 1; i < QN; i++) Q[i - 1] = Q[i];
                QN--;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
