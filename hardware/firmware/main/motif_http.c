#include "motif_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "motif_player.h"
#include "motif_state.h"
#include "motif_diagnostics.h"

static httpd_handle_t s_server;
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

static esp_err_t status_handler(httpd_req_t *req)
{
    if (!authorized(req)) return deny(req);
    uint32_t generation;
    motif_player_state_t state;
    motif_player_status(&generation, &state);
    char id[7], body[160];
    motif_state_device_id(id);
    const char *states[] = {"idle", "applying", "playing", "error"};
    snprintf(body, sizeof(body), "{\"protocol\":1,\"deviceId\":\"%s\",\"wifiConnected\":true,\"generation\":%lu,\"state\":\"%s\"}",
             id, (unsigned long)generation, states[state]);
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
    char type[32];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK || strcmp(type, "application/x-motif-animation") != 0) {
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
    uint8_t buffer[2048], header[12] = {0};
    int remaining = req->content_len;
    int received_total = 0;
    int next_progress = 64 * 1024;
    bool valid = true;
    while (remaining > 0) {
        int received = httpd_req_recv(req, (char *)buffer, remaining < sizeof(buffer) ? remaining : sizeof(buffer));
        if (received <= 0) {
            motif_diag_record("upload_receive_failed", received_total);
            ESP_LOGE(TAG, "Receive stopped: result=%d received=%d remaining=%d", received, received_total, remaining);
            valid = false;
            break;
        }
        int copy = received_total < 12 ? 12 - received_total : 0;
        if (copy > received) copy = received;
        if (copy) memcpy(header + received_total, buffer, copy);
        if (fwrite(buffer, 1, received, out) != received) {
            motif_diag_record("upload_write_failed", received_total);
            ESP_LOGE(TAG, "Media write failed after %d bytes", received_total);
            valid = false;
            break;
        }
        received_total += received;
        remaining -= received;
        if (received_total >= next_progress) {
            motif_diag_record("upload_received", received_total);
            ESP_LOGI(TAG, "Upload received %d/%d", received_total, req->content_len);
            next_progress += 64 * 1024;
        }
    }
    if (fclose(out) != 0) valid = false;
    motif_player_set_receiving(false);
    unsigned width = header[6] | (header[7] << 8);
    unsigned height = header[8] | (header[9] << 8);
    unsigned frames = header[10] | (header[11] << 8);
    valid = valid && remaining == 0 && memcmp(header, "MOTF", 4) == 0 && header[4] == 1 &&
            header[5] == 0 && width == 480 && height == 480 &&
            frames > 0 && frames <= 255;
    if (!valid) {
        motif_diag_record("upload_invalid", received_total);
        ESP_LOGE(TAG, "Upload rejected after %d/%d bytes", received_total, req->content_len);
        unlink(MOTIF_ANIMATION_PART_PATH);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid or incomplete animation");
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

esp_err_t motif_http_start(void)
{
    if (s_server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.stack_size = 8192;
    config.recv_wait_timeout = 180;
    config.send_wait_timeout = 30;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;
    httpd_uri_t status = {.uri = "/v1/status", .method = HTTP_GET, .handler = status_handler};
    httpd_uri_t diagnostics = {.uri = "/v1/diagnostics", .method = HTTP_GET, .handler = diagnostics_handler};
    httpd_uri_t upload = {.uri = "/v1/animation", .method = HTTP_POST, .handler = upload_handler};
    err = httpd_register_uri_handler(s_server, &status);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &diagnostics);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_server, &upload);
    if (err != ESP_OK) motif_http_stop();
    return err;
}

void motif_http_stop(void)
{
    if (s_server) { httpd_stop(s_server); s_server = NULL; }
}
