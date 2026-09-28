#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MOTIF_GIF_PATH "/media/test.gif"
#define MOTIF_GIF_PART_PATH "/media/test.gif.part"
#define MOTIF_GIF_BACKUP_PATH "/media/test.gif.bak"

typedef enum {
    MOTIF_PLAYER_IDLE = 0,
    MOTIF_PLAYER_APPLYING,
    MOTIF_PLAYER_PLAYING,
    MOTIF_PLAYER_ERROR,
} motif_player_state_t;

esp_err_t motif_player_mount(void);
void motif_player_init_ui(void);
void motif_player_loop(void);
bool motif_player_request_apply(uint32_t *generation);
void motif_player_status(uint32_t *generation, motif_player_state_t *state);
void motif_player_show_passkey(uint32_t passkey);
void motif_player_clear_passkey(void);
void motif_player_set_connection(bool connected, bool joining);
void motif_player_set_network_error(void);
void motif_player_set_receiving(bool receiving);
