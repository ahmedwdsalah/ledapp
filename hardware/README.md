# Motif display firmware

Target: Spotpear/Waveshare ESP32-S3-Touch-LCD-2.8C (8 MB PSRAM, 16 MB flash, 480 × 480 ST7701 LCD). The app uses authenticated BLE pairing and Wi-Fi to upload pre-rendered RGB565 animations. No SD card is needed.

The source is in `firmware/`. `PROTOCOL.md` describes the app/board contract. The original 16 MB flash backup is in `backups/` and is Git-ignored because NVS may contain private credentials. The vendor archive and local ESP-IDF toolchain are kept under `vendor/` and `toolchains/`, also Git-ignored. The firmware source, partition table, and app integration are not ignored.

Build with ESP-IDF 5.3.5:

```sh
export IDF_TOOLS_PATH="$PWD/hardware/toolchains/.espressif"
. hardware/toolchains/esp-idf/export.sh
cd hardware/firmware
idf.py build
idf.py -p /dev/cu.usbmodem31301 flash
```

The first installation changes the partition table. Flash the bootloader, partition table, OTA data, and app together through `idf.py flash`; a partial app-only flash is not enough. Check the current serial port before using the example path above. If the board remains in USB download mode after flashing, press RESET once without BOOT.

On first boot, the display initializes a blank internal SPIFFS media partition, advertises `MOTIF-<device ID>`, and shows a connection state. In the app, tap **Upload to Device**, choose the display, enter the Wi-Fi network and password, and confirm the code shown on the display. The phone and display must share the same Wi-Fi network. The app reports upload success only after the board confirms playback. GIFs remain app previews; `assets/device-animations/` holds the files sent to the board. Regenerate those files with `python3 hardware/tools/pack-animations.py` after editing the previews (requires FFmpeg and Pillow).
