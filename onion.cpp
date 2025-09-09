#include "onion.h"
#include "mesh.h"
#include "radio.h"
#include "crypto_abstraction.h"
#include "node_config.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <cstdio>

static const char *TAG = "onion";
static uint8_t replay_cache[REPLAY_CACHE_SIZE][32];
static int replay_cache_idx = 0;

static bool is_replay(const uint8_t *buf, size_t len) {
    uint8_t hash[32];
    mbedtls_sha256(buf, len, hash, 0);
    for (int i = 0; i < REPLAY_CACHE_SIZE; i++) {
        if (memcmp(replay_cache[i], hash, 32) == 0) return true;
    }
    memcpy(replay_cache[replay_cache_idx], hash, 32);
    replay_cache_idx = (replay_cache_idx + 1) % REPLAY_CACHE_SIZE;
    return false;
}

static char* hex_of(const uint8_t *b, size_t n) {
    char *o = (char*)malloc(n * 2 + 1);
    for (size_t i = 0; i < n; i++) sprintf(o + i * 2, "%02x", b[i]);
    o[n * 2] = 0;
    return o;
}
static uint8_t* unhex(const char *h, size_t *out) {
    size_t n = strlen(h) / 2;
    uint8_t *b = (uint8_t*)malloc(n);
    for (size_t i = 0; i < n; i++) { unsigned v; sscanf(h + i * 2, "%02x", &v); b[i] = v; }
    *out = n;
    return b;
}

bool onion_build(const char **route, size_t route_len, const uint8_t *inner, size_t inner_len, uint8_t *out, size_t *out_len) {
    uint8_t *payload = (uint8_t*)malloc(inner_len);
    memcpy(payload, inner, inner_len);
    size_t plen = inner_len;

    for (int i = (int)route_len - 1; i >= 0; --i) {
        const char *hop = route[i];
        uint8_t hop_pub[32];
        if (!mesh_get_x25519_pub(hop, hop_pub)) {
            ESP_LOGE(TAG, "no pub for %s", hop);
            free(payload);
            return false;
        }
        eph_kp_t epk;
        x25519_ephemeral(&epk);
        uint8_t shared[32];
        x25519_shared(epk.priv, hop_pub, shared);
        char info[64];
        int ilen = snprintf(info, sizeof(info), "layer:%s", hop);
        uint8_t key[32];
        hkdf_sha256(shared, 32, (uint8_t*)info, ilen, key);
        uint8_t nonce[24];
        random_bytes(nonce, 24);

        const char *next = (i + 1 < (int)route_len) ? route[i + 1] : "LOCAL";
        char *inner_hex = hex_of(payload, plen);
        cJSON *pl = cJSON_CreateObject();
        cJSON_AddStringToObject(pl, "next", next);
        cJSON_AddStringToObject(pl, "inner", inner_hex);
        free(inner_hex);
        char *plain = cJSON_PrintUnformatted(pl);
        size_t plain_len = strlen(plain);
        cJSON_Delete(pl);

        size_t ct_max = plain_len + 32;
        uint8_t *ct = (uint8_t*)malloc(ct_max);
        size_t ct_len = 0;
        aead_encrypt_xc20p(key, nonce, (uint8_t*)plain, plain_len, ct, &ct_len);
        free(plain);

        size_t layer_len = 32 + 24 + ct_len;
        uint8_t *layer = (uint8_t*)malloc(layer_len);
        memcpy(layer, epk.pub, 32);
        memcpy(layer + 32, nonce, 24);
        memcpy(layer + 56, ct, ct_len);
        free(ct);
        free(payload);
        payload = layer;
        plen = layer_len;
    }
    memcpy(out, payload, plen);
    *out_len = plen;
    free(payload);
    return true;
}

static void peel_and_forward(const uint8_t *buf, size_t len) {
    if (len < 56) return;
    const uint8_t *epk = buf, *nonce = buf + 32, *ct = buf + 56;
    size_t ct_len = len - 56;
    uint8_t shared[32];
    x25519_shared(crypto_get_x25519_private(), epk, shared);
    char info[64];
    int ilen = snprintf(info, sizeof(info), "layer:%s", NODE_ID);
    uint8_t key[32];
    hkdf_sha256(shared, 32, (uint8_t*)info, ilen, key);
    uint8_t pt[ONION_MAX_BYTES];
    size_t pt_len = 0;
    if (!aead_decrypt_xc20p(key, nonce, ct, ct_len, pt, &pt_len)) {
        ESP_LOGW(TAG, "AEAD fail");
        return;
    }

    char *s = strndup((char*)pt, pt_len);
    cJSON *pl = cJSON_Parse(s);
    if (!pl) { free(s); return; }
    const char *next = cJSON_GetObjectItem(pl, "next")->valuestring;
    const char *inner_hex = cJSON_GetObjectItem(pl, "inner")->valuestring;
    size_t inner_len = 0;
    uint8_t *inner = unhex(inner_hex, &inner_len);
    cJSON_Delete(pl);
    free(s);

    if (!strcmp(next, "LOCAL")) {
        ESP_LOGI(TAG, "Deliver to local phone (%u bytes E2EE)", (unsigned)inner_len);
        if (g_phone_client && g_phone_client.connected()) {
            g_phone_client.write(inner, inner_len);
            ESP_LOGI(TAG, "Pushed %u bytes to connected phone.", (unsigned)inner_len);
        } else {
            ESP_LOGW(TAG, "Packet for LOCAL, but no phone is connected.");
        }
        free(inner);
        return;
    }
    ESP_LOGI(TAG, "Forwarding peeled onion to %s", next);
    radio_send(next, inner, inner_len);
    free(inner);
}

void onion_on_frame(const uint8_t *buf, size_t len) {
    if (is_replay(buf, len)) {
        ESP_LOGW(TAG, "Replay attack detected! Dropping packet.");
        return;
    }
    peel_and_forward(buf, len);
}
