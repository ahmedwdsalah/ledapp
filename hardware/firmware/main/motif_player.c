#include "motif_player.h"

#include <math.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "miniz.h"
#include "motif_state.h"
#include "motif_diagnostics.h"
#include "ST7701S.h"

#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 480
#define DISPLAY_FRAME_BYTES (DISPLAY_WIDTH * DISPLAY_HEIGHT * 2)
#define MAX_COMPRESSED_FRAME_BYTES (DISPLAY_FRAME_BYTES + 64 * 1024)

// Panel frame buffers: handing the driver one of its own buffers switches the
// scanout at a frame boundary, so a full-screen frame is never copied or
// decoded while the previous frame is still being scanned out. Three buffers
// let playback compose the next frame while two are still in flight.
#define DISPLAY_FB_COUNT 3

// 0=identity, 1=neutral LCD contrast. Keep identity as rollback value.
#ifndef MOTIF_COLOR_PROFILE
#define MOTIF_COLOR_PROFILE 0
#endif

static const char *TAG = "motif_player";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static FILE *s_animation;
static uint8_t *s_compressed_frame;
static uint16_t *s_frames[DISPLAY_FB_COUNT];
static uint16_t *s_validation_frame;
// Frame buffer 0 is the panel's boot scanout buffer; start on the next one.
static uint8_t s_write_fb = 1;
static bool s_delta;
static bool s_split;
static uint16_t s_frame_count;
static uint16_t s_frame_index;
static uint32_t s_frame_due;
static uint32_t s_perf_started, s_perf_frames, s_perf_max_ms, s_perf_total_ms;
static uint32_t s_perf_read_ms, s_perf_decode_ms;
static uint32_t s_frame_phase_read_ms, s_frame_phase_decode_ms;
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
static int8_t s_color_test = -1;
static uint8_t s_color_profile = MOTIF_COLOR_PROFILE;
static uint8_t s_lut5[4][32];
static uint8_t s_lut6[4][64];
static bool s_color_luts_ready;
static bool present_frame(void);
static bool decode_mem(const uint8_t *input, size_t length, uint8_t *output, size_t output_bytes);
static void s_decode_worker(void *argument);

// A decode worker on the second core inflates one half of each frame while the
// main task inflates the other, roughly doubling the playback rate ceiling.
static TaskHandle_t s_decode_task;
static SemaphoreHandle_t s_decode_start;
static SemaphoreHandle_t s_decode_done;
static const uint8_t *s_worker_input;
static size_t s_worker_length, s_worker_output_bytes;
static uint8_t *s_worker_output;
static const uint16_t *s_worker_previous;
static bool s_worker_pending, s_worker_ok;

static uint8_t color_correct_channel(uint8_t level, uint8_t max_value, uint8_t profile)
{
    float value = (float)level / max_value;
    if (profile >= 2) value = powf(value, 0.9f);
    if (profile & 1) value = (value - .5f) * 1.06f + .5f;
    if (value < 0) value = 0;
    if (value > 1) value = 1;
    return (uint8_t)lroundf(value * max_value);
}

static void color_luts_build(void)
{
    for (uint8_t profile = 0; profile < 4; ++profile) {
        for (uint8_t level = 0; level < 32; ++level)
            s_lut5[profile][level] = color_correct_channel(level, 31, profile);
        for (uint8_t level = 0; level < 64; ++level)
            s_lut6[profile][level] = color_correct_channel(level, 63, profile);
    }
    s_color_luts_ready = true;
}

static void correct_frame_colors(void)
{
    if (s_color_profile == 0) return;
    uint16_t *frame = s_frames[s_write_fb];
    const uint8_t *lut5 = s_lut5[s_color_profile];
    const uint8_t *lut6 = s_lut6[s_color_profile];
    for (size_t i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; ++i) {
        uint16_t pixel = frame[i];
        frame[i] = (uint16_t)((uint16_t)lut5[pixel >> 11] << 11) |
                   (uint16_t)((uint16_t)lut6[(pixel >> 5) & 63] << 5) | lut5[pixel & 31];
    }
}

static bool acquire_frame_buffers(void)
{
    if (!s_color_luts_ready) color_luts_build();
    if (!s_decode_start) {
        s_decode_start = xSemaphoreCreateBinary();
        s_decode_done = xSemaphoreCreateBinary();
    }
    if (s_decode_start && s_decode_done && !s_decode_task) {
        xTaskCreatePinnedToCore(s_decode_worker, "motif_decode", 16384, NULL, 5, &s_decode_task, 1);
        ESP_LOGI(TAG, "Split decode worker %s", s_decode_task ? "ready on core 1" : "unavailable");
    }
    if (!s_compressed_frame) s_compressed_frame = heap_caps_malloc(MAX_COMPRESSED_FRAME_BYTES, MALLOC_CAP_SPIRAM);
    // Validation decompresses into its own scratch so it never writes into the
    // buffer that is currently displayed.
    if (!s_validation_frame) s_validation_frame = heap_caps_malloc(DISPLAY_FRAME_BYTES, MALLOC_CAP_SPIRAM);
    if (!s_frames[0] && esp_lcd_rgb_panel_get_frame_buffer(panel_handle, DISPLAY_FB_COUNT,
            (void **)&s_frames[0], (void **)&s_frames[1], (void **)&s_frames[2]) != ESP_OK) {
        motif_diag_record("frame_buffer_unavailable", DISPLAY_FB_COUNT);
    }
    if (!s_compressed_frame || !s_validation_frame || !s_frames[0]) {
        motif_diag_record("frame_buffer_alloc_failed", DISPLAY_FRAME_BYTES);
        ESP_LOGE(TAG, "Not enough PSRAM for frame buffers");
        return false;
    }
    return true;
}

esp_err_t motif_player_show_color_test(uint8_t pattern)
{
    if (pattern == 24) { s_color_test = -1; return ESP_OK; }
    if (pattern > 23 || !s_frames[0]) return ESP_ERR_INVALID_ARG;
    uint16_t *frame = s_frames[s_write_fb];
    for (size_t y = 0; y < DISPLAY_HEIGHT; ++y) {
        for (size_t x = 0; x < DISPLAY_WIDTH; ++x) {
            uint8_t red = 0, green = 0, blue = 0;
            uint8_t step = (uint8_t)((x * 16) / DISPLAY_WIDTH);
            switch (pattern) {
                case 0: red = green = blue = 0; break;
                case 1: red = green = blue = 31; green = 63; break;
                case 2: red = 31; break;
                case 3: green = 63; break;
                case 4: blue = 31; break;
                case 5: red = 31; green = 63; break;
                case 6: green = 63; blue = 31; break;
                case 7: red = 31; blue = 31; break;
                case 8: red = green = blue = step * 2; if (red > 31) red = 31; green = step * 4; if (green > 63) green = 63; break;
                case 9: red = step * 2; if (red > 31) red = 31; break;
                case 10: green = step * 4; if (green > 63) green = 63; break;
                case 11: blue = step * 2; if (blue > 31) blue = 31; break;
                default:
                    red = (pattern & 1) ? (uint8_t)(1u << ((pattern - 12) % 5)) : 0;
                    green = (pattern & 2) ? (uint8_t)(1u << ((pattern - 12) % 6)) : 0;
                    blue = (pattern & 4) ? (uint8_t)(1u << ((pattern - 12) % 5)) : 0;
                    break;
            }
            frame[y * DISPLAY_WIDTH + x] = (uint16_t)(red << 11) | (uint16_t)(green << 5) | blue;
        }
    }
    correct_frame_colors();
    if (!present_frame()) return ESP_FAIL;
    s_color_test = (int8_t)pattern;
    return ESP_OK;
}

esp_err_t motif_player_set_color_profile(uint8_t profile)
{
    if (profile > 3) return ESP_ERR_INVALID_ARG;
    s_color_profile = profile;
    motif_diag_record("color_profile", profile);
    // Redraw a displayed test pattern so the change is visible immediately.
    if (s_color_test >= 0) return motif_player_show_color_test((uint8_t)s_color_test);
    return ESP_OK;
}

uint8_t motif_player_color_profile(void) { return s_color_profile; }

static uint16_t read16(const uint8_t *bytes) { return bytes[0] | ((uint16_t)bytes[1] << 8); }
static uint32_t read32(const uint8_t *bytes) {
    return bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

// Single source of truth for what the firmware accepts. Playback and the HTTP
// upload handler both call this, so a format change can never be half-applied.
const char *motif_player_header_problem(const uint8_t *header, long length)
{
    if (length < 18 || length > MOTIF_MAX_ANIMATION_BYTES) return "Animation size is invalid";
    if (memcmp(header, "MOTF", 4) != 0) return "Animation signature is invalid";
    if (header[4] != 1 || (header[5] & ~MOTIF_SUPPORTED_FLAGS) != 0) return "Animation version is unsupported";
    if (read16(header + 6) != DISPLAY_WIDTH || read16(header + 8) != DISPLAY_HEIGHT)
        return "Animation must be 480 by 480";
    uint16_t frames = read16(header + 10);
    if (frames == 0 || frames > 255) return "Animation frame count is invalid";
    return NULL;
}

static bool decode_mem(const uint8_t *input, size_t length, uint8_t *output, size_t output_bytes)
{
    size_t decoded = tinfl_decompress_mem_to_mem(output, output_bytes, input, length, TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (decoded != output_bytes) {
        motif_diag_record("frame_decode_failed", decoded);
        return false;
    }
    return true;
}

static void xor_words(uint16_t *target, const uint16_t *previous, size_t words)
{
    for (size_t index = 0; index < words; ++index) target[index] ^= previous[index];
}

static void s_decode_worker(void *argument)
{
    for (;;) {
        xSemaphoreTake(s_decode_start, portMAX_DELAY);
        s_worker_ok = decode_mem(s_worker_input, s_worker_length, s_worker_output, s_worker_output_bytes);
        // Apply the worker's share of the difference frame while the main task
        // is still inflating the other half.
        if (s_worker_ok && s_worker_previous)
            xor_words((uint16_t *)s_worker_output, s_worker_previous, s_worker_output_bytes / 2);
        xSemaphoreGive(s_decode_done);
    }
}

static bool decode_in_worker(const uint8_t *input, size_t length, uint8_t *output, size_t output_bytes, const uint16_t *previous)
{
    // Without a worker (or in the logic tests) the half decodes on the spot.
    if (!s_decode_task) {
        if (!decode_mem(input, length, output, output_bytes)) return false;
        if (previous) xor_words((uint16_t *)output, previous, output_bytes / 2);
        return true;
    }
    s_worker_input = input;
    s_worker_length = length;
    s_worker_output = output;
    s_worker_output_bytes = output_bytes;
    s_worker_previous = previous;
    s_worker_pending = true;
    xSemaphoreGive(s_decode_start);
    return true;
}

static bool decode_worker_finish(void)
{
    if (!s_worker_pending) return true;
    xSemaphoreTake(s_decode_done, portMAX_DELAY);
    s_worker_pending = false;
    return s_worker_ok;
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

static bool read_frame(FILE *file, uint16_t *duration, uint16_t *target, const uint16_t *previous, bool split)
{
    uint32_t phase_started = esp_log_timestamp();
    uint8_t header[6];
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) return false;
    *duration = read16(header);
    uint32_t compressed = read32(header + 2);
    if (*duration < 20 || *duration > 1000 || compressed < 6 || compressed > MAX_COMPRESSED_FRAME_BYTES) {
        motif_diag_record("frame_header_invalid", compressed);
        return false;
    }
    if (fread(s_compressed_frame, 1, compressed, file) != compressed) {
        motif_diag_record("frame_read_failed", compressed);
        return false;
    }
    uint32_t decode_started = esp_log_timestamp();
    bool decoded;
    if (split) {
        // Two independent zlib streams: the first half inflates (and applies its
        // share of the difference frame) on the decode worker while this task
        // handles the second half.
        decoded = false;
        if (compressed >= 16) {
            uint32_t top_length = read32(s_compressed_frame);
            const uint8_t *top = s_compressed_frame + 4;
            const uint8_t *bottom = top + top_length;
            if (top_length >= 6 && top_length <= compressed - 4 - 6) {
                uint8_t *target_bytes = (uint8_t *)target;
                const uint16_t *bottom_previous = previous ? previous + DISPLAY_WIDTH * DISPLAY_HEIGHT / 2 : NULL;
                bool worker = decode_in_worker(top, top_length, target_bytes, DISPLAY_FRAME_BYTES / 2, previous);
                if (worker) {
                    bool main_half = decode_mem(bottom, compressed - 4 - top_length,
                                                target_bytes + DISPLAY_FRAME_BYTES / 2, DISPLAY_FRAME_BYTES / 2);
                    if (main_half && bottom_previous)
                        xor_words((uint16_t *)(target_bytes + DISPLAY_FRAME_BYTES / 2), bottom_previous,
                                  DISPLAY_WIDTH * DISPLAY_HEIGHT / 2);
                    decoded = decode_worker_finish() && main_half;
                }
            }
        }
        if (!decoded && s_worker_pending) decode_worker_finish();
    } else {
        decoded = decode_mem(s_compressed_frame, compressed, (uint8_t *)target, DISPLAY_FRAME_BYTES);
        if (decoded && previous) xor_words(target, previous, DISPLAY_WIDTH * DISPLAY_HEIGHT);
    }
    if (!decoded) {
        motif_diag_record("frame_stream_invalid", compressed);
        return false;
    }
    s_frame_phase_read_ms = decode_started - phase_started;
    s_frame_phase_decode_ms = esp_log_timestamp() - decode_started;
    return true;
}

static bool valid_animation(FILE *file, long length, uint16_t *frame_count, uint8_t *flags)
{
    uint8_t header[12];
    if (fseek(file, 0, SEEK_SET) != 0 || fread(header, 1, sizeof(header), file) != sizeof(header)) return false;
    if (motif_player_header_problem(header, length)) return false;
    uint16_t count = read16(header + 10), duration;
    for (uint16_t i = 0; i < count; ++i) {
        if (!read_frame(file, &duration, s_validation_frame, NULL, (header[5] & MOTIF_FLAG_SPLIT) != 0)) {
            motif_diag_record("frame_invalid", i);
            return false;
        }
        // Validation must not monopolize the main task during a long animation.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (ftell(file) != length || fseek(file, 12, SEEK_SET) != 0) return false;
    *frame_count = count;
    *flags = header[5];
    return true;
}

static bool present_frame(void)
{
    // Passing a panel frame buffer makes the driver switch the scanout at the
    // next frame boundary instead of copying the image mid-scan.
    if (esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                  s_frames[s_write_fb]) != ESP_OK) return false;
    s_write_fb = (uint8_t)((s_write_fb + 1) % DISPLAY_FB_COUNT);
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
    // The frame decoded immediately before lives in the neighbouring buffer and
    // is the reference for difference frames.
    const uint16_t *previous = s_delta && s_frame_index != 0
        ? s_frames[(s_write_fb + DISPLAY_FB_COUNT - 1) % DISPLAY_FB_COUNT] : NULL;
    if (!read_frame(s_animation, &duration, s_frames[s_write_fb], previous, s_split)) {
        motif_diag_record("frame_decode_failed", s_frame_index);
        return false;
    }
    correct_frame_colors();
    if (!present_frame()) return false;
    s_frame_index++;
    s_frame_due = frame_started + duration;
    uint32_t elapsed = esp_log_timestamp() - started;
    if (elapsed > s_perf_max_ms) s_perf_max_ms = elapsed;
    s_perf_total_ms += elapsed;
    s_perf_read_ms += s_frame_phase_read_ms;
    s_perf_decode_ms += s_frame_phase_decode_ms;
    s_perf_frames++;
    if (s_perf_started == 0) s_perf_started = esp_log_timestamp();
    if (esp_log_timestamp() - s_perf_started >= 5000) {
        motif_diag_record("playback_frames_5s", s_perf_frames);
        ESP_LOGI("motif_perf", "Playback frames=%lu/5s avg=%lums max=%lums read=%lums decode=%lums",
                 (unsigned long)s_perf_frames,
                 (unsigned long)(s_perf_total_ms / s_perf_frames), (unsigned long)s_perf_max_ms,
                 (unsigned long)(s_perf_read_ms / s_perf_frames), (unsigned long)(s_perf_decode_ms / s_perf_frames));
        s_perf_started = esp_log_timestamp();
        s_perf_frames = s_perf_max_ms = s_perf_total_ms = s_perf_read_ms = s_perf_decode_ms = 0;
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
    if (!acquire_frame_buffers()) { fclose(incoming); return false; }
    uint16_t count;
    uint8_t flags;
    if (!valid_animation(incoming, length, &count, &flags)) {
        fclose(incoming);
        motif_diag_record("animation_invalid", length);
        return false;
    }
    FILE *previous = s_animation;
    uint16_t previous_count = s_frame_count, previous_index = s_frame_index;
    s_animation = incoming;
    s_frame_count = count;
    s_frame_index = 0;
    s_delta = (flags & 1) != 0;
    s_split = (flags & 2) != 0;
    if (!draw_next_frame()) {
        motif_diag_record("first_frame_failed", count);
        s_animation = previous;
        s_frame_count = previous_count;
        s_frame_index = previous_index;
        fclose(incoming);
        return false;
    }
    s_color_test = -1;
    if (previous) fclose(previous);
    if (s_message) lv_obj_add_flag(s_message, LV_OBJ_FLAG_HIDDEN);
    if (s_detail) lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
    if (s_ring) lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Streaming %u frames from flash (%ld bytes), buffers=%u bytes", count, length,
             DISPLAY_FRAME_BYTES * DISPLAY_FB_COUNT + MAX_COMPRESSED_FRAME_BYTES + DISPLAY_FRAME_BYTES);
    motif_diag_record("animation_flags", flags);
    motif_diag_record("playback_started", count);
    return true;
}

static bool apply_animation(void)
{
    bool had_previous = access(MOTIF_ANIMATION_PATH, F_OK) == 0;
    if (s_animation) {
        fclose(s_animation);
        s_animation = NULL;
    }
    if (had_previous) {
        if (unlink(MOTIF_ANIMATION_BACKUP_PATH) != 0 && errno != ENOENT) {
            motif_diag_record("backup_unlink_failed", errno);
            play_current();
            return false;
        }
        if (rename(MOTIF_ANIMATION_PATH, MOTIF_ANIMATION_BACKUP_PATH) != 0) {
            motif_diag_record("current_rename_failed", errno);
            play_current();
            return false;
        }
    }
    if (rename(MOTIF_ANIMATION_PART_PATH, MOTIF_ANIMATION_PATH) != 0) {
        motif_diag_record("candidate_rename_failed", errno);
        if (had_previous && rename(MOTIF_ANIMATION_BACKUP_PATH, MOTIF_ANIMATION_PATH) == 0) play_current();
        return false;
    }
    if (!play_current()) {
        motif_diag_record("candidate_play_failed", 0);
        unlink(MOTIF_ANIMATION_PATH);
        if (had_previous && rename(MOTIF_ANIMATION_BACKUP_PATH, MOTIF_ANIMATION_PATH) == 0 && !play_current())
            ESP_LOGE(TAG, "Could not restore previous animation");
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
        } else if (s_state == MOTIF_PLAYER_APPLYING) {
            lv_label_set_text(s_message, "Applying");
            lv_label_set_text(s_detail, "Starting animation");
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

    if (s_color_test >= 0) return;
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
