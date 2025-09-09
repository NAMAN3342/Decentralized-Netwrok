#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void dtn_init(void);
bool dtn_enqueue(const char *dest, const uint8_t *payload, size_t len);