#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "TCA9554PWR.h"
#include "ST7701S.h"
#include "GT911.h"
#include "LVGL_Driver.h"
#include "motif_ble.h"
#include "motif_player.h"
#include "motif_state.h"
#include "motif_wifi.h"

void app_main(void)
{
    ESP_ERROR_CHECK(motif_state_init());
    I2C_Init();
    EXIO_Init();
    LCD_Init();
    Touch_Init();
    ESP_ERROR_CHECK(motif_player_mount());
    LVGL_Init();
    motif_player_init_ui();
    ESP_ERROR_CHECK(motif_wifi_start());
    ESP_ERROR_CHECK(motif_ble_start());
    while (true) {
        motif_player_loop();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
