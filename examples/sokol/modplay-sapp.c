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

//------------------------------------------------------------------------------
//  modplay-sapp.c
//  sokol_app + sokol_audio + libmodplug
//  This uses the user-data callback model both for sokol_app.h and
//  sokol_audio.h
//------------------------------------------------------------------------------
#include "common.h"
#include "modplug.h"
#include "data/mods.h"
#include <assert.h>

//#define AUDIO_HIGH_QUALITY

/* select between mono (1) and stereo (2) */
#define MODPLAY_NUM_CHANNELS (2)
/* use stream callback (0) or push-from-mainthread (1) model */
#define MODPLAY_USE_PUSH (1)
/* big enough for packet_size * num_packets * num_channels */
#define MODPLAY_SRCBUF_SAMPLES (16*1024)

typedef struct {
    bool mpf_valid;
    ModPlugFile* mpf;
    int int_buf[MODPLAY_SRCBUF_SAMPLES];
    #if MODPLAY_USE_PUSH
    float flt_buf[MODPLAY_SRCBUF_SAMPLES];
    #endif
} state_t;

/* integer-to-float conversion buffer */

/* common function to read sample stream from libmodplug and convert to float */
static void read_samples(state_t* state, float* buffer, int num_samples) {
    assert(num_samples <= MODPLAY_SRCBUF_SAMPLES);
    if (state->mpf_valid) {
        /* NOTE: for multi-channel playback, the samples are interleaved
           (e.g. left/right/left/right/...)
        */
        int res = ModPlug_Read(state->mpf, (void*)state->int_buf, (int)sizeof(int)*num_samples);
        int samples_in_buffer = res / (int)sizeof(int);
        int i;
        for (i = 0; i < samples_in_buffer; i++) {
            buffer[i] = state->int_buf[i] / (float)0x7fffffff;
        }
        for (; i < num_samples; i++) {
            buffer[i] = 0.0f;
        }
    }
    else {
        /* if file wasn't loaded, fill the output buffer with silence */
        for (int i = 0; i < num_samples; i++) {
            buffer[i] = 0.0f;
        }
    }
}

/* stream callback, called by sokol_audio when new samples are needed,
    on most platforms, this runs on a separate thread
*/
#if !MODPLAY_USE_PUSH
static void stream_cb(float* buffer, int num_frames, int num_channels, void* user_data) {
    state_t* state = (state_t*) user_data;
    const int num_samples = num_frames * num_channels;
    read_samples(state, buffer, num_samples);
}
#endif

void init(void* user_data) {
    state_t* state = (state_t*) user_data;
    gfx_init(&(gfx_desc_t) {
        .border_left = 0,
        .border_right = 0,
        .border_top = 0,
        .border_bottom = 0,
        .emu_aspect_x = 1,
        .emu_aspect_y = 1,
        .rot90 = false
    });
    clock_init();
    prof_init();
    /* setup sokol_audio (default sample rate is 44100Hz) */
    saudio_setup(&(saudio_desc){
        .num_channels = MODPLAY_NUM_CHANNELS,
        #if !MODPLAY_USE_PUSH
        .stream_userdata_cb = stream_cb,
        .user_data = state
        #else
    .buffer_frames = MODPLAY_SRCBUF_SAMPLES,
    .packet_frames = 1024
        #endif
    });

    /* setup libmodplug and load mod from embedded C array */
    ModPlug_Settings mps;
    ModPlug_GetSettings(&mps);
    mps.mChannels = saudio_channels();
    mps.mBits = 32;
    mps.mFrequency = saudio_sample_rate(); //ok at 44100 and 8000
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

    state->mpf = ModPlug_Load(embed_disco_feva_baby_s3m, sizeof(embed_disco_feva_baby_s3m));
    if (state->mpf) {
        state->mpf_valid = true;
    }
}

static state_t state;   /* static structs are implicitely zero-initialized */
static const int W = 400, H = 300; // window size

void frame(void* user_data) {
    /* alternative way to get audio data into sokol_audio: push the
        data from the main thread, this appends the sample data to a ring
        buffer where the audio thread will pull from
    */
    #if MODPLAY_USE_PUSH
        /* NOTE: if your application generates new samples at the same
           rate they are consumed (e.g. a steady 44100 frames per second,
           you don't need the call to saudio_expect(), instead just call
           saudio_push() as new sample data gets generated
        */
        state_t* state = (state_t*) user_data;
        const int num_frames = saudio_expect();
        if (num_frames > 0) {
            const int num_samples = num_frames * saudio_channels();
            read_samples(state, state->flt_buf, num_samples);
            saudio_push(state->flt_buf, num_frames);
        }
    #else
        (void)user_data;
    #endif
    gfx_draw(W, H);
}

void cleanup(void* user_data) {
    state_t* state = (state_t*) user_data;
    saudio_shutdown();
    if (state->mpf_valid) {
        ModPlug_Unload(state->mpf);
    }
#if 0
    sg_shutdown();
#else
    gfx_shutdown();
#endif
}

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    return (sapp_desc){
        .init_userdata_cb = init,
        .frame_userdata_cb = frame,
        .cleanup_userdata_cb = cleanup,
        .user_data = &state,
        .width = W,
        .height = H,
        .window_title = "Sokol Audio + LibModPlug",
    };
}
