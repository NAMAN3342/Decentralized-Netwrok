#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "src/monocypher/monocypher.h"
#ifdef __cplusplus
}
#endif

void crypto_keys_load_or_create(void);
const uint8_t* crypto_get_x25519_public(void);
const uint8_t* crypto_get_x25519_private(void);
const uint8_t* crypto_get_ed25519_public(void);

void crypto_sign(uint8_t signature[64], const uint8_t *msg, size_t msg_len);
bool crypto_verify(const uint8_t signature[64], const uint8_t pub_key[32], const uint8_t *msg, size_t msg_len);

typedef struct { uint8_t priv[32]; uint8_t pub[32]; } eph_kp_t;
void x25519_ephemeral(eph_kp_t *kp);
void x25519_shared(const uint8_t my_priv[32], const uint8_t peer_pub[32], uint8_t out[32]);

void hkdf_sha256(const uint8_t *ikm, size_t ikm_len, const uint8_t *info, size_t info_len, uint8_t out32[32]);

bool aead_encrypt_xc20p(const uint8_t key[32], const uint8_t nonce24[24], const uint8_t *pt, size_t pt_len, uint8_t *ct, size_t *ct_len);
bool aead_decrypt_xc20p(const uint8_t key[32], const uint8_t nonce24[24], const uint8_t *ct, size_t ct_len, uint8_t *pt, size_t *pt_len);

void random_bytes(uint8_t *out, size_t n);