#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#include "Buzzer.h"

#define TCA9554_EXIO1 0x01
#define TCA9554_EXIO2 0x02
#define TCA9554_EXIO3 0x03
#define TCA9554_EXIO4 0x04
#define TCA9554_EXIO5 0x05
#define TCA9554_EXIO6 0x06
#define TCA9554_EXIO7 0x07
#define TCA9554_EXIO8 0x08

void Set_EXIO(uint8_t Pin, uint8_t State);
esp_err_t EXIO_Init(void);
