#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MOTIF_MAX_ANIMATION_BYTES (4 * 1024 * 1024)

esp_err_t motif_state_init(void);
void motif_state_device_id(char out[7]);
void motif_state_mac(uint8_t out[6]);
void motif_state_token(uint8_t out[16]);
void motif_state_token_hex(char out[33]);
bool motif_state_wifi_credentials(char ssid[33], char password[65]);
esp_err_t motif_state_save_wifi(const char *ssid, const char *password);
void motif_state_set_ip(const uint8_t ip[4]);
void motif_state_get_ip(uint8_t ip[4]);
