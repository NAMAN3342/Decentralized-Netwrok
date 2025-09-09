#include "mesh.h"
#include "radio.h"
#include "crypto_abstraction.h"
#include "node_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <Arduino.h> // For FreeRTOS functions
#include <cstdio>    // For sprintf, sscanf

#define MAX_NB 32
typedef struct { 
    char id[32]; 
    uint8_t x_pub[32];
    uint8_t e_pub[32];
    uint64_t last; 
} nb_t;

static nb_t NB[MAX_NB];
static int NB_N = 0;
static const char *TAG = "mesh";

static void hello_task(void *arg);
extern void onion_on_frame(const uint8_t *buf, size_t len);

static char* hex_of(const uint8_t *b, size_t n) {
    char *o = (char*)malloc(n * 2 + 1);
    if (!o) return NULL;
    for (size_t i = 0; i < n; i++) sprintf(o + i * 2, "%02x", b[i]);
    o[n * 2] = 0;
    return o;
}
static void unhex(uint8_t* out, const char *h) {
    size_t n = strlen(h) / 2;
    for (size_t i = 0; i < n; i++) { unsigned v; sscanf(h + i * 2, "%02x", &v); out[i] = v; }
}

static void nb_upsert(const char *id, const uint8_t x_pub[32], const uint8_t e_pub[32]) {
    for (int i = 0; i < NB_N; i++) {
        if (!strcmp(NB[i].id, id)) {
            memcpy(NB[i].x_pub, x_pub, 32);
            memcpy(NB[i].e_pub, e_pub, 32);
            NB[i].last = esp_timer_get_time();
            return;
        }
    }
    if (NB_N < MAX_NB) {
        strncpy(NB[NB_N].id, id, sizeof(NB[NB_N].id) - 1);
        memcpy(NB[NB_N].x_pub, x_pub, 32);
        memcpy(NB[NB_N].e_pub, e_pub, 32);
        NB[NB_N].last = esp_timer_get_time();
        NB_N++;
        ESP_LOGI(TAG, "New secure neighbor: %s", id);
    }
}

bool mesh_get_x25519_pub(const char *node_id, uint8_t out_pub[32]) {
    for (int i = 0; i < NB_N; i++) {
        if (!strcmp(NB[i].id, node_id)) {
            memcpy(out_pub, NB[i].x_pub, 32);
            return true;
        }
    }
    return false;
}

void mesh_init(void) {
    crypto_keys_load_or_create();
    xTaskCreate(hello_task, "hello", 8192, NULL, 5, NULL); // Increased stack size for hello_task
}

static void hello_task(void *arg) {
    while (1) {
        Serial.println("DEBUG: hello_task waiting...");
        vTaskDelay(pdMS_TO_TICKS(HELLO_INTERVAL_MS));
        
        Serial.println("DEBUG: hello_task creating data payload...");
        cJSON *data_pl = cJSON_CreateObject();
        cJSON_AddStringToObject(data_pl, "type", "HELLO");
        cJSON_AddStringToObject(data_pl, "id", NODE_ID);
        char *x_pub_hex = hex_of(crypto_get_x25519_public(), 32);
        cJSON_AddStringToObject(data_pl, "x_pub", x_pub_hex);
        free(x_pub_hex);
        char *e_pub_hex = hex_of(crypto_get_ed25519_public(), 32);
        cJSON_AddStringToObject(data_pl, "e_pub", e_pub_hex);
        free(e_pub_hex);
        cJSON_AddNumberToObject(data_pl, "ttl", HELLO_TTL);
        char *data_txt = cJSON_PrintUnformatted(data_pl);
        cJSON_Delete(data_pl);

        Serial.println("DEBUG: hello_task signing data...");
        uint8_t signature[64];
        crypto_sign(signature, (const uint8_t*)data_txt, strlen(data_txt));
        char *sig_hex = hex_of(signature, 64);

        Serial.println("DEBUG: hello_task creating final payload...");
        cJSON *final_pl = cJSON_CreateObject();
        cJSON_AddStringToObject(final_pl, "data", data_txt);
        cJSON_AddStringToObject(final_pl, "sig", sig_hex);
        free(data_txt);
        free(sig_hex);
        char *final_txt = cJSON_PrintUnformatted(final_pl);
        cJSON_Delete(final_pl);
        
        Serial.println("DEBUG: hello_task broadcasting...");
        radio_send("BCAST", (const uint8_t*)final_txt, strlen(final_txt));
        free(final_txt);
        Serial.println("DEBUG: hello_task broadcast complete.");
    }
}

static void handle_hello(const uint8_t *buf, size_t len) {
    char *s = strndup((const char*)buf, len);
    cJSON *final_pl = cJSON_Parse(s);
    if (!final_pl) { free(s); return; }

    cJSON *data_item = cJSON_GetObjectItem(final_pl, "data");
    cJSON *sig_item = cJSON_GetObjectItem(final_pl, "sig");
    if (!data_item || !sig_item) { cJSON_Delete(final_pl); free(s); return; }

    const char *data_txt = data_item->valuestring;
    const char *sig_hex = sig_item->valuestring;
    cJSON *data_pl = cJSON_Parse(data_txt);
    if (!data_pl) { cJSON_Delete(final_pl); free(s); return; }

    const char *id = cJSON_GetObjectItem(data_pl, "id")->valuestring;
    if (!strcmp(id, NODE_ID)) { cJSON_Delete(data_pl); cJSON_Delete(final_pl); free(s); return; }

    const char *e_pub_hex = cJSON_GetObjectItem(data_pl, "e_pub")->valuestring;
    uint8_t e_pub[32], signature[64];
    unhex(e_pub, e_pub_hex);
    unhex(signature, sig_hex);

    if (!crypto_verify(signature, e_pub, (const uint8_t*)data_txt, strlen(data_txt))) {
        ESP_LOGW(TAG, "Invalid signature from %s! Dropping.", id);
        cJSON_Delete(data_pl); cJSON_Delete(final_pl); free(s); return;
    }

    const char *x_pub_hex = cJSON_GetObjectItem(data_pl, "x_pub")->valuestring;
    int ttl = cJSON_GetObjectItem(data_pl, "ttl")->valueint;
    uint8_t x_pub[32];
    unhex(x_pub, x_pub_hex);
    nb_upsert(id, x_pub, e_pub);

    if (ttl > 0) {
        cJSON_ReplaceItemInObject(data_pl, "ttl", cJSON_CreateNumber(ttl - 1));
        char *new_data_txt = cJSON_PrintUnformatted(data_pl);
        uint8_t new_sig[64];
        crypto_sign(new_sig, (const uint8_t*)new_data_txt, strlen(new_data_txt));
        char *new_sig_hex = hex_of(new_sig, 64);
        cJSON *rebroadcast_pl = cJSON_CreateObject();
        cJSON_AddStringToObject(rebroadcast_pl, "data", new_data_txt);
        cJSON_AddStringToObject(rebroadcast_pl, "sig", new_sig_hex);
        free(new_data_txt);
        free(new_sig_hex);
        char *rebroadcast_txt = cJSON_PrintUnformatted(rebroadcast_pl);
        cJSON_Delete(rebroadcast_pl);
        radio_send("BCAST", (uint8_t*)rebroadcast_txt, strlen(rebroadcast_txt));
        free(rebroadcast_txt);
    }
    
    cJSON_Delete(data_pl);
    cJSON_Delete(final_pl);
    free(s);
}

bool mesh_choose_route(const char *dest_id, const char **route_out, size_t *route_len) {
    for (int i = 0; i < NB_N; i++) {
        if (!strcmp(NB[i].id, dest_id)) {
            route_out[0] = strdup(dest_id);
            *route_len = 1;
            return true;
        }
    }
    if (NB_N > 0) {
        route_out[0] = strdup(NB[0].id);
        route_out[1] = strdup(dest_id);
        *route_len = 2;
        return true;
    }
    return false;
}

void mesh_on_radio_frame(const uint8_t *buf, size_t len) {
    if (len > 10 && memmem(buf, len, "\"HELLO\"", 7)) {
        handle_hello(buf, len);
        return;
    }
    onion_on_frame(buf, len);
}