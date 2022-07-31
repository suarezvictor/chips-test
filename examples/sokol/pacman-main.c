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

#define CHIPS_IMPL
#include "chips/z80.h"
#include "chips/clk.h"
#include "chips/mem.h"
#include "pacman-roms.h"
#define NAMCO_PACMAN
#include "systems/namco.h"

//callbacks
void push_audio(const float* samples, int num_samples, void* user_data);
int saudio_sample_rate(void);

uint32_t *gfx_framebuffer(void);
size_t gfx_framebuffer_size(void);

enum KEYCODE
{
  KEYCODE_NOKEY = 0,
  KEYCODE_RIGHT,
  KEYCODE_LEFT,
  KEYCODE_UP,
  KEYCODE_DOWN,
  KEYCODE_1,
  KEYCODE_2,
};

/////////////////////
//generic simulator

static namco_t sys;
void sim_init(void) { //FIXME: pass some parameters
    namco_init(&sys, &(namco_desc_t){
        .pixel_buffer = { .ptr = gfx_framebuffer(), .size = gfx_framebuffer_size() },
        .audio = {
            .callback = { .func = push_audio },
            .sample_rate = saudio_sample_rate(),
        },
        .roms = {
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
    namco_exec(&sys, us);
    return true;
}

int sim_width() { return namco_display_width(&sys); }
int sim_height() { return namco_display_height(&sys); }

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

#ifdef __linux__
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>

#include "sdl_fb.h"
#include <SDL2/SDL.h> //for events

#define FB_WIDTH 1024
static uint32_t pixel_buffer[FB_WIDTH*FB_WIDTH];
fb_handle_t fb;

#define PIXEL_SCALING 2
void draw_frame(int emu_width, int emu_height)
{
#if 1
	{
		static uint32_t rotated_buffer[FB_WIDTH*FB_WIDTH];
		const uint32_t *src = pixel_buffer+emu_height*emu_width;
		uint32_t *dst = rotated_buffer;
		for(int x = 0; x < emu_height; ++x)
		{
			src -= emu_width;
			for(int y = 0; y < emu_width; ++y)
				dst[x*PIXEL_SCALING+y*PIXEL_SCALING*FB_WIDTH] = src[y]; //FIXME: optimize
		}
		fb_update(&fb, rotated_buffer, FB_WIDTH*sizeof(rotated_buffer[0]));
	}
#else
	{
		//show something that may not be right
		fb_update(&fb, pixel_buffer, emu_width*sizeof(pixel_buffer[0]));
	}
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
    
  int w = sim_width(), h = sim_height();
  draw_frame(w, h);
  uint64_t t = higres_ticks() * 1000000ull / higres_ticks_freq();
  return sim_exec(t);
}

int main(int argc, char* argv[])
{
	fb_init(800, 600, true, &fb);
	signal(SIGINT, SIG_DFL); //allows to exit by ctrl-c
	sim_init();
    while(run_sim());
    return 0;
}

//callbacks
int saudio_sample_rate(void) { return 44100; }
uint32_t* gfx_framebuffer(void) { return pixel_buffer; }
size_t gfx_framebuffer_size(void) { return sizeof(pixel_buffer); }
void push_audio(const float* samples, int num_samples, void* user_data) {}

#endif

