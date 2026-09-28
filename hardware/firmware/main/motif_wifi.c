#include "motif_wifi.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "motif_http.h"
#include "motif_player.h"
#include "motif_state.h"

static const char *TAG = "motif_wifi";
static bool s_wifi_started;

static void configure_station(const char *ssid, const char *password)
{
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    config.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
}

static void event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        char ssid[33], password[65];
        if (motif_state_wifi_credentials(ssid, password)) {
            motif_player_set_connection(false, true);
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = event_data;
        const uint8_t empty_ip[4] = {0};
        motif_state_set_ip(empty_ip);
        motif_http_stop();
        if (disconnected->reason == WIFI_REASON_AUTH_FAIL ||
            disconnected->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
            disconnected->reason == WIFI_REASON_NO_AP_FOUND) {
            motif_player_set_network_error();
        } else {
            motif_player_set_connection(false, true);
        }
        char ssid[33], password[65];
        if (motif_state_wifi_credentials(ssid, password)) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got = event_data;
        uint8_t ip[4] = {IP2STR(&got->ip_info.ip)};
        motif_state_set_ip(ip);
        motif_player_set_connection(true, false);
        esp_err_t err = motif_http_start();
        if (err != ESP_OK) ESP_LOGE(TAG, "HTTP start failed: %s", esp_err_to_name(err));
        ESP_LOGI(TAG, "Connected with IP " IPSTR, IP2STR(&got->ip_info.ip));
    }
}

esp_err_t motif_wifi_start(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    if (!esp_netif_create_default_wifi_sta()) return ESP_FAIL;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err != ESP_OK) return err;
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    char ssid[33], password[65];
    if (motif_state_wifi_credentials(ssid, password)) configure_station(ssid, password);
    err = esp_wifi_start();
    s_wifi_started = err == ESP_OK;
    return err;
}

esp_err_t motif_wifi_provision(const char *ssid, const char *password)
{
    if (!s_wifi_started) return ESP_ERR_INVALID_STATE;
    esp_err_t err = motif_state_save_wifi(ssid, password);
    if (err != ESP_OK) return err;
    err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) return err;
    configure_station(ssid, password);
    motif_player_set_connection(false, true);
    return esp_wifi_connect();
}
