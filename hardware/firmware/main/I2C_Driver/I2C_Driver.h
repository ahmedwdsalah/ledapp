#pragma once

#include <stdint.h>
#include "esp_log.h"
#include "driver/i2c_master.h"

/********************* I2C *********************/
#define I2C_Touch_SCL_IO            7         /*!< GPIO number used for I2C master clock */
#define I2C_Touch_SDA_IO            15        /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_0 /*!< I2C master port number */
#define I2C_MASTER_FREQ_HZ          400000    /*!< I2C master clock frequency */
#define I2C_MASTER_TIMEOUT_MS       1000

void I2C_Init(void);
i2c_master_bus_handle_t I2C_BusHandle(void);
