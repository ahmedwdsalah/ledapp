#pragma once

#include "esp_err.h"

esp_err_t motif_wifi_start(void);
esp_err_t motif_wifi_provision(const char *ssid, const char *password);
