#!/usr/bin/env python3
"""Compile the actual PSRAM draw-buffer setup and check LVGL's capacity contract."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'firmware/main/LVGL_Driver/LVGL_Driver.c').read_text()
setup = source.split('#else\n    ESP_LOGI(LVGL_TAG, "Allocate separate LVGL draw buffers from PSRAM");', 1)[1].split('#endif', 1)[0]
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint16_t lv_color_t;
#define EXAMPLE_LCD_H_RES 480
#define EXAMPLE_LCD_V_RES 480
#define MALLOC_CAP_SPIRAM 0
static size_t allocated[2];
static unsigned calls;
static void *buf1, *buf2;
static int disp_buf;
static void *heap_caps_malloc(size_t bytes, int caps) {
    assert(calls < 2);
    allocated[calls++] = bytes;
    return malloc(bytes);
}
static void lv_disp_draw_buf_init(void *draw, void *a, void *b, unsigned pixels) {
    for (unsigned i = 0; i < 2; i++) {
        if (allocated[i] < pixels * sizeof(lv_color_t)) {
            fprintf(stderr, "Buffer %u: allocated %zu bytes, LVGL can write %zu bytes\n",
                    i + 1, allocated[i], pixels * sizeof(lv_color_t));
            exit(1);
        }
    }
}
int main(void) {
''' + setup + '\nfree(buf1); free(buf2); puts("LVGL buffer capacities PASS");\n}\n'
with tempfile.TemporaryDirectory() as temp:
    c = Path(temp) / 'check.c'
    executable = Path(temp) / 'check'
    c.write_text(harness)
    subprocess.run(['cc', str(c), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
