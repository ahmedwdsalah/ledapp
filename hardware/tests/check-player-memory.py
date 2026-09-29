#!/usr/bin/env python3
"""Compile real playback/transaction functions and exercise full-size animation files."""
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

import numpy as np

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
#include <math.h>
#include <zlib.h>
#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 480
#define DISPLAY_FRAME_BYTES (480*480*2)
#define MAX_COMPRESSED_FRAME_BYTES (DISPLAY_FRAME_BYTES+64*1024)
#define DISPLAY_FB_COUNT 3
#define MOTIF_MAX_ANIMATION_BYTES (4*1024*1024)
#define MOTIF_ANIMATION_PATH "current.motif"
#define MOTIF_ANIMATION_PART_PATH "current.motif.part"
#define MOTIF_ANIMATION_BACKUP_PATH "current.motif.bak"
#define MALLOC_CAP_SPIRAM 0
#define MOTIF_COLOR_PROFILE 0
#define MOTIF_FLAG_DELTA 1
#define MOTIF_FLAG_SPLIT 2
#define MOTIF_SUPPORTED_FLAGS (MOTIF_FLAG_DELTA | MOTIF_FLAG_SPLIT)
#define portMUX_INITIALIZER_UNLOCKED 0
#define MOTIF_PLAYER_IDLE 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_FAIL -1
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define LV_OBJ_FLAG_HIDDEN 1
#define TINFL_FLAG_PARSE_ZLIB_HEADER 1
#define pdMS_TO_TICKS(x) (x)
typedef int portMUX_TYPE;
typedef int esp_err_t;
typedef int lv_obj_t;
typedef int motif_player_state_t;
static int panel_handle;
static size_t used, draws;
static void *mock_fb[DISPLAY_FB_COUNT];
static void *last_drawn;
static void *heap_caps_malloc(size_t n, int caps) {
    if(used+n>3*1024*1024) return NULL;
    void *p=malloc(n);assert(p);used+=n;return p;
}
static uint32_t esp_log_timestamp(void) { static uint32_t t;return ++t; }
static uint32_t lv_tick_get(void) { return esp_log_timestamp(); }
static void vTaskDelay(unsigned ms) {}
static void lv_obj_add_flag(void *p,int flag) {}
static void lv_obj_clear_flag(void *p,int flag) {}
static void motif_diag_record(const char *event,int value) {}
static size_t tinfl_decompress_mem_to_mem(void *out,size_t capacity,const void *in,size_t length,int flags) {
    uLongf n=capacity;return uncompress(out,&n,in,length)==Z_OK ? n : (size_t)-1;
}
static int esp_lcd_panel_draw_bitmap(int p,int x,int y,int w,int h,void *pixels) {
    assert(pixels==mock_fb[0]||pixels==mock_fb[1]||pixels==mock_fb[2]);
    last_drawn=pixels;draws++;return 0;
}
static esp_err_t esp_lcd_rgb_panel_get_frame_buffer(int p,uint32_t fb_num,void **fb0,void **fb1,void **fb2) {
    assert(fb_num==DISPLAY_FB_COUNT);
    for(uint32_t i=0;i<DISPLAY_FB_COUNT;i++) {
        if(!mock_fb[i]) mock_fb[i]=heap_caps_malloc(DISPLAY_FRAME_BYTES,MALLOC_CAP_SPIRAM);
        if(!mock_fb[i]) return ESP_ERR_INVALID_ARG;
        memset(mock_fb[i],0x5A,DISPLAY_FRAME_BYTES);
    }
    *fb0=mock_fb[0];*fb1=mock_fb[1];*fb2=mock_fb[2];return ESP_OK;
}
typedef int SemaphoreHandle_t;
typedef int TaskHandle_t;
typedef int BaseType_t;
#define pdTRUE 1
#define portMAX_DELAY 0xFFFFFFFFu
static SemaphoreHandle_t xSemaphoreCreateBinary(void) { return 1; }
static int xSemaphoreGive(SemaphoreHandle_t s) { return pdTRUE; }
static int xSemaphoreTake(SemaphoreHandle_t s,int block) { return pdTRUE; }
// No worker task in the harness: parallel halves decode inline via the fallback path.
static int xTaskCreatePinnedToCore(void (*fn)(void*),const char *name,int stack,void *arg,int prio,TaskHandle_t *handle,int core) { return 1; }
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
    int fixtures=argc-4;
    // The shared header validator must accept every supported flag combination
    // and reject the rest; both playback and the upload handler call it.
    uint8_t h[12]={'M','O','T','F',1,0,0xE0,0x01,0xE0,0x01,10,0};
    for(int f=0;f<=3;f++) { h[5]=(uint8_t)f; assert(!motif_player_header_problem(h,100)); }
    h[5]=4; assert(motif_player_header_problem(h,100));
    h[4]=2;h[5]=0; assert(motif_player_header_problem(h,100));
    h[4]=1;h[6]=0x20; assert(motif_player_header_problem(h,100));
    h[6]=0xE0;h[10]=0; assert(motif_player_header_problem(h,100));
    h[10]=10; assert(motif_player_header_problem(h,17)); assert(!motif_player_header_problem(h,18));
    for(int round=0;round<2;round++) {
        for(int f=1;f<=fixtures;f++) {
            select_file(argv[f]);assert(apply_animation());
            for(int i=0;i<s_frame_count*2;i++)assert(draw_next_frame());
            assert(used<=3*1024*1024 && access(MOTIF_ANIMATION_BACKUP_PATH,F_OK)!=0);
        }
        select_file(argv[argc-3]);assert(apply_animation());
        for(int i=0;i<s_frame_count*2;i++)assert(draw_next_frame());
        assert(access(MOTIF_ANIMATION_BACKUP_PATH,F_OK)!=0);
    }
    for(int failure=1;failure<=2;failure++) {
        select_file(argv[1]);fail_rename=failure;assert(!apply_animation());
        assert(s_animation && access(MOTIF_ANIMATION_PATH,F_OK)==0);assert(draw_next_frame());
    }
    select_file(argv[1]);
    FILE *bad=fopen(MOTIF_ANIMATION_PART_PATH,"r+b");assert(bad);
    fseek(bad,-1,SEEK_END);int byte=fgetc(bad);fseek(bad,-1,SEEK_END);fputc(byte^255,bad);fclose(bad);
    assert(!apply_animation());assert(s_animation);assert(draw_next_frame());
    select_file(argv[argc-3]);bad=fopen(MOTIF_ANIMATION_PART_PATH,"ab");fputc(0,bad);fclose(bad);
    assert(!apply_animation());assert(s_animation);assert(draw_next_frame());
    select_file(argv[argc-2]);assert(apply_animation());assert(s_delta);assert(s_split);
    FILE *expected=fopen(argv[argc-1],"rb");assert(expected);
    size_t expected_size=(size_t)s_frame_count*DISPLAY_FRAME_BYTES;
    unsigned char *reference=malloc(expected_size);assert(reference);
    assert(fread(reference,1,expected_size,expected)==expected_size);
    for(int i=0;i<s_frame_count*3;i++) {
        assert(draw_next_frame());
        // play_current already drew frame 0, so each harness draw is the next frame.
        unsigned char *want=reference+(size_t)((i+1)%s_frame_count)*DISPLAY_FRAME_BYTES;
        if(!last_drawn || memcmp(last_drawn,want,DISPLAY_FRAME_BYTES)) {
            unsigned char *got=last_drawn;size_t k=0;
            while(k<DISPLAY_FRAME_BYTES&&got[k]==want[k])k++;
            fprintf(stderr,"delta mismatch i=%d frame=%d first_diff=%zu got=%02x want=%02x\n",
                    i,(i+1)%s_frame_count,k,k<DISPLAY_FRAME_BYTES?got[k]:0,k<DISPLAY_FRAME_BYTES?want[k]:0);
            abort();
        }
    }
    fclose(expected);free(reference);
    fclose(s_animation);free(s_compressed_frame);free(s_validation_frame);
    for(int i=0;i<DISPLAY_FB_COUNT;i++)free(mock_fb[i]);
    printf("PASS: real fixtures + exact 4 MiB + delta frames, repeated loops, last-frame corruption, oversize, rename rollback; %zu bytes RAM\n",used);
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
    fixtures=[root/f'assets/device-animations/{name}.motif' for name in
              ['violet-iris-real','curious-raccoon','prism-vortex','badge-loading']]
    # Rebuild one fixture as a split difference stream so the decoder restores the originals.
    first=fixtures[0].read_bytes()
    magic,version,flags,width,height,frame_count=struct.unpack_from('<4sBBHHH',first)
    frames=[];durations=[];offset=12
    for index in range(frame_count):
        duration,length=struct.unpack_from('<HI',first,offset);offset+=6
        payload=first[offset:offset+length]
        if flags & 2:
            top_length=struct.unpack_from('<I',payload)[0]
            data=zlib.decompress(payload[4:4+top_length])+zlib.decompress(payload[4+top_length:])
        else:
            data=zlib.decompress(payload)
        # Fixtures may already be difference-encoded; reconstruct absolute frames first.
        pixels=np.frombuffer(data,dtype=np.uint16)
        frames.append(pixels if (flags&1)==0 or index==0 else (pixels^frames[index-1]))
        durations.append(duration);offset+=length
    assert offset==len(first)
    delta=bytearray(struct.pack('<4sBBHHH',b'MOTF',1,3,width,height,frame_count))
    for index,pixels in enumerate(frames):
        payload_pixels=pixels if index==0 else (pixels^frames[index-1])
        data=payload_pixels.astype('<u2').tobytes()
        half=len(data)//2
        top=zlib.compress(data[:half],9);bottom=zlib.compress(data[half:],9)
        blob=struct.pack('<I',len(top))+top+bottom
        delta+=struct.pack('<HI',durations[index],len(blob))+blob
    delta_path=temp/'delta.motif';delta_path.write_bytes(delta)
    expected=temp/'delta-expected.raw'
    expected.write_bytes(b''.join(frame.astype('<u2').tobytes() for frame in frames))
    c=temp/'check.c';exe=temp/'check';c.write_text(harness)
    subprocess.run(['cc',str(c),'-lz','-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe),*map(str,fixtures),str(full),str(delta_path),str(expected)],cwd=temp,check=True)
