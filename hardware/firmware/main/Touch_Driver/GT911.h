#pragma once

#include <stdint.h>
#include "esp_lcd_touch.h"

#define I2C_Touch_INT_IO            16         /*!< GPIO number used for touch interrupt */
#define I2C_Touch_RST_IO            -1         /*!< Touch reset is driven through the TCA9554 expander */

extern esp_lcd_touch_handle_t tp;

void Touch_Init(void);
