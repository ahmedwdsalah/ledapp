#include "motif_player.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "lvgl.h"

static const char *TAG = "motif_player";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static lv_obj_t *s_gif;
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
static bool s_status_dirty;
static motif_player_state_t s_state = MOTIF_PLAYER_IDLE;

static void *media_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    char full[96];
    if (snprintf(full, sizeof(full), "/media/%s", path) >= sizeof(full)) return NULL;
    return fopen(full, mode == LV_FS_MODE_RD ? "rb" : "wb");
}

static lv_fs_res_t media_close(lv_fs_drv_t *drv, void *file)
{
    return fclose(file) == 0 ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t media_read(lv_fs_drv_t *drv, void *file, void *buffer, uint32_t wanted, uint32_t *read)
{
    *read = fread(buffer, 1, wanted, file);
    return ferror(file) ? LV_FS_RES_FS_ERR : LV_FS_RES_OK;
}

static lv_fs_res_t media_seek(lv_fs_drv_t *drv, void *file, uint32_t position, lv_fs_whence_t whence)
{
    int origin = whence == LV_FS_SEEK_CUR ? SEEK_CUR : whence == LV_FS_SEEK_END ? SEEK_END : SEEK_SET;
    return fseek(file, position, origin) == 0 ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t media_tell(lv_fs_drv_t *drv, void *file, uint32_t *position)
{
    long value = ftell(file);
    if (value < 0) return LV_FS_RES_FS_ERR;
    *position = value;
    return LV_FS_RES_OK;
}

static void register_media_driver(void)
{
    static lv_fs_drv_t driver;
    lv_fs_drv_init(&driver);
    driver.letter = 'S';
    driver.open_cb = media_open;
    driver.close_cb = media_close;
    driver.read_cb = media_read;
    driver.seek_cb = media_seek;
    driver.tell_cb = media_tell;
    lv_fs_drv_register(&driver);
}

static bool media_is_blank(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "media");
    if (!part) return false;
    uint8_t probe[256];
    if (esp_partition_read(part, 0, probe, sizeof(probe)) != ESP_OK) return false;
    for (size_t i = 0; i < sizeof(probe); ++i) if (probe[i] != 0xff) return false;
    return true;
}

esp_err_t motif_player_mount(void)
{
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = "/media",
        .partition_label = "media",
        .max_files = 5,
        .format_if_mount_failed = media_is_blank(),
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) ESP_LOGE(TAG, "Media mount failed: %s", esp_err_to_name(err));
    return err;
}

static bool valid_gif(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t header[10];
    bool good = fread(header, 1, sizeof(header), file) == sizeof(header) &&
                memcmp(header, "GIF89a", 6) == 0;
    fclose(file);
    if (!good) return false;
    unsigned width = header[6] | (header[7] << 8);
    unsigned height = header[8] | (header[9] << 8);
    return width > 0 && width <= 480 && height > 0 && height <= 480;
}

static bool play_current(void)
{
    if (!valid_gif(MOTIF_GIF_PATH)) return false;
    if (s_gif) { lv_obj_del(s_gif); s_gif = NULL; }
    s_gif = lv_gif_create(lv_scr_act());
    lv_gif_set_src(s_gif, "S:/test.gif");
    if (!lv_img_get_src(s_gif)) {
        lv_obj_del(s_gif);
        s_gif = NULL;
        return false;
    }
    lv_obj_align(s_gif, LV_ALIGN_CENTER, 0, 0);
    if (s_message) lv_obj_add_flag(s_message, LV_OBJ_FLAG_HIDDEN);
    return true;
}

void motif_player_init_ui(void)
{
    register_media_driver();
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

    if (access(MOTIF_GIF_PATH, F_OK) != 0 && access(MOTIF_GIF_BACKUP_PATH, F_OK) == 0) {
        rename(MOTIF_GIF_BACKUP_PATH, MOTIF_GIF_PATH);
    }
    unlink(MOTIF_GIF_PART_PATH);
    if (play_current()) s_state = MOTIF_PLAYER_PLAYING;
    s_status_dirty = true;
}

bool motif_player_request_apply(uint32_t *generation)
{
    portENTER_CRITICAL(&s_mux);
    if (s_pending_generation != 0) {
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
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
    s_status_dirty = true;
    portEXIT_CRITICAL(&s_mux);
}

void motif_player_loop(void)
{
    uint32_t passkey;
    bool update_passkey = false;
    bool connected, joining, receiving, network_error, status_dirty;
    uint32_t pending = 0;
    portENTER_CRITICAL(&s_mux);
    if (s_passkey_dirty) {
        update_passkey = true;
        s_passkey_dirty = false;
    }
    passkey = s_passkey;
    pending = s_pending_generation;
    connected = s_connected;
    joining = s_joining;
    receiving = s_receiving;
    network_error = s_network_error;
    status_dirty = s_status_dirty;
    s_status_dirty = false;
    portEXIT_CRITICAL(&s_mux);

    if ((update_passkey || status_dirty) && s_message) {
        if (passkey) {
            char code[12];
            snprintf(code, sizeof(code), "%06lu", (unsigned long)passkey);
            lv_label_set_text(s_message, code);
            lv_label_set_text(s_detail, "Enter this code on your phone");
        } else if (receiving) {
            lv_label_set_text(s_message, "Receiving");
            lv_label_set_text(s_detail, "Keep the display powered on");
        } else if (joining) {
            lv_label_set_text(s_message, "Connecting");
            lv_label_set_text(s_detail, "Joining Wi-Fi");
        } else if (network_error) {
            lv_label_set_text(s_message, "Wi-Fi failed");
            lv_label_set_text(s_detail, "Check network details in Motif");
        } else if (s_state == MOTIF_PLAYER_ERROR) {
            lv_label_set_text(s_message, "Can't play GIF");
            lv_label_set_text(s_detail, "Try a different animation");
        } else if (connected) {
            lv_label_set_text(s_message, "Connected");
            lv_label_set_text(s_detail, "Choose an animation in Motif");
        } else {
            lv_label_set_text(s_message, "MOTIF");
            lv_label_set_text(s_detail, "Open Motif to connect");
        }
        bool visible = passkey || receiving || joining || !s_gif || s_state == MOTIF_PLAYER_ERROR;
        if (visible) {
            lv_obj_clear_flag(s_message, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_ring);
            lv_obj_move_foreground(s_message);
            lv_obj_move_foreground(s_detail);
        } else {
            lv_obj_add_flag(s_message, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!pending) return;

    if (s_gif) { lv_obj_del(s_gif); s_gif = NULL; }
    bool had_previous = access(MOTIF_GIF_PATH, F_OK) == 0;
    if (had_previous) {
        unlink(MOTIF_GIF_BACKUP_PATH);
        if (rename(MOTIF_GIF_PATH, MOTIF_GIF_BACKUP_PATH) != 0) had_previous = false;
    }
    bool applied = rename(MOTIF_GIF_PART_PATH, MOTIF_GIF_PATH) == 0 && play_current();
    if (!applied && had_previous) {
        unlink(MOTIF_GIF_PATH);
        rename(MOTIF_GIF_BACKUP_PATH, MOTIF_GIF_PATH);
        play_current();
    }
    if (applied) unlink(MOTIF_GIF_BACKUP_PATH);
    portENTER_CRITICAL(&s_mux);
    s_generation = pending;
    s_pending_generation = 0;
    s_state = applied ? MOTIF_PLAYER_PLAYING : MOTIF_PLAYER_ERROR;
    s_status_dirty = true;
    portEXIT_CRITICAL(&s_mux);
}
