#include <SPI.h>
#include <RF24.h>
#include "esp_log.h"
#include "node_config.h"
#include "radio.h"
#include <Arduino.h> // For FreeRTOS functions

static const char *TAG = "radio_nrf24";
RF24 radio(NRF24_CE_PIN, NRF24_CSN_PIN);
const byte broadcast_address[6] = "BCAST";

#define FRAG_PAYLOAD_SIZE 30
#define MAX_FRAGMENTS 68
#define REASSEMBLY_TIMEOUT_MS 5000

typedef struct {
    uint8_t packet_id;
    uint8_t total_frags;
    bool received_frags[MAX_FRAGMENTS];
    uint8_t buffer[ONION_MAX_BYTES];
    uint64_t last_frag_time;
    bool in_use;
} reassembly_buffer_t;

static reassembly_buffer_t reassembly_pool[5];
static uint8_t next_packet_id = 0;

reassembly_buffer_t* get_reassembly_buffer(uint8_t packet_id) {
    uint64_t current_time = esp_timer_get_time();
    for (int i = 0; i < 5; i++) {
        if (reassembly_pool[i].in_use && (current_time - reassembly_pool[i].last_frag_time) > REASSEMBLY_TIMEOUT_MS * 1000) {
            ESP_LOGW(TAG, "Packet ID %d timed out.", reassembly_pool[i].packet_id);
            reassembly_pool[i].in_use = false;
        }
    }
    for (int i = 0; i < 5; i++) {
        if (reassembly_pool[i].in_use && reassembly_pool[i].packet_id == packet_id) return &reassembly_pool[i];
    }
    for (int i = 0; i < 5; i++) {
        if (!reassembly_pool[i].in_use) {
            memset(&reassembly_pool[i], 0, sizeof(reassembly_buffer_t));
            reassembly_pool[i].packet_id = packet_id;
            reassembly_pool[i].in_use = true;
            return &reassembly_pool[i];
        }
    }
    return NULL;
}

void rx_task(void *arg) {
    uint8_t frag_buf[32];
    while (1) {
        if (radio.available()) {
            radio.read(&frag_buf, sizeof(frag_buf));
            uint8_t packet_id = frag_buf[0];
            uint8_t frag_info = frag_buf[1];
            bool is_last = (frag_info >> 7) & 0x01;
            uint8_t frag_num = frag_info & 0x7F;

            reassembly_buffer_t* rb = get_reassembly_buffer(packet_id);
            if (!rb || rb->received_frags[frag_num]) continue;

            memcpy(rb->buffer + (frag_num * FRAG_PAYLOAD_SIZE), frag_buf + 2, FRAG_PAYLOAD_SIZE);
            rb->received_frags[frag_num] = true;
            rb->last_frag_time = esp_timer_get_time();
            if (is_last) rb->total_frags = frag_num + 1;

            if (rb->total_frags > 0) {
                bool complete = true;
                for (int i = 0; i < rb->total_frags; i++) {
                    if (!rb->received_frags[i]) { complete = false; break; }
                }
                if (complete) {
                    ESP_LOGI(TAG, "Reassembled packet ID %d", packet_id);
                    // Simplification: Assume last packet is full for total size calculation
                    size_t total_size = rb->total_frags * FRAG_PAYLOAD_SIZE;
                    mesh_on_radio_frame(rb->buffer, total_size);
                    rb->in_use = false;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void radio_init(void) {
    SPI.begin(NRF24_SCK_PIN, NRF24_MISO_PIN, NRF24_MOSI_PIN, NRF24_CSN_PIN);
    
    if (!radio.begin()) {
        ESP_LOGE(TAG, "Radio hardware not responding!");
        while (1) {}
    }
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.openReadingPipe(1, broadcast_address);
    radio.startListening();
    xTaskCreate(rx_task, "radio_rx", 4096, NULL, 10, NULL);
    ESP_LOGI(TAG, "nRF24L01 Radio initialized.");
}

bool radio_send(const char *next_hop_id, const uint8_t *buf, size_t len) {
    uint8_t total_frags = (len + FRAG_PAYLOAD_SIZE - 1) / FRAG_PAYLOAD_SIZE;
    if (total_frags > 127) {
        ESP_LOGE(TAG, "Packet too large to fragment.");
        return false;
    }
    uint8_t packet_id = next_packet_id++;
    
    radio.stopListening();
    radio.openWritingPipe(broadcast_address);

    for (uint8_t i = 0; i < total_frags; i++) {
        uint8_t frag_buf[32] = {0};
        frag_buf[0] = packet_id;
        frag_buf[1] = i;
        if (i == total_frags - 1) frag_buf[1] |= 0x80;

        size_t offset = i * FRAG_PAYLOAD_SIZE;
        size_t chunk_size = (len - offset < FRAG_PAYLOAD_SIZE) ? (len - offset) : FRAG_PAYLOAD_SIZE;
        memcpy(frag_buf + 2, buf + offset, chunk_size);
        
        if (!radio.write(&frag_buf, sizeof(frag_buf))) {
            radio.startListening();
            ESP_LOGE(TAG, "Failed to send fragment %d", i);
            return false;
        }
    }
    
    radio.startListening();
    return true;
}

