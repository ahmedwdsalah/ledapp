#include "TCA9554PWR.h"

#include "esp_io_expander.h"
#include "esp_io_expander_tca9554.h"
#include "esp_log.h"
#include "I2C_Driver.h"

static const char *TAG = "TCA9554";
static esp_io_expander_handle_t s_expander;

static const uint32_t ALL_EXIO_PINS =
    IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2 | IO_EXPANDER_PIN_NUM_3 |
    IO_EXPANDER_PIN_NUM_4 | IO_EXPANDER_PIN_NUM_5 | IO_EXPANDER_PIN_NUM_6 | IO_EXPANDER_PIN_NUM_7;

void Set_EXIO(uint8_t Pin, uint8_t State)
{
    if (!s_expander || Pin < 1 || Pin > 8) return;
    uint32_t mask = (uint32_t)(IO_EXPANDER_PIN_NUM_0 << (Pin - 1));
    esp_io_expander_set_level(s_expander, mask, State ? 1 : 0);
}

esp_err_t EXIO_Init(void)
{
    esp_err_t err = esp_io_expander_new_i2c_tca9554(I2C_BusHandle(), ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &s_expander);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Expander init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_io_expander_set_dir(s_expander, ALL_EXIO_PINS, IO_EXPANDER_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Expander direction failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_io_expander_set_level(s_expander, ALL_EXIO_PINS, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Expander initial level failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}
