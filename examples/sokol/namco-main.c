/*
    Pacman arcade machine emulator.
    
MIT License

Copyright (c) 2017 Andre Weissflog
Copyright (c) 2022 Victor Suarez Rovere <suarezvictor@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.    
*/

#ifdef __linux__
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#define FB_WIDTH 800
#define FB_HEIGHT 600
#define FAST_CODE
#define FAST_DATA
#else
#include <stddef.h> //for NULL (FIXME: used in litesdk_timer.h)
#include <litex.h> //FAST_CODE and FAST_DATA macros
#include "lite_fb.h"
#include "litesdk_timer.h"
#endif

#define FB_WIDTH_MAX 1024 //next power of 2
#define PIXEL_SCALING 2
#define ROTATED_90
#define DEFAULT_SAMPLERATE 44100

/////////////////////////
//simulator declarations

#define NAMCO_USE_BGRA8 //invers palette
#define NAMCO_PACMAN
//#define NAMCO_PENGO

#define CHIPS_IMPL
#include "chips/z80.h"
#include "chips/clk.h"
#include "chips/mem.h"
#ifdef NAMCO_PACMAN
#include "pacman-roms.h"
#endif
#ifdef NAMCO_PENGO
#include "pengo-roms.h"
#endif

#include "systems/namco.h"


/////////////////////////

enum KEYCODE
{
  KEYCODE_UP,
  KEYCODE_LEFT,
  KEYCODE_RIGHT,
  KEYCODE_DOWN,
  KEYCODE_1,
  KEYCODE_2,
  KEYCODE_NOKEY = -1,
};

#define NAMCO_INPUT_P1_UP       (1<<0)
#define NAMCO_INPUT_P1_LEFT     (1<<1)
#define NAMCO_INPUT_P1_RIGHT    (1<<2)
#define NAMCO_INPUT_P1_DOWN     (1<<3)
#define NAMCO_INPUT_P1_BUTTON   (1<<4)
#define NAMCO_INPUT_P1_COIN     (1<<5)
#define NAMCO_INPUT_P1_START    (1<<6)


typedef void (*audio_cb_t)(const namco_sample_t* samples, int num_samples, void* user_data);
void sim_init(uint32_t *framebuffer, size_t fb_size, audio_cb_t audio_cb, int samplerate);
bool sim_exec(uint64_t t1);
int sim_width(void);
int sim_height(void);
void sim_setkey(enum KEYCODE key, bool value);

#ifdef __linux__
#include <signal.h>
#include <SDL2/SDL.h> //for events

#include "sdl_fb.h"


static uint32_t pixel_buffer[FB_WIDTH_MAX*FB_HEIGHT];
static fb_handle_t fb;

void draw_frame(int emu_width, int emu_height)
{
#ifdef ROTATED_90
	static uint32_t rotated_buffer[FB_WIDTH_MAX*FB_HEIGHT];
	const uint32_t *src = pixel_buffer+emu_height*emu_width;
	uint32_t *dst = rotated_buffer;
	dst += (FB_WIDTH-emu_height*PIXEL_SCALING)/2; //center X
	dst += FB_WIDTH_MAX*(FB_HEIGHT-emu_width*PIXEL_SCALING)/2; //center Y
	src -= emu_width;
	for(int x = 0; x < emu_height; ++x)
	{
		for(int y = 0; y < emu_width; ++y)
		{
			*dst = *src;
			src += 1;
			dst += PIXEL_SCALING*FB_WIDTH_MAX;
		}
		src -= 2*emu_width;
		dst += PIXEL_SCALING - emu_width*PIXEL_SCALING*FB_WIDTH_MAX;
	}
	fb_update(&fb, rotated_buffer, FB_WIDTH_MAX*sizeof(rotated_buffer[0]));
#else
	fb_update(&fb, pixel_buffer, emu_width*sizeof(pixel_buffer[0]));
#endif
}

enum KEYCODE sdl2keymap(int k)
{
  switch(k)
  {
    case SDLK_1: 		return KEYCODE_1;
    case SDLK_2: 		return KEYCODE_2;
    case SDLK_LEFT: 	return KEYCODE_LEFT;
    case SDLK_RIGHT:	return KEYCODE_RIGHT;
    case SDLK_UP:		return KEYCODE_UP;
    case SDLK_DOWN: 	return KEYCODE_DOWN;
  }
  return KEYCODE_NOKEY;
}

bool run_sim(void)
{
      
  SDL_Event event;
  while(SDL_PollEvent(&event))
  {
        switch(event.type)
        {
          case SDL_QUIT:
            return false;
          case SDL_KEYDOWN:
          case SDL_KEYUP:
            if(event.key.keysym.sym == SDLK_ESCAPE)
              return false;
            sim_setkey(sdl2keymap(event.key.keysym.sym), event.type==SDL_KEYDOWN);
            break;
        }
    }
    
  draw_frame(sim_width(), sim_height());
  uint64_t t = higres_ticks() * 1000000ull / higres_ticks_freq();
  return sim_exec(t);
}

#ifdef NAMCO_AUDIO_FLOAT 
static void push_audio(const float* samples, int num_samples, void* user_data) {
    (void)user_data;
    //saudio_push(samples, num_samples);
}
#else
static void push_audio(const int32_t* samples, int num_samples, void* user_data) {
    (void)user_data;
    static float samples_f[NAMCO_MAX_AUDIO_SAMPLES];
    for(int i = 0; i < num_samples; ++i)
    	samples_f[i] = (float)samples[i]/NAMCO_AUDIO_SAMPLE_SCALING;
    //saudio_push(samples_f, num_samples);
}
#endif

int main(int argc, char* argv[])
{
	fb_init(FB_WIDTH, FB_HEIGHT, true, &fb);
	signal(SIGINT, SIG_DFL); //allows to exit by ctrl-c
	sim_init(pixel_buffer,  sizeof(pixel_buffer), push_audio, DEFAULT_SAMPLERATE);
    while(run_sim());
    return 0;
}

#else


static uint32_t pixel_buffer[NAMCO_DISPLAY_WIDTH*NAMCO_DISPLAY_HEIGHT];

void FAST_CODE draw_frame(int emu_width, int emu_height)
{
#ifdef ROTATED_90
	const uint32_t *src = pixel_buffer+NAMCO_DISPLAY_HEIGHT*NAMCO_DISPLAY_WIDTH;
	uint32_t *dst = (uint32_t*) FB_PAGE1;
	dst += FB_WIDTH/2-emu_height*PIXEL_SCALING/2; //center X
	dst += FB_WIDTH*(FB_HEIGHT/2-NAMCO_DISPLAY_WIDTH*PIXEL_SCALING/2); //center Y
	src -= NAMCO_DISPLAY_WIDTH;
	for(int x = 0; x < NAMCO_DISPLAY_HEIGHT; ++x)
	{
		for(int y = 0; y < NAMCO_DISPLAY_WIDTH; ++y)
		{
			*dst = *src;
			src += 1;
			dst += PIXEL_SCALING*FB_WIDTH;
		}
		src -= 2*NAMCO_DISPLAY_WIDTH;
		dst += PIXEL_SCALING - NAMCO_DISPLAY_WIDTH*PIXEL_SCALING*FB_WIDTH;
	}
#else
	#error no rotation
#endif
}
static inline uint64_t cpu_hal_get_cycle_count64(void) { timer0_uptime_latch_write(1); return timer0_uptime_cycles_read(); }
#define micros() ((1000000ull*cpu_hal_get_cycle_count64())/LITETIMER_BASE_FREQUENCY)

bool run_sim(void)
{
  draw_frame(sim_width(), sim_height());
  uint64_t t = micros();
  printf("current time %llu\n", t);
  return sim_exec(t);
}

#ifdef NAMCO_AUDIO_FLOAT
#error floating point valued samples not supported
#else
static void push_audio(const int32_t* samples, int num_samples, void* user_data) {
    (void)user_data;
    //saudio_push(samples_f, num_samples);
}
#endif

void emu_main(void)
{
	fb_clear();
	sim_init(pixel_buffer, sizeof(pixel_buffer), push_audio, DEFAULT_SAMPLERATE);
    while(run_sim());
}


#endif


/////////////////////
//generic simulator

static /*FAST_DATA*/ namco_t sys;
void sim_init(uint32_t *framebuffer, size_t fb_size, audio_cb_t audio_cb, int samplerate)
{
    namco_init(&sys, &(namco_desc_t){
        .pixel_buffer = { .ptr = framebuffer, .size = fb_size },
        .audio = {
            .callback = { .func = audio_cb },
            .sample_rate = samplerate,
        },
        .roms = {
#ifdef NAMCO_PACMAN
            .common = {
                .cpu_0000_0FFF = { .ptr=dump_pacman_6e, .size = sizeof(dump_pacman_6e) },
                .cpu_1000_1FFF = { .ptr=dump_pacman_6f, .size = sizeof(dump_pacman_6f) },
                .cpu_2000_2FFF = { .ptr=dump_pacman_6h, .size = sizeof(dump_pacman_6h) },
                .cpu_3000_3FFF = { .ptr=dump_pacman_6j, .size = sizeof(dump_pacman_6j) },
                .prom_0000_001F = { .ptr=dump_82s123_7f, .size = sizeof(dump_82s123_7f) },
                .sound_0000_00FF = { .ptr=dump_82s126_1m, .size = sizeof(dump_82s126_1m) },
                .sound_0100_01FF = { .ptr=dump_82s126_3m, .size = sizeof(dump_82s126_3m) },
            },
            .pacman = {
                .gfx_0000_0FFF = { .ptr=dump_pacman_5e, .size = sizeof(dump_pacman_5e) },
                .gfx_1000_1FFF = { .ptr=dump_pacman_5f, .size = sizeof(dump_pacman_5f) },
                .prom_0020_011F = { .ptr=dump_82s126_4a, .size = sizeof(dump_82s126_4a) },
            }
#endif
#ifdef NAMCO_PENGO
            .common = {
                .cpu_0000_0FFF = { .ptr=dump_ep5120_8, .size=sizeof(dump_ep5120_8) },
                .cpu_1000_1FFF = { .ptr=dump_ep5121_7, .size=sizeof(dump_ep5121_7) },
                .cpu_2000_2FFF = { .ptr=dump_ep5122_15, .size=sizeof(dump_ep5122_15) },
                .cpu_3000_3FFF = { .ptr=dump_ep5123_14, .size=sizeof(dump_ep5123_14) },
                .prom_0000_001F = { .ptr=dump_pr1633_78, .size=sizeof(dump_pr1633_78) },
                .sound_0000_00FF = { .ptr=dump_pr1635_51, .size=sizeof(dump_pr1635_51) },
                .sound_0100_01FF = { .ptr=dump_pr1636_70, .size=sizeof(dump_pr1636_70) }
            },
            .pengo = {
                .cpu_4000_4FFF = { .ptr=dump_ep5124_21, .size=sizeof(dump_ep5124_21) },
                .cpu_5000_5FFF = { .ptr=dump_ep5125_20, .size=sizeof(dump_ep5125_20) },
                .cpu_6000_6FFF = { .ptr=dump_ep5126_32, .size=sizeof(dump_ep5126_32) },
                .cpu_7000_7FFF = { .ptr=dump_ep5127_31, .size=sizeof(dump_ep5127_31) },
                .gfx_0000_1FFF = { .ptr=dump_ep1640_92, .size=sizeof(dump_ep1640_92) },
                .gfx_2000_3FFF = { .ptr=dump_ep1695_105, .size=sizeof(dump_ep1695_105) },
                .prom_0020_041F = { .ptr=dump_pr1634_88, .size=sizeof(dump_pr1634_88) }
            }
#endif
        },
    });
}

bool sim_exec(uint64_t t1)
{
	static uint64_t t0 = -1;
	if(t0 == (uint64_t)-1)
	{
	  t0 = t1;
	  return true; //no simulation 
	}
	
    int64_t us = (int64_t)(t1-t0);
    t0 = t1;
    if(us > 1000000/60)
      us = 1000000/60;
    namco_exec(&sys, us);
    return true;
}

int sim_width(void) { return namco_display_width(&sys); }
int sim_height(void) { return namco_display_height(&sys); }

void sim_setkey(enum KEYCODE key, bool value)
{
    switch (key) {
		case KEYCODE_RIGHT:	(value ? namco_input_set : namco_input_clear) (&sys, NAMCO_INPUT_P1_RIGHT); break;
		case KEYCODE_LEFT:	(value ? namco_input_set : namco_input_clear) (&sys, NAMCO_INPUT_P1_LEFT); break;
		case KEYCODE_UP:	(value ? namco_input_set : namco_input_clear) (&sys, NAMCO_INPUT_P1_UP); break;
		case KEYCODE_DOWN:	(value ? namco_input_set : namco_input_clear) (&sys, NAMCO_INPUT_P1_DOWN); break;
		case KEYCODE_1:		(value ? namco_input_set : namco_input_clear) (&sys, NAMCO_INPUT_P1_COIN); break;
		case KEYCODE_2:		(value ? namco_input_set : namco_input_clear) (&sys, NAMCO_INPUT_P2_COIN); break;
		default:			(value ? namco_input_set : namco_input_clear) (&sys, NAMCO_INPUT_P1_START); break;
    }
}

///////////////////////////

