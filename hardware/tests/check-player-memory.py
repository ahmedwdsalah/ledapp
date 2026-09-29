#!/usr/bin/env python3
"""Compile real playback/transaction functions and exercise full-size animation files."""
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

root = Path(__file__).resolve().parents[2]
source = (root/'hardware/firmware/main/motif_player.c').read_text()
declarations = source[source.index('static const char *TAG'):source.index('esp_err_t motif_player_mount')]
functions = source[source.index('static bool read_frame'):source.index('void motif_player_init_ui')]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <zlib.h>
#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 480
#define DISPLAY_FRAME_BYTES (480*480*2)
#define MAX_COMPRESSED_FRAME_BYTES (DISPLAY_FRAME_BYTES+1024)
#define MOTIF_MAX_ANIMATION_BYTES (4*1024*1024)
#define MOTIF_ANIMATION_PATH "current.motif"
#define MOTIF_ANIMATION_PART_PATH "current.motif.part"
#define MOTIF_ANIMATION_BACKUP_PATH "current.motif.bak"
#define MALLOC_CAP_SPIRAM 0
#define portMUX_INITIALIZER_UNLOCKED 0
#define MOTIF_PLAYER_IDLE 0
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define LV_OBJ_FLAG_HIDDEN 1
#define TINFL_FLAG_PARSE_ZLIB_HEADER 1
#define pdMS_TO_TICKS(x) (x)
typedef int portMUX_TYPE;
typedef int lv_obj_t;
typedef int motif_player_state_t;
static int panel_handle;
static size_t used, draws;
static void *heap_caps_malloc(size_t n, int caps) {
    if(used+n>1024*1024) return NULL;
    void *p=malloc(n);assert(p);used+=n;return p;
}
static uint32_t esp_log_timestamp(void) { static uint32_t t;return ++t; }
static uint32_t lv_tick_get(void) { return esp_log_timestamp(); }
static void vTaskDelay(unsigned ms) {}
static void lv_obj_add_flag(void *p,int flag) {}
static void motif_diag_record(const char *event,int value) {}
static size_t tinfl_decompress_mem_to_mem(void *out,size_t capacity,const void *in,size_t length,int flags) {
    uLongf n=capacity;return uncompress(out,&n,in,length)==Z_OK ? n : (size_t)-1;
}
static int esp_lcd_panel_draw_bitmap(int p,int x,int y,int w,int h,void *pixels) { draws++;return 0; }
static int fail_rename;
static int test_rename(const char *a,const char *b) {
    if(fail_rename && ((fail_rename==1 && !strcmp(b,MOTIF_ANIMATION_BACKUP_PATH)) ||
       (fail_rename==2 && !strcmp(a,MOTIF_ANIMATION_PART_PATH)))) {
        fail_rename=0;errno=EIO;return -1;
    }
    return rename(a,b);
}
#define rename test_rename
''' + declarations + functions + r'''
static void select_file(const char *path) {
    FILE *in=fopen(path,"rb"),*out=fopen(MOTIF_ANIMATION_PART_PATH,"wb");assert(in&&out);
    char buf[8192];size_t n;
    while((n=fread(buf,1,sizeof(buf),in)))assert(fwrite(buf,1,n,out)==n);
    fclose(in);fclose(out);
}
int main(int argc,char **argv) {
    for(int round=0;round<2;round++)for(int f=1;f<argc;f++) {
        select_file(argv[f]);assert(apply_animation());
        for(int i=0;i<s_frame_count*2;i++)assert(draw_next_frame());
        assert(used<=1024*1024 && access(MOTIF_ANIMATION_BACKUP_PATH,F_OK)!=0);
    }
    FILE *previous=s_animation;
    for(int failure=1;failure<=2;failure++) {
        select_file(argv[1]);fail_rename=failure;assert(!apply_animation());
        assert(s_animation==previous && access(MOTIF_ANIMATION_PATH,F_OK)==0);assert(draw_next_frame());
    }
    select_file(argv[1]);
    FILE *bad=fopen(MOTIF_ANIMATION_PART_PATH,"r+b");assert(bad);
    fseek(bad,-1,SEEK_END);int byte=fgetc(bad);fseek(bad,-1,SEEK_END);fputc(byte^255,bad);fclose(bad);
    size_t before=draws;assert(!apply_animation());assert(draws==before && s_animation==previous);assert(draw_next_frame());
    select_file(argv[argc-1]);bad=fopen(MOTIF_ANIMATION_PART_PATH,"ab");fputc(0,bad);fclose(bad);
    assert(!apply_animation());assert(s_animation==previous);assert(draw_next_frame());
    fclose(s_animation);free(s_compressed_frame);free(s_display_frame);
    printf("PASS: real fixtures + exact 4 MiB, repeated loops, last-frame corruption, oversize, rename rollback; %zu bytes RAM\n",used);
}
'''
with tempfile.TemporaryDirectory() as temp:
    temp=Path(temp)
    # A valid zlib stream may have padding; this exercises the exact transport boundary.
    count=10;remaining=4*1024*1024-12-6*count
    payload=zlib.compress(bytes(460800),9)
    boundary=bytearray(struct.pack('<4sBBHHH',b'MOTF',1,0,480,480,count))
    for i in range(count):
        n=remaining//(count-i);remaining-=n
        boundary+=struct.pack('<HI',100,n)+payload+bytes(n-len(payload))
    full=temp/'boundary.motif';full.write_bytes(boundary)
    c=temp/'check.c';exe=temp/'check';c.write_text(harness)
    subprocess.run(['cc',str(c),'-lz','-o',str(exe)],check=True)
    fixtures=[root/f'assets/device-animations/{name}.motif' for name in
              ['violet-iris-real','curious-raccoon','prism-vortex','badge-loading']]
    subprocess.run([str(exe),*map(str,fixtures),str(full)],cwd=temp,check=True)
