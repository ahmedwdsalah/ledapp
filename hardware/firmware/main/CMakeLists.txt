idf_component_register(
    SRCS
        "main.c"
        "motif_state.c"
        "motif_wifi.c"
        "motif_ble.c"
        "motif_diagnostics.c"
        "motif_http.c"
        "motif_player.c"
        "EXIO/TCA9554PWR.c"
        "LCD_Driver/ST7701S.c"
        "Touch_Driver/GT911.c"
        "LVGL_Driver/LVGL_Driver.c"
        "I2C_Driver/I2C_Driver.c"
        "Buzzer/Buzzer.c"
    INCLUDE_DIRS
        "." "EXIO" "LCD_Driver" "Touch_Driver"
        "LVGL_Driver" "I2C_Driver" "Buzzer"
    PRIV_REQUIRES
        bt driver esp_lcd esp_rom esp_event esp_http_server esp_netif esp_wifi
        nvs_flash littlefs mbedtls app_update esp_lcd_touch esp_lcd_touch_gt911 esp_io_expander_tca9554
)
