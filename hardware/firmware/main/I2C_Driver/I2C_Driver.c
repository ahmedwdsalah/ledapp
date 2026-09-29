#include "I2C_Driver.h"

static const char *I2C_TAG = "I2C";
static i2c_master_bus_handle_t s_bus;

void I2C_Init(void)
{
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_Touch_SDA_IO,
        .scl_io_num = I2C_Touch_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&config, &s_bus));
    ESP_LOGI(I2C_TAG, "I2C initialized successfully");
}

i2c_master_bus_handle_t I2C_BusHandle(void)
{
    return s_bus;
}
