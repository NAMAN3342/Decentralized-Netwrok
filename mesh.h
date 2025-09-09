#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void mesh_init(void);
bool mesh_choose_route(const char *dest_id, const char **route_out, size_t *route_len);
void mesh_on_radio_frame(const uint8_t *buf, size_t len);
bool mesh_get_x25519_pub(const char *node_id, uint8_t out_pub[32]);