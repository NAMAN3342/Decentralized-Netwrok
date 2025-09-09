#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool storage_init(void);
bool storage_get_blob(const char *key, void *buf, size_t *len_inout);
bool storage_set_blob(const char *key, const void *buf, size_t len);
