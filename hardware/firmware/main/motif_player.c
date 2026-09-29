#include "motif_player.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "lvgl.h"
#include "miniz.h"
#include "motif_state.h"
#include "motif_diagnostics.h"
#include "ST7701S.h"

#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 480
#define DISPLAY_FRAME_BYTES (DISPLAY_WIDTH * DISPLAY_HEIGHT * 2)
#define MAX_COMPRESSED_FRAME_BYTES (DISPLAY_FRAME_BYTES + 1024)

static const char *TAG = "motif_player";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static FILE *s_animation;
static uint8_t *s_compressed_frame;
static uint16_t *s_display_frame;
static uint16_t s_frame_count;
static uint16_t s_frame_index;
static uint32_t s_frame_due;
static uint32_t s_perf_started, s_perf_frames, s_perf_max_ms, s_perf_total_ms;
static lv_obj_t *s_message;
static lv_obj_t *s_detail;
static lv_obj_t *s_ring;
static uint32_t s_generation;
static uint32_t s_pending_generation;
static uint32_t s_passkey;
static bool s_passkey_dirty;
static bool s_connected;
static bool s_joining;
static bool s_network_error;
static bool s_receiving;
static uint8_t s_receive_percent;
static bool s_status_dirty;
static motif_player_state_t s_state = MOTIF_PLAYER_IDLE;

static uint16_t read16(const uint8_t *bytes) { return bytes[0] | ((uint16_t)bytes[1] << 8); }
static uint32_t read32(const uint8_t *bytes) {
    return bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

esp_err_t motif_player_mount(void)
{
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = "/media",
        .partition_label = "media",
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) ESP_LOGE(TAG, "Media mount failed: %s", esp_err_to_name(err));
    return err;
}

static bool read_frame(FILE *file, uint16_t *duration)
{
    uint8_t header[6];
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) return false;
    *duration = read16(header);
    uint32_t compressed = read32(header + 2);
    if (*duration < 20 || *duration > 1000 || compressed < 6 || compressed > MAX_COMPRESSED_FRAME_BYTES ||
        fread(s_compressed_frame, 1, compressed, file) != compressed) return false;
    size_t decoded = tinfl_decompress_mem_to_mem(s_display_frame, DISPLAY_FRAME_BYTES,
        s_compressed_frame, compressed, TINFL_FLAG_PARSE_ZLIB_HEADER);
    return decoded == DISPLAY_FRAME_BYTES;
}

static bool valid_animation(FILE *file, long length, uint16_t *frame_count)
{
    uint8_t header[12];
    if (length < 18 || length > MOTIF_MAX_ANIMATION_BYTES || fseek(file, 0, SEEK_SET) != 0 ||
        fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "MOTF", 4) != 0 || header[4] != 1 || header[5] != 0 ||
        read16(header + 6) != DISPLAY_WIDTH || read16(header + 8) != DISPLAY_HEIGHT) return false;
    uint16_t count = read16(header + 10), duration;
    if (!count || count > 255) return false;
    for (uint16_t i = 0; i < count; ++i) {
        if (!read_frame(file, &duration)) {
            motif_diag_record("frame_invalid", i);
            return false;
        }
        // Validation must not monopolize the main task during a long animation.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (ftell(file) != length || fseek(file, 12, SEEK_SET) != 0) return false;
    *frame_count = count;
    return true;
}

static bool draw_next_frame(void)
{
    uint32_t started = esp_log_timestamp();
    uint32_t frame_started = lv_tick_get();
    if (s_frame_index == s_frame_count) {
        if (fseek(s_animation, 12, SEEK_SET) != 0) return false;
        s_frame_index = 0;
    }
    uint16_t duration;
    if (!read_frame(s_animation, &duration)) {
        motif_diag_record("frame_decode_failed", s_frame_index);
        return false;
    }
    if (esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                  s_display_frame) != ESP_OK) return false;
    s_frame_index++;
    s_frame_due = frame_started + duration;
    uint32_t elapsed = esp_log_timestamp() - started;
    if (elapsed > s_perf_max_ms) s_perf_max_ms = elapsed;
    s_perf_total_ms += elapsed;
    s_perf_frames++;
    if (s_perf_started == 0) s_perf_started = esp_log_timestamp();
    if (esp_log_timestamp() - s_perf_started >= 5000) {
        motif_diag_record("playback_frames_5s", s_perf_frames);
        ESP_LOGI("motif_perf", "Playback frames=%lu/5s avg=%lums max=%lums", (unsigned long)s_perf_frames,
                 (unsigned long)(s_perf_total_ms / s_perf_frames), (unsigned long)s_perf_max_ms);
        s_perf_started = esp_log_timestamp();
        s_perf_frames = s_perf_max_ms = s_perf_total_ms = 0;
    }
    return true;
}

static bool play_current(void)
{
    FILE *incoming = fopen(MOTIF_ANIMATION_PATH, "rb");
    if (!incoming) return false;
    if (fseek(incoming, 0, SEEK_END) != 0) { fclose(incoming); return false; }
    long length = ftell(incoming);
    // Fixed buffers: total animation length never determines a RAM allocation.
    if (!s_compressed_frame) s_compressed_frame = heap_caps_malloc(MAX_COMPRESSED_FRAME_BYTES, MALLOC_CAP_SPIRAM);
    if (!s_display_frame) s_display_frame = heap_caps_malloc(DISPLAY_FRAME_BYTES, MALLOC_CAP_SPIRAM);
    if (!s_compressed_frame || !s_display_frame) {
        fclose(incoming);
        motif_diag_record("frame_buffer_alloc_failed", DISPLAY_FRAME_BYTES);
        ESP_LOGE(TAG, "Not enough PSRAM for frame buffers");
        return false;
    }
    uint16_t count;
    if (!valid_animation(incoming, length, &count)) {
        fclose(incoming);
        motif_diag_record("animation_invalid", length);
        return false;
    }
    FILE *previous = s_animation;
    uint16_t previous_count = s_frame_count, previous_index = s_frame_index;
    s_animation = incoming;
    s_frame_count = count;
    s_frame_index = 0;
    if (!draw_next_frame()) {
        s_animation = previous;
        s_frame_count = previous_count;
        s_frame_index = previous_index;
        fclose(incoming);
        return false;
    }
    if (previous) fclose(previous);
    if (s_message) lv_obj_add_flag(s_message, LV_OBJ_FLAG_HIDDEN);
    if (s_detail) lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
    if (s_ring) lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Streaming %u frames from flash (%ld bytes), buffers=%u bytes", count, length,
             DISPLAY_FRAME_BYTES + MAX_COMPRESSED_FRAME_BYTES);
    motif_diag_record("playback_started", count);
    return true;
}

static bool apply_animation(void)
{
    bool had_previous = access(MOTIF_ANIMATION_PATH, F_OK) == 0;
    if (had_previous) {
        if (unlink(MOTIF_ANIMATION_BACKUP_PATH) != 0 && errno != ENOENT) return false;
        if (rename(MOTIF_ANIMATION_PATH, MOTIF_ANIMATION_BACKUP_PATH) != 0) return false;
    }
    if (rename(MOTIF_ANIMATION_PART_PATH, MOTIF_ANIMATION_PATH) != 0) {
        if (had_previous) rename(MOTIF_ANIMATION_BACKUP_PATH, MOTIF_ANIMATION_PATH);
        return false;
    }
    if (!play_current()) {
        unlink(MOTIF_ANIMATION_PATH);
        if (had_previous && rename(MOTIF_ANIMATION_BACKUP_PATH, MOTIF_ANIMATION_PATH) != 0)
            ESP_LOGE(TAG, "Could not restore previous animation path");
        // The previous open file and its playback position remain valid on failure.
        return false;
    }
    if (had_previous) unlink(MOTIF_ANIMATION_BACKUP_PATH);
    return true;
}

void motif_player_init_ui(void)
{
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);
    s_ring = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_ring, 90, 90);
    lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(0xF05850), 0);
    lv_obj_set_style_border_width(s_ring, 3, 0);
    lv_obj_align(s_ring, LV_ALIGN_CENTER, 0, -76);
    s_message = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(s_message, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_message, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_message, "MOTIF");
    lv_obj_align(s_message, LV_ALIGN_CENTER, 0, 5);
    s_detail = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(s_detail, lv_color_hex(0xA0A0A0), 0);
    lv_label_set_text(s_detail, "Open Motif to connect");
    lv_obj_align(s_detail, LV_ALIGN_CENTER, 0, 42);

    if (access(MOTIF_ANIMATION_PATH, F_OK) != 0 && access(MOTIF_ANIMATION_BACKUP_PATH, F_OK) == 0) {
        rename(MOTIF_ANIMATION_BACKUP_PATH, MOTIF_ANIMATION_PATH);
    }
    unlink(MOTIF_ANIMATION_PART_PATH);
    if (play_current()) s_state = MOTIF_PLAYER_PLAYING;
    else if (access(MOTIF_ANIMATION_BACKUP_PATH, F_OK) == 0) {
        // Preserve the failed candidate until the known previous file is restored.
        if (rename(MOTIF_ANIMATION_PATH, MOTIF_ANIMATION_PART_PATH) == 0 || errno == ENOENT) {
            if (rename(MOTIF_ANIMATION_BACKUP_PATH, MOTIF_ANIMATION_PATH) == 0 && play_current())
                s_state = MOTIF_PLAYER_PLAYING;
            else s_state = MOTIF_PLAYER_ERROR;
        }
    } else if (access(MOTIF_ANIMATION_PATH, F_OK) == 0) s_state = MOTIF_PLAYER_ERROR;
    s_status_dirty = true;
}

bool motif_player_request_apply(uint32_t *generation)
{
    portENTER_CRITICAL(&s_mux);
    if (s_pending_generation != 0) { portEXIT_CRITICAL(&s_mux); return false; }
    s_pending_generation = s_generation + 1;
    *generation = s_pending_generation;
    s_state = MOTIF_PLAYER_APPLYING;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

void motif_player_status(uint32_t *generation, motif_player_state_t *state)
{
    portENTER_CRITICAL(&s_mux);
    *generation = s_pending_generation ? s_pending_generation : s_generation;
    *state = s_state;
    portEXIT_CRITICAL(&s_mux);
}

void motif_player_show_passkey(uint32_t passkey)
{
    portENTER_CRITICAL(&s_mux);
    s_passkey = passkey;
    s_passkey_dirty = true;
    portEXIT_CRITICAL(&s_mux);
}

void motif_player_clear_passkey(void) { motif_player_show_passkey(0); }

void motif_player_set_connection(bool connected, bool joining)
{
    portENTER_CRITICAL(&s_mux);
    s_connected = connected;
    s_joining = joining;
    s_network_error = false;
    s_status_dirty = true;
    portEXIT_CRITICAL(&s_mux);
}

void motif_player_set_network_error(void)
{
    portENTER_CRITICAL(&s_mux);
    s_connected = false;
    s_joining = false;
    s_network_error = true;
    s_status_dirty = true;
    portEXIT_CRITICAL(&s_mux);
}

void motif_player_set_receiving(bool receiving)
{
    portENTER_CRITICAL(&s_mux);
    s_receiving = receiving;
    if (receiving) s_receive_percent = 0;
    s_status_dirty = true;
    portEXIT_CRITICAL(&s_mux);
}

void motif_player_set_receive_progress(uint8_t percent)
{
    portENTER_CRITICAL(&s_mux);
    if (s_receive_percent != percent) {
        s_receive_percent = percent;
        s_status_dirty = true;
    }
    portEXIT_CRITICAL(&s_mux);
}

void motif_player_loop(void)
{
    uint32_t passkey, pending;
    bool connected, joining, receiving, network_error, status_dirty, passkey_dirty;
    uint8_t receive_percent;
    portENTER_CRITICAL(&s_mux);
    passkey = s_passkey;
    pending = s_pending_generation;
    connected = s_connected;
    joining = s_joining;
    receiving = s_receiving;
    receive_percent = s_receive_percent;
    network_error = s_network_error;
    status_dirty = s_status_dirty;
    passkey_dirty = s_passkey_dirty;
    s_status_dirty = s_passkey_dirty = false;
    portEXIT_CRITICAL(&s_mux);

    if ((passkey_dirty || status_dirty) && s_message) {
        if (passkey) {
            char code[12];
            snprintf(code, sizeof(code), "%06lu", (unsigned long)passkey);
            lv_label_set_text(s_message, code);
            lv_label_set_text(s_detail, "Enter this code on your phone");
        } else if (receiving) {
            char text[24];
            snprintf(text, sizeof(text), "Receiving %u%%", (unsigned)receive_percent);
            lv_label_set_text(s_message, text);
            lv_label_set_text(s_detail, "Keep the display powered on");
        } else if (joining) {
            lv_label_set_text(s_message, "Connecting");
            lv_label_set_text(s_detail, "Joining Wi-Fi");
        } else if (network_error) {
            lv_label_set_text(s_message, "Wi-Fi failed");
            lv_label_set_text(s_detail, "Check network details in Motif");
        } else if (s_state == MOTIF_PLAYER_ERROR) {
            lv_label_set_text(s_message, "Can't play animation");
            lv_label_set_text(s_detail, "Try a different one in Motif");
        } else if (connected) {
            lv_label_set_text(s_message, "MOTIF");
            lv_label_set_text(s_detail, "Wi-Fi ready. Open the app.");
        } else {
            lv_label_set_text(s_message, "MOTIF");
            lv_label_set_text(s_detail, "Open Motif to connect");
        }
        if (s_animation && !receiving) {
            lv_obj_add_flag(s_message, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_message, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_ring);
            lv_obj_move_foreground(s_message);
            lv_obj_move_foreground(s_detail);
        }
    }

    if (pending) {
        bool applied = apply_animation();
        portENTER_CRITICAL(&s_mux);
        s_generation = pending;
        s_pending_generation = 0;
        s_state = applied ? MOTIF_PLAYER_PLAYING : MOTIF_PLAYER_ERROR;
        s_status_dirty = true;
        portEXIT_CRITICAL(&s_mux);
        motif_diag_record(applied ? "animation_applied" : "animation_apply_failed", pending);
    }

    if (s_animation && !receiving && (s_state == MOTIF_PLAYER_PLAYING || s_state == MOTIF_PLAYER_ERROR) &&
        (int32_t)(lv_tick_get() - s_frame_due) >= 0) {
        if (!draw_next_frame()) {
            ESP_LOGE(TAG, "Frame decompression failed");
            fclose(s_animation);
            s_animation = NULL;
            portENTER_CRITICAL(&s_mux);
            s_state = MOTIF_PLAYER_ERROR;
            s_status_dirty = true;
            portEXIT_CRITICAL(&s_mux);
        }
    }
}

bool motif_player_has_animation(void) { return s_animation != NULL; }
bool motif_player_is_receiving(void) { return s_receiving; }
