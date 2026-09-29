#include "GT911.h"

#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "I2C_Driver.h"
#include "ST7701S.h"
#include "TCA9554PWR.h"

static const char *TAG = "GT911";

esp_lcd_touch_handle_t tp = NULL;

static void gt911_hardware_reset(void)
{
    gpio_set_direction(I2C_Touch_INT_IO, GPIO_MODE_OUTPUT);
    gpio_set_level(I2C_Touch_INT_IO, false);
    vTaskDelay(pdMS_TO_TICKS(150));

    Set_EXIO(TCA9554_EXIO2, false);
    vTaskDelay(pdMS_TO_TICKS(150));
    Set_EXIO(TCA9554_EXIO2, true);
    vTaskDelay(pdMS_TO_TICKS(50));

    gpio_set_level(I2C_Touch_INT_IO, true);
    gpio_set_direction(I2C_Touch_INT_IO, GPIO_MODE_INPUT);
}

void Touch_Init(void)
{
    gt911_hardware_reset();

    const esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .flags = {
            .disable_control_phase = 1,
        },
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_LOGI(TAG, "Initialize touch IO (I2C)");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(I2C_BusHandle(), &io_config, &io));

    const esp_lcd_touch_config_t config = {
        .x_max = EXAMPLE_LCD_V_RES,
        .y_max = EXAMPLE_LCD_H_RES,
        .rst_gpio_num = I2C_Touch_RST_IO,
        .int_gpio_num = I2C_Touch_INT_IO,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_LOGI(TAG, "Initialize touch controller GT911");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(io, &config, &tp));
}
