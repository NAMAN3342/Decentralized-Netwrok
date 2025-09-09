#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void radio_init(void);
bool radio_send(const char *next_hop_id, const uint8_t *buf, size_t len);
void mesh_on_radio_frame(const uint8_t *buf, size_t len);
