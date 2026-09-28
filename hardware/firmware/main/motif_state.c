#include "motif_state.h"

#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static SemaphoreHandle_t s_lock;
static uint8_t s_mac[6];
static uint8_t s_token[16];
static uint8_t s_ip[4];
static char s_ssid[33];
static char s_password[65];

esp_err_t motif_state_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    err = esp_read_mac(s_mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;

    nvs_handle_t handle;
    err = nvs_open("motif", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    size_t token_len = sizeof(s_token);
    err = nvs_get_blob(handle, "upload_key", s_token, &token_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(s_token, sizeof(s_token));
        token_len = sizeof(s_token);
        err = nvs_set_blob(handle, "upload_key", s_token, sizeof(s_token));
        if (err == ESP_OK) err = nvs_commit(handle);
    }
    if (err == ESP_OK && token_len != sizeof(s_token)) err = ESP_ERR_INVALID_SIZE;
    if (err == ESP_OK) {
        size_t len = sizeof(s_ssid);
        if (nvs_get_str(handle, "ssid", s_ssid, &len) != ESP_OK) s_ssid[0] = '\0';
        len = sizeof(s_password);
        if (nvs_get_str(handle, "wifi_pass", s_password, &len) != ESP_OK) s_password[0] = '\0';
    }
    nvs_close(handle);
    return err;
}

void motif_state_device_id(char out[7])
{
    snprintf(out, 7, "%02X%02X%02X", s_mac[3], s_mac[4], s_mac[5]);
}

void motif_state_mac(uint8_t out[6]) { memcpy(out, s_mac, 6); }
void motif_state_token(uint8_t out[16]) { memcpy(out, s_token, 16); }

void motif_state_token_hex(char out[33])
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; i++) {
        out[2 * i] = hex[s_token[i] >> 4];
        out[2 * i + 1] = hex[s_token[i] & 0xf];
    }
    out[32] = '\0';
}

bool motif_state_wifi_credentials(char ssid[33], char password[65])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strcpy(ssid, s_ssid);
    strcpy(password, s_password);
    bool available = s_ssid[0] != '\0';
    xSemaphoreGive(s_lock);
    return available;
}

esp_err_t motif_state_save_wifi(const char *ssid, const char *password)
{
    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(password);
    if (ssid_len == 0 || ssid_len > 32 || pass_len > 63) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("motif", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "wifi_pass", password);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        memcpy(s_ssid, ssid, ssid_len + 1);
        memcpy(s_password, password, pass_len + 1);
        xSemaphoreGive(s_lock);
    }
    return err;
}

void motif_state_set_ip(const uint8_t ip[4])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_ip, ip, 4);
    xSemaphoreGive(s_lock);
}

void motif_state_get_ip(uint8_t ip[4])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(ip, s_ip, 4);
    xSemaphoreGive(s_lock);
}
