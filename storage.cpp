#include "storage.h"
#include <Preferences.h>

Preferences preferences;

bool storage_init(void) {
    return preferences.begin("fusion", false);
}

bool storage_get_blob(const char *key, void *buf, size_t *len) {
    if (!preferences.isKey(key)) return false;
    size_t stored_len = preferences.getBytesLength(key);
    if (*len < stored_len) return false;
    *len = preferences.getBytes(key, buf, *len);
    return *len > 0;
}

bool storage_set_blob(const char *key, const void *buf, size_t len) {
    return preferences.putBytes(key, buf, len) == len;
}


