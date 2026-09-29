#include "motif_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "motif_player.h"
#include "motif_state.h"
#include "motif_diagnostics.h"

static httpd_handle_t s_server;
static httpd_handle_t s_progress_server;
static const char *TAG = "motif_http";

static bool authorized(httpd_req_t *req)
{
    char actual[48] = {0};
    char expected[33];
    motif_state_token_hex(expected);
    if (httpd_req_get_hdr_value_str(req, "Authorization", actual, sizeof(actual)) != ESP_OK ||
        strlen(actual) != 39 || memcmp(actual, "Bearer ", 7) != 0) return false;
    unsigned diff = 0;
    for (int i = 0; i < 32; ++i) diff |= (unsigned)(actual[i + 7] ^ expected[i]);
    return diff == 0;
}

static esp_err_t deny(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    return httpd_resp_sendstr(req, "Unauthorized");
}

static volatile int s_upload_received;
static volatile int s_upload_total;

static esp_err_t status_handler(httpd_req_t *req)
{
    if (!authorized(req)) return deny(req);
    uint32_t generation;
    motif_player_state_t state;
    motif_player_status(&generation, &state);
    char id[7], body[192];
    motif_state_device_id(id);
    const char *states[] = {"idle", "applying", "playing", "error"};
    snprintf(body, sizeof(body), "{\"protocol\":1,\"deviceId\":\"%s\",\"wifiConnected\":true,\"generation\":%lu,\"state\":\"%s\",\"uploadBytes\":%d,\"uploadTotal\":%d}",
             id, (unsigned long)generation, states[state], s_upload_received, s_upload_total);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t progress_handler(httpd_req_t *req)
{
    if (!authorized(req)) return deny(req);
    char body[64];
    snprintf(body, sizeof(body), "{\"uploadBytes\":%d,\"uploadTotal\":%d}", s_upload_received, s_upload_total);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t diagnostics_handler(httpd_req_t *req)
{
    if (!authorized(req)) return deny(req);
    char *body = malloc(4096);
    if (!body || !motif_diag_json(body, 4096)) {
        free(body);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Diagnostics unavailable");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_sendstr(req, body);
    free(body);
    return result;
}

static esp_err_t color_test_handler(httpd_req_t *req)
{
    if (!authorized(req)) return deny(req);
    char query[64], value[8] = {0};
    int pattern = -1, profile = -1;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "pattern", value, sizeof(value)) == ESP_OK) pattern = atoi(value);
        value[0] = 0;
        if (httpd_query_key_value(query, "profile", value, sizeof(value)) == ESP_OK) profile = atoi(value);
    }
    if (pattern < -1 || pattern > 24 || profile < -1 || profile > 3 ||
        (profile >= 0 && motif_player_set_color_profile((uint8_t)profile) != ESP_OK) ||
        (pattern >= 0 && motif_player_show_color_test((uint8_t)pattern) != ESP_OK)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "pattern 0..24 (24 resumes), profile 0..3");
        return ESP_FAIL;
    }
    char body[64];
    snprintf(body, sizeof(body), "{\"pattern\":%d,\"profile\":%u}", pattern,
             (unsigned)motif_player_color_profile());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t upload_handler(httpd_req_t *req)
{
    if (!authorized(req)) return deny(req);
    motif_diag_record("upload_started", req->content_len);
    ESP_LOGI(TAG, "Upload request: content_len=%d", req->content_len);
    uint32_t current_generation;
    motif_player_state_t current_state;
    motif_player_status(&current_generation, &current_state);
    if (current_state == MOTIF_PLAYER_APPLYING) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Previous upload still applying");
    }
    if (req->content_len < 18 || req->content_len > MOTIF_MAX_ANIMATION_BYTES) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_sendstr(req, "Animation must be 18 bytes to 4 MiB");
        return ESP_FAIL;
    }
    char type[32] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK) type[0] = '\0';
    if (strcmp(type, "application/x-motif-animation") != 0 && strcmp(type, "application/octet-stream") != 0) {
        ESP_LOGW(TAG, "Upload rejected: content type '%s'", type[0] ? type : "(none)");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected Motif animation");
        return ESP_FAIL;
    }
    FILE *out = fopen(MOTIF_ANIMATION_PART_PATH, "wb");
    if (!out) {
        motif_diag_record("upload_open_failed", 0);
        ESP_LOGE(TAG, "Cannot open media upload file");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Media unavailable");
        return ESP_FAIL;
    }
    motif_player_set_receiving(true);
    uint8_t buffer[4096], header[12] = {0};
    int remaining = req->content_len;
    int received_total = 0;
    s_upload_total = req->content_len;
    s_upload_received = 0;
    int next_progress = 64 * 1024;
    bool valid = true;
    const char *failure = NULL;
    while (remaining > 0) {
        int received = httpd_req_recv(req, (char *)buffer, remaining < sizeof(buffer) ? remaining : sizeof(buffer));
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received <= 0) {
            motif_diag_record("upload_receive_failed", received_total);
            ESP_LOGE(TAG, "Receive stopped: result=%d received=%d remaining=%d", received, received_total, remaining);
            failure = "Upload receive interrupted";
            valid = false;
            break;
        }
        int copy = received_total < 12 ? 12 - received_total : 0;
        if (copy > received) copy = received;
        if (copy) memcpy(header + received_total, buffer, copy);
        if (fwrite(buffer, 1, received, out) != received) {
            motif_diag_record("upload_write_failed", received_total);
            ESP_LOGE(TAG, "Media write failed after %d bytes", received_total);
            failure = "Display storage write failed";
            valid = false;
            break;
        }
        received_total += received;
        s_upload_received = received_total;
        remaining -= received;
        motif_player_set_receive_progress((uint8_t)((uint64_t)received_total * 100u / (uint32_t)req->content_len));
        if (received_total >= next_progress) {
            motif_diag_record("upload_received", received_total);
            ESP_LOGI(TAG, "Upload received %d/%d", received_total, req->content_len);
            next_progress += 64 * 1024;
        }
    }
    if (fclose(out) != 0) { valid = false; failure = "Display storage close failed"; }
    motif_player_set_receiving(false);
    s_upload_received = 0;
    s_upload_total = 0;
    if (valid && remaining != 0) failure = "Upload ended before all bytes arrived";
    else if (valid) failure = motif_player_header_problem(header, req->content_len);
    valid = valid && failure == NULL;
    if (!valid) {
        motif_diag_record("upload_invalid", received_total);
        ESP_LOGE(TAG, "Upload rejected after %d/%d bytes", received_total, req->content_len);
        unlink(MOTIF_ANIMATION_PART_PATH);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, failure ? failure : "Invalid animation upload");
        return ESP_FAIL;
    }
    uint32_t generation;
    if (!motif_player_request_apply(&generation)) {
        motif_diag_record("upload_busy", received_total);
        unlink(MOTIF_ANIMATION_PART_PATH);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "Previous upload still applying");
        return ESP_FAIL;
    }
    char body[48];
    snprintf(body, sizeof(body), "{\"generation\":%lu}", (unsigned long)generation);
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    esp_err_t sent = httpd_resp_sendstr(req, body);
    motif_diag_record("upload_response", sent);
    ESP_LOGI(TAG, "Upload complete: %d bytes, response=%s", received_total, esp_err_to_name(sent));
    return sent;
}

static void firmware_reboot_task(void *argument)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static esp_err_t firmware_upload_handler(httpd_req_t *req)
{
    if (!authorized(req)) return deny(req);
    motif_diag_record("firmware_update_started", req->content_len);
    ESP_LOGI(TAG, "Firmware update: content_len=%d", req->content_len);
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target || req->content_len < 64 * 1024 || (uint32_t)req->content_len > target->size) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Firmware size is invalid");
    }
    esp_ota_handle_t handle;
    if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware slot unavailable");
        return ESP_FAIL;
    }
    uint8_t buffer[4096];
    int remaining = req->content_len;
    int received_total = 0;
    bool failed = false;
    while (remaining > 0) {
        int received = httpd_req_recv(req, (char *)buffer, remaining < sizeof(buffer) ? remaining : sizeof(buffer));
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received <= 0 || esp_ota_write(handle, buffer, received) != ESP_OK) {
            failed = true;
            break;
        }
        received_total += received;
        remaining -= received;
    }
    if (failed) esp_ota_abort(handle);
    else if (esp_ota_end(handle) != ESP_OK) failed = true;
    else if (esp_ota_set_boot_partition(target) != ESP_OK) failed = true;
    if (failed) {
        motif_diag_record("firmware_update_failed", received_total);
        ESP_LOGE(TAG, "Firmware update failed after %d bytes", received_total);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware update failed");
        return ESP_FAIL;
    }
    motif_diag_record("firmware_updated", received_total);
    ESP_LOGI(TAG, "Firmware updated: %d bytes, restarting", received_total);
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/json");
    esp_err_t sent = httpd_resp_sendstr(req, "{\"reboot\":true}");
    xTaskCreate(firmware_reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
    return sent;
}

esp_err_t motif_http_start(void)
{
    if (s_server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.stack_size = 12288;
    config.recv_wait_timeout = 180;
    config.send_wait_timeout = 30;
    config.keep_alive_enable = true;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;
    httpd_uri_t status = {.uri = "/v1/status", .method = HTTP_GET, .handler = status_handler};
    httpd_uri_t diagnostics = {.uri = "/v1/diagnostics", .method = HTTP_GET, .handler = diagnostics_handler};
    httpd_uri_t color_test = {.uri = "/v1/color-test", .method = HTTP_GET, .handler = color_test_handler};
    httpd_uri_t upload = {.uri = "/v1/animation", .method = HTTP_POST, .handler = upload_handler};
    httpd_uri_t firmware = {.uri = "/v1/firmware", .method = HTTP_POST, .handler = firmware_upload_handler};
    err = httpd_register_uri_handler(s_server, &status);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &diagnostics);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &color_test);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &upload);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &firmware);
    if (err != ESP_OK) {
        motif_http_stop();
        return err;
    }
    httpd_config_t progress_config = HTTPD_DEFAULT_CONFIG();
    progress_config.server_port = 8081;
    progress_config.ctrl_port = 32769;
    progress_config.max_open_sockets = 2;
    progress_config.stack_size = 4096;
    progress_config.lru_purge_enable = true;
    if (httpd_start(&s_progress_server, &progress_config) != ESP_OK) {
        ESP_LOGW(TAG, "Progress server failed to start");
        s_progress_server = NULL;
        return ESP_OK;
    }
    httpd_uri_t progress = {.uri = "/v1/progress", .method = HTTP_GET, .handler = progress_handler};
    if (httpd_register_uri_handler(s_progress_server, &progress) != ESP_OK) {
        ESP_LOGW(TAG, "Progress endpoint registration failed");
    }
    return ESP_OK;
}

void motif_http_stop(void)
{
    if (s_progress_server) { httpd_stop(s_progress_server); s_progress_server = NULL; }
    if (s_server) { httpd_stop(s_server); s_server = NULL; }
}
