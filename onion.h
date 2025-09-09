#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <WiFi.h> // For WiFiClient

extern WiFiClient g_phone_client;

bool onion_build(const char **route, size_t route_len, const uint8_t *inner, size_t inner_len, uint8_t *out, size_t *out_len);
void onion_on_frame(const uint8_t *buf, size_t len);