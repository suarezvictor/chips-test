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

//select project
#define NAMCO_PACMAN
//#define NAMCO_PENGO
//#define MOD_PLAYER


#ifdef __linux__
//#define NAMCO_AUDIO_FLOAT
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
#define NAMCO_USE_BGRA8 //invers palette
#endif

#define FB_WIDTH_MAX 1024 //next power of 2
#define DEFAULT_SAMPLERATE 44100
/////////////////////////
//simulator declarations

#if defined(NAMCO_PACMAN) || defined(NAMCO_PENGO)
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
#include "namco-optimized.h"
#define PIXEL_SCALING 2
#define ROTATED_90
#define SAMPLE_CONVERT(s) ((s)*((1<<30)/NAMCO_AUDIO_SAMPLE_SCALING))
#endif //not NAMCO_PACMAN nor NAMCO_PENGO

#ifdef MOD_PLAYER
#define PIXEL_SCALING 1
//FIXME: rename those namco-specific names
#define NAMCO_DEFAULT_AUDIO_SAMPLES (128*8)
#define NAMCO_DEFAULT_BUFFER_SAMPLES (16*1024)
#define NAMCO_MAX_AUDIO_SAMPLES NAMCO_DEFAULT_BUFFER_SAMPLES
typedef int32_t namco_sample_t;
#endif

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

static SDL_AudioDeviceID audio_device;
void audio_init(int samplerate, int num_samples)
{
	static SDL_AudioSpec specs = {0}, obtanined;
	specs.freq = samplerate;
	specs.channels = 1;
	specs.samples = num_samples;
	specs.userdata = 0;
	specs.callback = NULL; //no callback
#ifdef NAMCO_AUDIO_FLOAT 
	specs.format = AUDIO_F32SYS; //float32
#else
	specs.format = AUDIO_S32SYS; //int32
#endif

	SDL_InitSubSystem(SDL_INIT_AUDIO);
	audio_device = SDL_OpenAudioDevice(NULL, 0, &specs, &obtanined, SDL_AUDIO_ALLOW_CHANNELS_CHANGE|SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (audio_device)
	{
			SDL_PauseAudioDevice(audio_device, 0); //start playing
		    //printf("Audio ok, samplerate %d, num_samples %d\n", samplerate, num_samples);
	}
}

void audio_pushbuf(const void* samples, size_t buffer_size)
{
	SDL_QueueAudio(audio_device, samples, buffer_size);
}

//returns how much is in the buffer
#ifdef NAMCO_DEFAULT_BUFFER_SAMPLES
int audio_fifo_space(void)
{
  int queued = SDL_GetQueuedAudioSize(audio_device)/sizeof(namco_sample_t); //1 ch
  int frames = NAMCO_DEFAULT_AUDIO_SAMPLES;
  if(queued >= NAMCO_DEFAULT_BUFFER_SAMPLES)
    return 0;
  return frames; //always fixed packet size expected
}
#endif

int saudio_channels() { return 1; }

#ifdef NAMCO_AUDIO_FLOAT 
static void push_audio(const float* samples, int num_samples, void* user_data) {
    (void)user_data;
    audio_pushbuf(samples, num_samples*sizeof(*samples));
}
#else
static void push_audio(const int32_t* samples, int num_samples, void* user_data) {
    (void)user_data;
    static int32_t samples_conv[NAMCO_MAX_AUDIO_SAMPLES];
#ifdef SAMPLE_CONVERT
    for(int i = 0; i < num_samples; ++i)
    	samples_conv[i] = SAMPLE_CONVERT(samples[i]);
    audio_pushbuf(samples_conv, num_samples*sizeof(*samples));
#else
    audio_pushbuf(samples, num_samples*sizeof(*samples));
#endif
}
#endif

int main(int argc, char* argv[])
{
	fb_init(FB_WIDTH, FB_HEIGHT, true, &fb);
	audio_init(DEFAULT_SAMPLERATE, NAMCO_DEFAULT_AUDIO_SAMPLES);
	signal(SIGINT, SIG_DFL); //allows to exit by ctrl-c
	sim_init(pixel_buffer,  sizeof(pixel_buffer), push_audio, DEFAULT_SAMPLERATE);
    while(run_sim());
    return 0;
}

#else //not __linux__


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


#endif //__linux__


#if defined(NAMCO_PACMAN) || defined(NAMCO_PENGO)
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
#endif // NAMCO_PACMAN || NAMCO_PENGO

#ifdef MOD_PLAYER
/*
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

#include "modplug.h"
#include "data/mods.h"
#include <assert.h>

//#define AUDIO_HIGH_QUALITY

/* select between mono (1) and stereo (2) */
#define MODPLAY_NUM_CHANNELS (1)
/* use stream callback (0) or push-from-mainthread (1) model */
#define MODPLAY_USE_PUSH (1)
/* big enough for packet_size * num_packets * num_channels */
#define MODPLAY_SRCBUF_SAMPLES (16*1024)

typedef struct {
    bool mpf_valid;
    ModPlugFile* mpf;
#ifdef NAMCO_AUDIO_FLOAT
    int src_buf[MODPLAY_SRCBUF_SAMPLES]; //needed for conversion
#endif
    namco_sample_t dst_buf[MODPLAY_SRCBUF_SAMPLES];
    audio_cb_t audio_cb;
} state_t;

static state_t state;

static const int W = 400, H = 300; // window size

void sim_init(uint32_t *framebuffer, size_t fb_size, audio_cb_t audio_cb, int samplerate)
{
    /* setup libmodplug and load mod from embedded C array */
    ModPlug_Settings mps;
    ModPlug_GetSettings(&mps);
    mps.mChannels = MODPLAY_NUM_CHANNELS;
    mps.mBits = 32;
    mps.mFrequency = samplerate; //ok at 44100 and 8000
#ifdef AUDIO_HIGH_QUALITY
    mps.mResamplingMode = MODPLUG_RESAMPLE_LINEAR;
    mps.mFlags = MODPLUG_ENABLE_OVERSAMPLING;
#else
    mps.mResamplingMode = MODPLUG_RESAMPLE_NEAREST; //fine enough
    mps.mFlags = MODPLUG_ENABLE_OVERSAMPLING;
#endif
    mps.mMaxMixChannels = 64;
    mps.mLoopCount = -1; /* loop play seems to be disabled in current libmodplug */
    ModPlug_SetSettings(&mps);

    state.mpf = ModPlug_Load(embed_disco_feva_baby_s3m, sizeof(embed_disco_feva_baby_s3m));
    if (state.mpf) {
        state.mpf_valid = true;
    }
    state.audio_cb = audio_cb;
}


/* common function to read sample stream from libmodplug and convert to float */
static int read_samples(state_t* state, namco_sample_t* buffer, int num_samples) {
    assert(num_samples <= MODPLAY_SRCBUF_SAMPLES);
    if (!state->mpf_valid)
        return 0;
    /* NOTE: for multi-channel playback, the samples are interleaved
       (e.g. left/right/left/right/...)
    */
#ifndef NAMCO_AUDIO_FLOAT
    int res = ModPlug_Read(state->mpf, buffer, sizeof(namco_sample_t)*num_samples);
    return res / (int)sizeof(int);
#else
    int res = ModPlug_Read(state->mpf, (void*)state->src_buf, sizeof(namco_sample_t)*num_samples);
    int samples_in_buffer = res / (int)sizeof(int);
    int i;
    for (i = 0; i < samples_in_buffer; i++) {
        buffer[i] = state->src_buf[i] / (float)0x7fffffff;
    }
    for (; i < num_samples; i++) {
        buffer[i] = 0.0f;
    }
    return samples_in_buffer;
#endif
}

bool sim_exec(uint64_t t1)
{
    //draw something
	static uint8_t c = 0;
    memset(pixel_buffer, c, 50*sizeof(*pixel_buffer));
	c += 17;

    const int num_frames = audio_fifo_space();
    if (num_frames == 0)
      return true; //already full

  const int num_samples = num_frames * saudio_channels();
  int r = read_samples(&state, state.dst_buf, num_samples);
  if(r == 0)
    return false;

  state.audio_cb(state.dst_buf, r, NULL);
  return true;
}

int sim_width(void) { return W; }
int sim_height(void) { return H; }
void sim_setkey(enum KEYCODE key, bool value) {}

#endif // MOD_PLAYER

