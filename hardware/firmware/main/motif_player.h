#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MOTIF_ANIMATION_PATH "/media/current.motif"
#define MOTIF_ANIMATION_PART_PATH "/media/current.motif.part"
#define MOTIF_ANIMATION_BACKUP_PATH "/media/current.motif.bak"

// Frame-stream flags understood by this firmware; anything else is rejected.
#define MOTIF_FLAG_DELTA 1
#define MOTIF_FLAG_SPLIT 2
#define MOTIF_SUPPORTED_FLAGS (MOTIF_FLAG_DELTA | MOTIF_FLAG_SPLIT)

typedef enum {
    MOTIF_PLAYER_IDLE = 0,
    MOTIF_PLAYER_APPLYING,
    MOTIF_PLAYER_PLAYING,
    MOTIF_PLAYER_ERROR,
} motif_player_state_t;

esp_err_t motif_player_mount(void);
void motif_player_init_ui(void);
void motif_player_loop(void);
bool motif_player_has_animation(void);
bool motif_player_request_apply(uint32_t *generation);
void motif_player_status(uint32_t *generation, motif_player_state_t *state);
void motif_player_show_passkey(uint32_t passkey);
void motif_player_clear_passkey(void);
void motif_player_set_connection(bool connected, bool joining);
void motif_player_set_network_error(void);
void motif_player_set_receiving(bool receiving);
void motif_player_set_receive_progress(uint8_t percent);
bool motif_player_is_receiving(void);
esp_err_t motif_player_show_color_test(uint8_t pattern);
esp_err_t motif_player_set_color_profile(uint8_t profile);
uint8_t motif_player_color_profile(void);
const char *motif_player_header_problem(const uint8_t *header, long length);
