#pragma once


#define NRF24_CE_PIN   15
#define NRF24_CSN_PIN  16
#define NRF24_SCK_PIN  14
#define NRF24_MISO_PIN 12
#define NRF24_MOSI_PIN 13


#define NODE_ID        "E32-S2-01"


#define AP_SSID       "FUSION_NODE2_AP"
#define AP_PASS       "fusion_pass2_123"
#define AP_CHANNEL    1
#define AP_MAX_CONN   4

#define HELLO_INTERVAL_MS 10000
#define HELLO_TTL         5
#define ONION_MAX_BYTES   2048
#define DTN_MAX_ITEMS     32
#define REPLAY_CACHE_SIZE 64