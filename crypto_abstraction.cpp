#include "crypto_abstraction.h"
#include "storage.h"
#include "mbedtls/md.h"
#include "esp_random.h"
#include <string.h>
#include <cstdio> // For sprintf, sscanf

static uint8_t x_priv[32], x_pub[32];
static uint8_t e_pub[32], e_priv[64];  // Back to 64 bytes - might be seed + expanded key

void random_bytes(uint8_t *out, size_t n) {
    esp_fill_random(out, n);
}

void crypto_keys_load_or_create(void) {
    size_t l_x = 32, l_e = 64;  
    if (!storage_get_blob("x_priv", x_priv, &l_x) || l_x != 32) {
        random_bytes(x_priv, 32);
        crypto_x25519_public_key(x_pub, x_priv);
        storage_set_blob("x_priv", x_priv, 32);
        storage_set_blob("x_pub", x_pub, 32);
    } else {
        crypto_x25519_public_key(x_pub, x_priv);
    }

    if (!storage_get_blob("e_priv", e_priv, &l_e) || l_e != 64) {
        uint8_t seed[32];
        random_bytes(seed, 32);
        crypto_eddsa_key_pair(e_priv, e_pub, seed);  // This is the correct function
        storage_set_blob("e_priv", e_priv, 64);
        storage_set_blob("e_pub", e_pub, 32);
    }
    // Load public key separately if we loaded private key from storage  
    size_t l_e_pub = 32;
    storage_get_blob("e_pub", e_pub, &l_e_pub);
}

const uint8_t* crypto_get_x25519_public(void) { return x_pub; }
const uint8_t* crypto_get_x25519_private(void) { return x_priv; }
const uint8_t* crypto_get_ed25519_public(void) { return e_pub; }

void crypto_sign(uint8_t signature[64], const uint8_t *msg, size_t msg_len) {
    crypto_eddsa_sign(signature, e_priv, msg, msg_len);  // Correct function from monocypher.h
}

bool crypto_verify(const uint8_t signature[64], const uint8_t pub_key[32], const uint8_t *msg, size_t msg_len) {
    return crypto_eddsa_check(signature, pub_key, msg, msg_len) == 0;  // Correct function from monocypher.h
}

void x25519_ephemeral(eph_kp_t *kp) { random_bytes(kp->priv, 32); crypto_x25519_public_key(kp->pub, kp->priv); }
void x25519_shared(const uint8_t my_priv[32], const uint8_t peer_pub[32], uint8_t out[32]) { crypto_x25519(out, my_priv, peer_pub); }

static void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[32]) {
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, key, key_len);
    mbedtls_md_hmac_update(&ctx, data, data_len);
    mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);
}

void hkdf_sha256(const uint8_t *ikm, size_t ikm_len, const uint8_t *info, size_t info_len, uint8_t out32[32]) {
    uint8_t prk[32];
    uint8_t salt[32] = {0};
    hmac_sha256(salt, 32, ikm, ikm_len, prk);
    uint8_t t[32];
    uint8_t c = 1;
    uint8_t buf[info_len + 1];
    memcpy(buf, info, info_len);
    buf[info_len] = c;
    hmac_sha256(prk, 32, buf, info_len + 1, t);
    memcpy(out32, t, 32);
}

bool aead_encrypt_xc20p(const uint8_t key[32], const uint8_t nonce24[24], const uint8_t *pt, size_t pt_len, uint8_t *ct, size_t *ct_len) {
    uint8_t *mac = ct;          // mac first 16 bytes
    uint8_t *out_ct = ct + 16;  // ciphertext after mac
    crypto_aead_lock(out_ct, mac, key, nonce24, NULL, 0, pt, pt_len);  // Correct parameter order
    *ct_len = pt_len + 16;
    return true;
}

bool aead_decrypt_xc20p(const uint8_t key[32], const uint8_t nonce24[24], const uint8_t *ct, size_t ct_len, uint8_t *pt, size_t *pt_len) {
    if (ct_len < 16) return false;
    const uint8_t* mac = ct;
    const uint8_t* in_ct = ct + 16;
    size_t plen = ct_len - 16;
    int rc = crypto_aead_unlock(pt, mac, key, nonce24, NULL, 0, in_ct, plen);  // Correct parameter order
    if (rc != 0) return false;
    *pt_len = plen;
    return true;
}