/*
API REQUIREMENT LIST
===========================
sokol_app.h:
  sapp_widthf, sapp_heightf

sokol_audio.h:
  saudio_suspended, saudio_setup, saudio_shutdown, saudio_sample_rate, saudio_push

sokol_time.h:
  stm_since, stm_ms

common.h (in examples):
  prof_init, clock_init
  clock_frame_time
  gfx_framebuffer, gfx_framebuffer_size
  prof_push, prof_stats
  gfx_init, gfx_shutdown
  gfx_draw
*/


#ifdef USE_SOKOL_DIRECT
#define SOKOL_IMPL
#define SOKOL_GLCORE33
#include "sokol_app.h"
/*
REQUIRED BY pengo.c (provided by sokol_app.h):
sapp_widthf, sapp_heightf
*/

#include "sokol_audio.h"
/*
REQUIRED BY pengo.c (provided by sokol_audio.c):
saudio_suspended, saudio_setup, saudio_shutdown, saudio_sample_rate, saudio_push
*/

#include "sokol_time.h"
/*
REQUIRED BY pengo.c (provided by sokol_time.c):
stm_since, stm_ms
*/
#else //not USE_SOKOL_DIRECT

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h> //memset
#include <signal.h>
#include <pthread.h>
#include "sdl_fb.h"

#include "sokol_app.h" //no implementation requested

#ifndef SOKOL_API_IMPL
#define SOKOL_API_IMPL
#endif
#ifndef SOKOL_ASSERT
#define SOKOL_ASSERT(x)
#endif
#ifndef _SOKOL_UNUSED
#define _SOKOL_UNUSED(x) (void)(x)
#endif

#define SOKOL_LOG printf

SOKOL_API_IMPL void _sapp_linux_run(const sapp_desc* desc);
sapp_desc sokol_main(int argc, char* argv[]);
 
int main(int argc, char* argv[]) {
    sapp_desc desc = sokol_main(argc, argv);
    _sapp_linux_run(&desc);
    return 0;
}


////////////////////////////////
//from sokol_app.h
#include "gfx.h"

SOKOL_API_IMPL int sapp_width(void) {
    return GFX_MAX_FB_WIDTH;
}

SOKOL_API_IMPL float sapp_widthf(void) {
    return (float)sapp_width();
}

SOKOL_API_IMPL int sapp_height(void) {
    return GFX_MAX_FB_HEIGHT;
}

SOKOL_API_IMPL float sapp_heightf(void) {
    return (float)sapp_height();
}

////////////////////////////////
//from sokol_audio.h
#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include "sokol_audio.h"

#define _SAUDIO_DEFAULT_SAMPLE_RATE (44100)
#define _SAUDIO_DEFAULT_BUFFER_FRAMES (2048)
#define _SAUDIO_DEFAULT_PACKET_FRAMES (128)
#define _SAUDIO_DEFAULT_NUM_PACKETS ((_SAUDIO_DEFAULT_BUFFER_FRAMES/_SAUDIO_DEFAULT_PACKET_FRAMES)*4)

#define _SAUDIO_PTHREADS

#ifndef SAUDIO_RING_MAX_SLOTS
#define SAUDIO_RING_MAX_SLOTS (1024)
#endif

#if defined(_SAUDIO_PTHREADS)

typedef struct {
    pthread_mutex_t mutex;
} _saudio_mutex_t;

#elif defined(_SAUDIO_WINTHREADS)

typedef struct {
    CRITICAL_SECTION critsec;
} _saudio_mutex_t;

#elif defined(_SAUDIO_NOTHREADS)

typedef struct {
    int dummy_mutex;
} _saudio_mutex_t;

#endif

#define _saudio_def(val, def) (((val) == 0) ? (def) : (val))

void _saudio_clear(void* ptr, size_t size) {
    SOKOL_ASSERT(ptr && (size > 0));
    memset(ptr, 0, size);
}


typedef struct {
    snd_pcm_t* device;
    float* buffer;
    int buffer_byte_size;
    int buffer_frames;
    pthread_t thread;
    bool thread_stop;
} _saudio_backend_t;

/* a ringbuffer structure */
typedef struct {
    int head;  // next slot to write to
    int tail;  // next slot to read from
    int num;   // number of slots in queue
    int queue[SAUDIO_RING_MAX_SLOTS];
} _saudio_ring_t;

/* a packet FIFO structure */
typedef struct {
    bool valid;
    int packet_size;            /* size of a single packets in bytes(!) */
    int num_packets;            /* number of packet in fifo */
    uint8_t* base_ptr;          /* packet memory chunk base pointer (dynamically allocated) */
    int cur_packet;             /* current write-packet */
    int cur_offset;             /* current byte-offset into current write packet */
    _saudio_mutex_t mutex;      /* mutex for thread-safe access */
    _saudio_ring_t read_queue;  /* buffers with data, ready to be streamed */
    _saudio_ring_t write_queue; /* empty buffers, ready to be pushed to */
} _saudio_fifo_t;

/* sokol-audio state */
typedef struct {
    bool valid;
    void (*stream_cb)(float* buffer, int num_frames, int num_channels);
    void (*stream_userdata_cb)(float* buffer, int num_frames, int num_channels, void* user_data);
    void* user_data;
    int sample_rate;            /* sample rate */
    int buffer_frames;          /* number of frames in streaming buffer */
    int bytes_per_frame;        /* filled by backend */
    int packet_frames;          /* number of frames in a packet */
    int num_packets;            /* number of packets in packet queue */
    int num_channels;           /* actual number of channels */
    saudio_desc desc;
    _saudio_fifo_t fifo;
    _saudio_backend_t backend;
} _saudio_state_t;

static _saudio_state_t _saudio;

void *_saudio_malloc(size_t size) {
    SOKOL_ASSERT(size > 0);
    void* ptr;
    if (_saudio.desc.allocator.alloc) {
        ptr = _saudio.desc.allocator.alloc(size, _saudio.desc.allocator.user_data);
    }
    else {
        ptr = malloc(size);
    }
    SOKOL_ASSERT(ptr);
    return ptr;
}

void *_saudio_malloc_clear(size_t size) {
    void* ptr = _saudio_malloc(size);
    _saudio_clear(ptr, size);
    return ptr;
}

void _saudio_free(void* ptr) {
    if (_saudio.desc.allocator.free) {
        _saudio.desc.allocator.free(ptr, _saudio.desc.allocator.user_data);
    }
    else {
        free(ptr);
    }
}


void _saudio_mutex_destroy(_saudio_mutex_t* m) {
    pthread_mutex_destroy(&m->mutex);
}
void _saudio_mutex_lock(_saudio_mutex_t* m) {
    pthread_mutex_lock(&m->mutex);
}
void _saudio_mutex_unlock(_saudio_mutex_t* m) {
    pthread_mutex_unlock(&m->mutex);
}


void _saudio_ring_init(_saudio_ring_t* ring, int num_slots) {
    SOKOL_ASSERT((num_slots + 1) <= SAUDIO_RING_MAX_SLOTS);
    ring->head = 0;
    ring->tail = 0;
    /* one slot reserved to detect 'full' vs 'empty' */
    ring->num = num_slots + 1;
}

int _saudio_ring_idx(_saudio_ring_t* ring, int i) {
    return (i % ring->num);
}

bool _saudio_ring_empty(_saudio_ring_t* ring) {
    return ring->head == ring->tail;
}

void _saudio_ring_enqueue(_saudio_ring_t* ring, int val) {
    SOKOL_ASSERT(!_saudio_ring_full(ring));
    ring->queue[ring->head] = val;
    ring->head = _saudio_ring_idx(ring, ring->head + 1);
}

int _saudio_ring_dequeue(_saudio_ring_t* ring) {
    SOKOL_ASSERT(!_saudio_ring_empty(ring));
    int val = ring->queue[ring->tail];
    ring->tail = _saudio_ring_idx(ring, ring->tail + 1);
    return val;
}

void _saudio_fifo_init(_saudio_fifo_t* fifo, int packet_size, int num_packets) {
    /* NOTE: there's a chicken-egg situation during the init phase where the
        streaming thread must be started before the fifo is actually initialized,
        thus the fifo init must already be protected from access by the fifo_read() func.
    */
    _saudio_mutex_lock(&fifo->mutex);
    SOKOL_ASSERT((packet_size > 0) && (num_packets > 0));
    fifo->packet_size = packet_size;
    fifo->num_packets = num_packets;
    fifo->base_ptr = (uint8_t*) _saudio_malloc((size_t)(packet_size * num_packets));
    fifo->cur_packet = -1;
    fifo->cur_offset = 0;
    _saudio_ring_init(&fifo->read_queue, num_packets);
    _saudio_ring_init(&fifo->write_queue, num_packets);
    for (int i = 0; i < num_packets; i++) {
        _saudio_ring_enqueue(&fifo->write_queue, i);
    }
    SOKOL_ASSERT(_saudio_ring_full(&fifo->write_queue));
    SOKOL_ASSERT(_saudio_ring_count(&fifo->write_queue) == num_packets);
    SOKOL_ASSERT(_saudio_ring_empty(&fifo->read_queue));
    SOKOL_ASSERT(_saudio_ring_count(&fifo->read_queue) == 0);
    fifo->valid = true;
    _saudio_mutex_unlock(&fifo->mutex);
}


int _saudio_ring_count(_saudio_ring_t* ring) {
    int count;
    if (ring->head >= ring->tail) {
        count = ring->head - ring->tail;
    }
    else {
        count = (ring->head + ring->num) - ring->tail;
    }
    SOKOL_ASSERT(count < ring->num);
    return count;
}


/* read queued data, this is called form the stream callback (maybe separate thread) */
int _saudio_fifo_read(_saudio_fifo_t* fifo, uint8_t* ptr, int num_bytes) {
    /* NOTE: fifo_read might be called before the fifo is properly initialized */
    _saudio_mutex_lock(&fifo->mutex);
    int num_bytes_copied = 0;
    if (fifo->valid) {
        SOKOL_ASSERT(0 == (num_bytes % fifo->packet_size));
        SOKOL_ASSERT(num_bytes <= (fifo->packet_size * fifo->num_packets));
        const int num_packets_needed = num_bytes / fifo->packet_size;
        uint8_t* dst = ptr;
        /* either pull a full buffer worth of data, or nothing */
        if (_saudio_ring_count(&fifo->read_queue) >= num_packets_needed) {
            for (int i = 0; i < num_packets_needed; i++) {
                int packet_index = _saudio_ring_dequeue(&fifo->read_queue);
                _saudio_ring_enqueue(&fifo->write_queue, packet_index);
                const uint8_t* src = fifo->base_ptr + packet_index * fifo->packet_size;
                memcpy(dst, src, (size_t)fifo->packet_size);
                dst += fifo->packet_size;
                num_bytes_copied += fifo->packet_size;
            }
            SOKOL_ASSERT(num_bytes == num_bytes_copied);
        }
    }
    _saudio_mutex_unlock(&fifo->mutex);
    return num_bytes_copied;
}


/* write new data to the write queue, this is called from main thread */
int _saudio_fifo_write(_saudio_fifo_t* fifo, const uint8_t* ptr, int num_bytes) {
    /* returns the number of bytes written, this will be smaller then requested
        if the write queue runs full
    */
    int all_to_copy = num_bytes;
    while (all_to_copy > 0) {
        /* need to grab a new packet? */
        if (fifo->cur_packet == -1) {
            _saudio_mutex_lock(&fifo->mutex);
            if (!_saudio_ring_empty(&fifo->write_queue)) {
                fifo->cur_packet = _saudio_ring_dequeue(&fifo->write_queue);
            }
            _saudio_mutex_unlock(&fifo->mutex);
            SOKOL_ASSERT(fifo->cur_offset == 0);
        }
        /* append data to current write packet */
        if (fifo->cur_packet != -1) {
            int to_copy = all_to_copy;
            const int max_copy = fifo->packet_size - fifo->cur_offset;
            if (to_copy > max_copy) {
                to_copy = max_copy;
            }
            uint8_t* dst = fifo->base_ptr + fifo->cur_packet * fifo->packet_size + fifo->cur_offset;
            memcpy(dst, ptr, (size_t)to_copy);
            ptr += to_copy;
            fifo->cur_offset += to_copy;
            all_to_copy -= to_copy;
            SOKOL_ASSERT(fifo->cur_offset <= fifo->packet_size);
            SOKOL_ASSERT(all_to_copy >= 0);
        }
        else {
            /* early out if we're starving */
            int bytes_copied = num_bytes - all_to_copy;
            SOKOL_ASSERT((bytes_copied >= 0) && (bytes_copied < num_bytes));
            return bytes_copied;
        }
        /* if write packet is full, push to read queue */
        if (fifo->cur_offset == fifo->packet_size) {
            _saudio_mutex_lock(&fifo->mutex);
            _saudio_ring_enqueue(&fifo->read_queue, fifo->cur_packet);
            _saudio_mutex_unlock(&fifo->mutex);
            fifo->cur_packet = -1;
            fifo->cur_offset = 0;
        }
    }
    SOKOL_ASSERT(all_to_copy == 0);
    return num_bytes;
}
void _saudio_fifo_shutdown(_saudio_fifo_t* fifo) {
    SOKOL_ASSERT(fifo->base_ptr);
    _saudio_free(fifo->base_ptr);
    fifo->base_ptr = 0;
    fifo->valid = false;
    _saudio_mutex_destroy(&fifo->mutex);
}


void _saudio_mutex_init(_saudio_mutex_t* m) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutex_init(&m->mutex, &attr);
}

void _saudio_fifo_init_mutex(_saudio_fifo_t* fifo) {
    /* this must be called before initializing both the backend and the fifo itself! */
    _saudio_mutex_init(&fifo->mutex);
}


bool _saudio_has_callback(void) {
    return (_saudio.stream_cb || _saudio.stream_userdata_cb);
}

void _saudio_stream_callback(float* buffer, int num_frames, int num_channels) {
    if (_saudio.stream_cb) {
        _saudio.stream_cb(buffer, num_frames, num_channels);
    }
    else if (_saudio.stream_userdata_cb) {
        _saudio.stream_userdata_cb(buffer, num_frames, num_channels, _saudio.user_data);
    }
}


/* the streaming callback runs in a separate thread */
void* _saudio_alsa_cb(void* param) {
    _SOKOL_UNUSED(param);
    while (!_saudio.backend.thread_stop) {
        /* snd_pcm_writei() will be blocking until it needs data */
        int write_res = snd_pcm_writei(_saudio.backend.device, _saudio.backend.buffer, (snd_pcm_uframes_t)_saudio.backend.buffer_frames);
        if (write_res < 0) {
            /* underrun occurred */
            snd_pcm_prepare(_saudio.backend.device);
        }
        else {
            /* fill the streaming buffer with new data */
            if (_saudio_has_callback()) {
                _saudio_stream_callback(_saudio.backend.buffer, _saudio.backend.buffer_frames, _saudio.num_channels);
            }
            else {
                if (0 == _saudio_fifo_read(&_saudio.fifo, (uint8_t*)_saudio.backend.buffer, _saudio.backend.buffer_byte_size)) {
                    /* not enough read data available, fill the entire buffer with silence */
                    _saudio_clear(_saudio.backend.buffer, (size_t)_saudio.backend.buffer_byte_size);
                }
            }
        }
    }
    return 0;
}

bool _saudio_backend_init(void) {
    int dir; uint32_t rate;
    int rc = snd_pcm_open(&_saudio.backend.device, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        SOKOL_LOG("sokol_audio.h: snd_pcm_open() failed");
        return false;
    }

    /* configuration works by restricting the 'configuration space' step
       by step, we require all parameters except the sample rate to
       match perfectly
    */
    snd_pcm_hw_params_t* params = 0;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(_saudio.backend.device, params);
    snd_pcm_hw_params_set_access(_saudio.backend.device, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (0 > snd_pcm_hw_params_set_format(_saudio.backend.device, params, SND_PCM_FORMAT_FLOAT_LE)) {
        SOKOL_LOG("sokol_audio.h: float samples not supported");
        goto error;
    }
    if (0 > snd_pcm_hw_params_set_buffer_size(_saudio.backend.device, params, (snd_pcm_uframes_t)_saudio.buffer_frames)) {
        SOKOL_LOG("sokol_audio.h: requested buffer size not supported");
        goto error;
    }
    if (0 > snd_pcm_hw_params_set_channels(_saudio.backend.device, params, (uint32_t)_saudio.num_channels)) {
        SOKOL_LOG("sokol_audio.h: requested channel count not supported");
        goto error;
    }
    /* let ALSA pick a nearby sampling rate */
    rate = (uint32_t) _saudio.sample_rate;
    dir = 0;
    if (0 > snd_pcm_hw_params_set_rate_near(_saudio.backend.device, params, &rate, &dir)) {
        SOKOL_LOG("sokol_audio.h: snd_pcm_hw_params_set_rate_near() failed");
        goto error;
    }
    if (0 > snd_pcm_hw_params(_saudio.backend.device, params)) {
        SOKOL_LOG("sokol_audio.h: snd_pcm_hw_params() failed");
        goto error;
    }

    /* read back actual sample rate and channels */
    _saudio.sample_rate = (int)rate;
    _saudio.bytes_per_frame = _saudio.num_channels * (int)sizeof(float);

    /* allocate the streaming buffer */
    _saudio.backend.buffer_byte_size = _saudio.buffer_frames * _saudio.bytes_per_frame;
    _saudio.backend.buffer_frames = _saudio.buffer_frames;
    _saudio.backend.buffer = (float*) _saudio_malloc_clear((size_t)_saudio.backend.buffer_byte_size);

    /* create the buffer-streaming start thread */
    if (0 != pthread_create(&_saudio.backend.thread, 0, _saudio_alsa_cb, 0)) {
        SOKOL_LOG("sokol_audio.h: pthread_create() failed");
        goto error;
    }

    return true;
error:
    if (_saudio.backend.device) {
        snd_pcm_close(_saudio.backend.device);
        _saudio.backend.device = 0;
    }
    return false;
};

void _saudio_backend_shutdown(void) {
    SOKOL_ASSERT(_saudio.backend.device);
    _saudio.backend.thread_stop = true;
    pthread_join(_saudio.backend.thread, 0);
    snd_pcm_drain(_saudio.backend.device);
    snd_pcm_close(_saudio.backend.device);
    _saudio_free(_saudio.backend.buffer);
};

SOKOL_API_IMPL void saudio_setup(const saudio_desc* desc) {
    SOKOL_ASSERT(!_saudio.valid);
    SOKOL_ASSERT(desc);
    SOKOL_ASSERT((desc->allocator.alloc && desc->allocator.free) || (!desc->allocator.alloc && !desc->allocator.free));
    _saudio_clear(&_saudio, sizeof(_saudio));
    _saudio.desc = *desc;
    _saudio.stream_cb = desc->stream_cb;
    _saudio.stream_userdata_cb = desc->stream_userdata_cb;
    _saudio.user_data = desc->user_data;
    _saudio.sample_rate = _saudio_def(_saudio.desc.sample_rate, _SAUDIO_DEFAULT_SAMPLE_RATE);
    _saudio.buffer_frames = _saudio_def(_saudio.desc.buffer_frames, _SAUDIO_DEFAULT_BUFFER_FRAMES);
    _saudio.packet_frames = _saudio_def(_saudio.desc.packet_frames, _SAUDIO_DEFAULT_PACKET_FRAMES);
    _saudio.num_packets = _saudio_def(_saudio.desc.num_packets, _SAUDIO_DEFAULT_NUM_PACKETS);
    _saudio.num_channels = _saudio_def(_saudio.desc.num_channels, 1);
    _saudio_fifo_init_mutex(&_saudio.fifo);
    if (_saudio_backend_init()) {
        /* the backend might not support the requested exact buffer size,
           make sure the actual buffer size is still a multiple of
           the requested packet size
        */
        if (0 != (_saudio.buffer_frames % _saudio.packet_frames)) {
            SOKOL_LOG("sokol_audio.h: actual backend buffer size isn't multiple of requested packet size");
            _saudio_backend_shutdown();
            return;
        }
        SOKOL_ASSERT(_saudio.bytes_per_frame > 0);
        _saudio_fifo_init(&_saudio.fifo, _saudio.packet_frames * _saudio.bytes_per_frame, _saudio.num_packets);
        _saudio.valid = true;
    }
}

SOKOL_API_IMPL void saudio_shutdown(void) {
    if (_saudio.valid) {
        _saudio_backend_shutdown();
        _saudio_fifo_shutdown(&_saudio.fifo);
        _saudio.valid = false;
    }
}

SOKOL_API_IMPL int saudio_sample_rate(void) {
    printf("sample rate %d\n", _saudio.sample_rate);
    return _saudio.sample_rate;
}


extern uint32_t rgba8_buffer[];

SOKOL_API_IMPL int saudio_push(const float* frames, int num_frames) {
  //simple synth
  /*
  for(int i = 0; i < num_frames; ++i)
  {
    static float wt = 0;
    wt += 2*3.14*1000/44100;
    ((float*)frames)[i] += .25*sin(wt); //adds a signal	
  }
  */
  
  //memcpy(rgba8_buffer+0x1000, frames,  num_frames*sizeof(*frames)); //hacky screen dump. CAUTION: may overwrite memory
  printf("pushing %d samples some sample=%f\n", num_frames, frames[num_frames/2]);

    SOKOL_ASSERT(frames && (num_frames > 0));
    if (_saudio.valid) {
        const int num_bytes = num_frames * _saudio.bytes_per_frame;
        const int num_written = _saudio_fifo_write(&_saudio.fifo, (const uint8_t*)frames, num_bytes);
        return num_written / _saudio.bytes_per_frame;
    }
    else {
        return 0;
    }
}



////////////////////////////////
//from sokol_time.h
#include <time.h>

typedef struct {
    uint32_t initialized;
    uint64_t start;
} _stm_state_t;
static _stm_state_t _stm;

SOKOL_API_IMPL void stm_setup(void) {
    memset(&_stm, 0, sizeof(_stm));
    _stm.initialized = 0xABCDABCD;
    #if defined(_WIN32)
        QueryPerformanceFrequency(&_stm.freq);
        QueryPerformanceCounter(&_stm.start);
    #elif defined(__APPLE__) && defined(__MACH__)
        mach_timebase_info(&_stm.timebase);
        _stm.start = mach_absolute_time();
    #elif defined(__EMSCRIPTEN__)
        _stm.start = emscripten_get_now();
    #else
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        _stm.start = (uint64_t)ts.tv_sec*1000000000 + (uint64_t)ts.tv_nsec;
    #endif
}

SOKOL_API_IMPL uint64_t stm_now(void) {
    SOKOL_ASSERT(_stm.initialized == 0xABCDABCD);
    uint64_t now;
    #if defined(_WIN32)
        LARGE_INTEGER qpc_t;
        QueryPerformanceCounter(&qpc_t);
        now = (uint64_t) _stm_int64_muldiv(qpc_t.QuadPart - _stm.start.QuadPart, 1000000000, _stm.freq.QuadPart);
    #elif defined(__APPLE__) && defined(__MACH__)
        const uint64_t mach_now = mach_absolute_time() - _stm.start;
        now = (uint64_t) _stm_int64_muldiv((int64_t)mach_now, (int64_t)_stm.timebase.numer, (int64_t)_stm.timebase.denom);
    #elif defined(__EMSCRIPTEN__)
        double js_now = emscripten_get_now() - _stm.start;
        now = (uint64_t) (js_now * 1000000.0);
    #else
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        now = ((uint64_t)ts.tv_sec*1000000000 + (uint64_t)ts.tv_nsec) - _stm.start;
    #endif
    return now;
}

SOKOL_API_IMPL uint64_t stm_diff(uint64_t new_ticks, uint64_t old_ticks) {
    if (new_ticks > old_ticks) {
        return new_ticks - old_ticks;
    }
    else {
        return 1;
    }
}

SOKOL_API_IMPL uint64_t stm_since(uint64_t start_ticks) {
    return stm_diff(stm_now(), start_ticks);
}


SOKOL_API_IMPL double stm_sec(uint64_t ticks) {
    return (double)ticks / 1000000000.0;
}

SOKOL_API_IMPL double stm_ms(uint64_t ticks) {
    return (double)ticks / 1000000.0;
}

SOKOL_API_IMPL double stm_us(uint64_t ticks) {
    return (double)ticks / 1000.0;
}

SOKOL_API_IMPL double stm_ns(uint64_t ticks) {
    return (double)ticks;
}



////////////////////////
//OS-specific
#define _sapp_def(val, def) (((val) == 0) ? (def) : (val))

#define _SAPP_RING_NUM_SLOTS (256)
typedef struct {
    int head;
    int tail;
    double buf[_SAPP_RING_NUM_SLOTS];
} _sapp_ring_t;

typedef struct {
    #if defined(_SAPP_APPLE)
        struct {
            mach_timebase_info_data_t timebase;
            uint64_t start;
        } mach;
    #elif defined(_SAPP_EMSCRIPTEN)
        // empty
    #elif defined(_SAPP_WIN32) || defined(_SAPP_UWP)
        struct {
            LARGE_INTEGER freq;
            LARGE_INTEGER start;
        } win;
    #else // Linux, Android, ...
        #ifdef CLOCK_MONOTONIC
        #define _SAPP_CLOCK_MONOTONIC CLOCK_MONOTONIC
        #else
        // on some embedded platforms, CLOCK_MONOTONIC isn't defined
        #define _SAPP_CLOCK_MONOTONIC (1)
        #endif
        struct {
            uint64_t start;
        } posix;
    #endif
} _sapp_timestamp_t;

typedef struct {
    double last;
    double accum;
    double avg;
    int spike_count;
    int num;
    _sapp_timestamp_t timestamp;
    _sapp_ring_t ring;
} _sapp_timing_t;

typedef struct {
    sapp_desc desc;
    bool valid;
    bool fullscreen;
    bool gles2_fallback;
    bool first_frame;
    bool init_called;
    bool cleanup_called;
    bool quit_requested;
    bool quit_ordered;
    bool event_consumed;
    bool html5_ask_leave_site;
    bool onscreen_keyboard_shown;
    int window_width;
    int window_height;
    int framebuffer_width;
    int framebuffer_height;
    int sample_count;
    int swap_interval;
    float dpi_scale;
    uint64_t frame_count;
    _sapp_timing_t timing;
    /*
    sapp_event event;
    _sapp_mouse_t mouse;
    _sapp_clipboard_t clipboard;
    _sapp_drop_t drop;
    sapp_icon_desc default_icon_desc;
    */
    uint32_t* default_icon_pixels;
    #if defined(_SAPP_MACOS)
        _sapp_macos_t macos;
    #elif defined(_SAPP_IOS)
        _sapp_ios_t ios;
    #elif defined(_SAPP_EMSCRIPTEN)
        _sapp_emsc_t emsc;
    #elif defined(_SAPP_WIN32)
        _sapp_win32_t win32;
        #if defined(SOKOL_D3D11)
            _sapp_d3d11_t d3d11;
        #elif defined(SOKOL_GLCORE33)
            _sapp_wgl_t wgl;
        #endif
    #elif defined(_SAPP_UWP)
            _sapp_uwp_t uwp;
        #if defined(SOKOL_D3D11)
            _sapp_d3d11_t d3d11;
        #endif
    #elif defined(_SAPP_ANDROID)
        _sapp_android_t android;
    #elif defined(_SAPP_LINUX)
        _sapp_x11_t x11;
        _sapp_glx_t glx;
    #endif
    /*
    char html5_canvas_selector[_SAPP_MAX_TITLE_LENGTH];
    char window_title[_SAPP_MAX_TITLE_LENGTH];      // UTF-8
    wchar_t window_title_wide[_SAPP_MAX_TITLE_LENGTH];   // UTF-32 or UCS-2
    */
    sapp_keycode keycodes[SAPP_MAX_KEYCODES];
} _sapp_t;

static _sapp_t _sapp;

void _sapp_call_init(void) {
    if (_sapp.desc.init_cb) {
        _sapp.desc.init_cb();
    }
    else if (_sapp.desc.init_userdata_cb) {
        _sapp.desc.init_userdata_cb(_sapp.desc.user_data);
    }
    _sapp.init_called = true;
}

void _sapp_call_frame(void) {
    if (_sapp.init_called && !_sapp.cleanup_called) {
        if (_sapp.desc.frame_cb) {
            _sapp.desc.frame_cb();
        }
        else if (_sapp.desc.frame_userdata_cb) {
            _sapp.desc.frame_userdata_cb(_sapp.desc.user_data);
        }
    }
}

void _sapp_frame(void) {
    if (_sapp.first_frame) {
        _sapp.first_frame = false;
        _sapp_call_init();
    }
    _sapp_call_frame();
    _sapp.frame_count++;
}

void _sapp_ring_init(_sapp_ring_t* ring) {
    ring->head = 0;
    ring->tail = 0;
}

void _sapp_timing_reset(_sapp_timing_t* t) {
    t->last = 0.0;
    t->accum = 0.0;
    t->spike_count = 0;
    t->num = 0;
    _sapp_ring_init(&t->ring);
}


void _sapp_timestamp_init(_sapp_timestamp_t* ts) {
    #if defined(_SAPP_APPLE)
        mach_timebase_info(&ts->mach.timebase);
        ts->mach.start = mach_absolute_time();
    #elif defined(_SAPP_EMSCRIPTEN)
        (void)ts;
    #elif defined(_SAPP_WIN32) || defined(_SAPP_UWP)
        QueryPerformanceFrequency(&ts->win.freq);
        QueryPerformanceCounter(&ts->win.start);
    #else
        struct timespec tspec;
        clock_gettime(_SAPP_CLOCK_MONOTONIC, &tspec);
        ts->posix.start = (uint64_t)tspec.tv_sec*1000000000 + (uint64_t)tspec.tv_nsec;
    #endif
}

void _sapp_timing_init(_sapp_timing_t* t) {
    t->avg = 1.0 / 60.0;    // dummy value until first actual value is available
    _sapp_timing_reset(t);
    _sapp_timestamp_init(&t->timestamp);
}

void _sapp_clear(void* ptr, size_t size) {
    SOKOL_ASSERT(ptr && (size > 0));
    memset(ptr, 0, size);
}


sapp_desc _sapp_desc_defaults(const sapp_desc* desc) {
    SOKOL_ASSERT((desc->allocator.alloc && desc->allocator.free) || (!desc->allocator.alloc && !desc->allocator.free));
    sapp_desc res = *desc;
    res.sample_count = _sapp_def(res.sample_count, 1);
    res.swap_interval = _sapp_def(res.swap_interval, 1);
    // NOTE: can't patch the default for gl_major_version and gl_minor_version
    // independently, because a desired version 4.0 would be patched to 4.2
    // (or expressed differently: zero is a valid value for gl_minor_version
    // and can't be used to indicate 'default')
    if (0 == res.gl_major_version) {
        res.gl_major_version = 3;
        res.gl_minor_version = 2;
    }
    /*
    res.html5_canvas_name = _sapp_def(res.html5_canvas_name, "canvas");
    res.clipboard_size = _sapp_def(res.clipboard_size, 8192);
    res.max_dropped_files = _sapp_def(res.max_dropped_files, 1);
    res.max_dropped_file_path_length = _sapp_def(res.max_dropped_file_path_length, 2048);
    res.window_title = _sapp_def(res.window_title, "sokol_app");
    */
    return res;
}

#if defined(_SAPP_MACOS) || defined(_SAPP_IOS)
    // this is ARC compatible
    #if defined(__cplusplus)
        #define _SAPP_CLEAR_ARC_STRUCT(type, item) { item = type(); }
    #else
        #define _SAPP_CLEAR_ARC_STRUCT(type, item) { item = (type) { 0 }; }
    #endif
#else
    #define _SAPP_CLEAR_ARC_STRUCT(type, item) { _sapp_clear(&item, sizeof(item)); }
#endif

void _sapp_init_state(const sapp_desc* desc) {
    SOKOL_ASSERT(desc);
    SOKOL_ASSERT(desc->width >= 0);
    SOKOL_ASSERT(desc->height >= 0);
    SOKOL_ASSERT(desc->sample_count >= 0);
    SOKOL_ASSERT(desc->swap_interval >= 0);
    SOKOL_ASSERT(desc->clipboard_size >= 0);
    SOKOL_ASSERT(desc->max_dropped_files >= 0);
    SOKOL_ASSERT(desc->max_dropped_file_path_length >= 0);
    _SAPP_CLEAR_ARC_STRUCT(_sapp_t, _sapp);
    _sapp.desc = _sapp_desc_defaults(desc);
    _sapp.first_frame = true;
    // NOTE: _sapp.desc.width/height may be 0! Platform backends need to deal with this
    _sapp.window_width = _sapp.desc.width;
    _sapp.window_height = _sapp.desc.height;
    _sapp.framebuffer_width = _sapp.window_width;
    _sapp.framebuffer_height = _sapp.window_height;
    _sapp.sample_count = _sapp.desc.sample_count;
    _sapp.swap_interval = _sapp.desc.swap_interval;
    /*
    _sapp.html5_canvas_selector[0] = '#';
    _sapp_strcpy(_sapp.desc.html5_canvas_name, &_sapp.html5_canvas_selector[1], sizeof(_sapp.html5_canvas_selector) - 1);
    _sapp.desc.html5_canvas_name = &_sapp.html5_canvas_selector[1];
    _sapp.html5_ask_leave_site = _sapp.desc.html5_ask_leave_site;
    _sapp.clipboard.enabled = _sapp.desc.enable_clipboard;
    if (_sapp.clipboard.enabled) {
        _sapp.clipboard.buf_size = _sapp.desc.clipboard_size;
        _sapp.clipboard.buffer = (char*) _sapp_malloc_clear((size_t)_sapp.clipboard.buf_size);
    }
    _sapp.drop.enabled = _sapp.desc.enable_dragndrop;
    if (_sapp.drop.enabled) {
        _sapp.drop.max_files = _sapp.desc.max_dropped_files;
        _sapp.drop.max_path_length = _sapp.desc.max_dropped_file_path_length;
        _sapp.drop.buf_size = _sapp.drop.max_files * _sapp.drop.max_path_length;
        _sapp.drop.buffer = (char*) _sapp_malloc_clear((size_t)_sapp.drop.buf_size);
    }
    */
    //_sapp_strcpy(_sapp.desc.window_title, _sapp.window_title, sizeof(_sapp.window_title));
    //_sapp.desc.window_title = _sapp.window_title;
    _sapp.dpi_scale = 1.0f;
    _sapp.fullscreen = _sapp.desc.fullscreen;
    //_sapp.mouse.shown = true;
    _sapp_timing_init(&_sapp.timing);
}

void _sapp_linux_run(const sapp_desc* desc) {
    /* The following lines are here to trigger a linker error instead of an
        obscure runtime error if the user has forgotten to add -pthread to
        the compiler or linker options. They have no other purpose.
    pthread_attr_t pthread_attr;
    pthread_attr_init(&pthread_attr);
    pthread_attr_destroy(&pthread_attr);
    */

    _sapp_init_state(desc);
    /*
    _sapp.x11.window_state = NormalState;

    XInitThreads();
    XrmInitialize();
    _sapp.x11.display = XOpenDisplay(NULL);
    if (!_sapp.x11.display) {
        _sapp_fail("XOpenDisplay() failed!\n");
    }
    _sapp.x11.screen = DefaultScreen(_sapp.x11.display);
    _sapp.x11.root = DefaultRootWindow(_sapp.x11.display);
    XkbSetDetectableAutoRepeat(_sapp.x11.display, true, NULL);
    _sapp_x11_query_system_dpi();
    _sapp.dpi_scale = _sapp.x11.dpi / 96.0f;
    _sapp_x11_init_extensions();
    _sapp_x11_create_cursors();
    _sapp_glx_init();
    Visual* visual = 0;
    int depth = 0;
    _sapp_glx_choose_visual(&visual, &depth);
    _sapp_x11_create_window(visual, depth);
    _sapp_glx_create_context();
    sapp_set_icon(&desc->icon);
    _sapp.valid = true;
    _sapp_x11_show_window();
    if (_sapp.fullscreen) {
        _sapp_x11_set_fullscreen(true);
    }
    _sapp_glx_swapinterval(_sapp.swap_interval);

    XFlush(_sapp.x11.display);
    while (!_sapp.quit_ordered) {
        _sapp_timing_measure(&_sapp.timing);
        _sapp_glx_make_current();
        int count = XPending(_sapp.x11.display);
        while (count--) {
            XEvent event;
            XNextEvent(_sapp.x11.display, &event);
            _sapp_x11_process_event(&event);
        }
        _sapp_frame();
        _sapp_glx_swap_buffers();
        XFlush(_sapp.x11.display);
        // handle quit-requested, either from window or from sapp_request_quit()
        if (_sapp.quit_requested && !_sapp.quit_ordered) {
            // give user code a chance to intervene
            _sapp_x11_app_event(SAPP_EVENTTYPE_QUIT_REQUESTED);
            // if user code hasn't intervened, quit the app
            if (_sapp.quit_requested) {
                _sapp.quit_ordered = true;
            }
        }
    }
    _sapp_call_cleanup();
    _sapp_glx_destroy_context();
    _sapp_x11_destroy_window();
    _sapp_x11_destroy_cursors();
    XCloseDisplay(_sapp.x11.display);
    _sapp_discard_state();
    */
    while(!fb_should_quit())
    {
        _sapp_frame();
        //_sapp_glx_swap_buffers()
   		printf("frame %d\n", _sapp.frame_count);
    }
}


#include "gfx.h" //just prototypes and defines (since no COMMON_IMPL defined)

fb_handle_t fb;
uint32_t rgba8_buffer[GFX_MAX_FB_WIDTH * GFX_MAX_FB_HEIGHT];
gfx_desc_t gfx_desc;

void gfx_init(const gfx_desc_t* desc) {
	gfx_desc = *desc;
    //printf("border_top %d, border_bottom %d, border_left %d, border_right %d, rot90 %d\n", desc->border_top, desc->border_bottom, desc->border_left, desc->border_right, desc->rot90);
	fb_init(GFX_MAX_FB_WIDTH, GFX_MAX_FB_HEIGHT, true, &fb);
	signal(SIGINT, SIG_DFL); //allows to exit by ctrl-c
}

//bool fb_should_quit(void);  

void gfx_shutdown() {
	fb_deinit(&fb);
}

uint32_t* gfx_framebuffer(void) {
    return rgba8_buffer;
}

size_t gfx_framebuffer_size(void) {
    return sizeof(rgba8_buffer);
}

uint64_t stm_now(void);

void gfx_draw(int emu_width, int emu_height) {
	//static int frame = 0;
	//printf("draw emu window %dx%d, time %d, frame/60 %d\n", emu_width, emu_height, stm_now()/1000000000, ++frame/60);
	if(gfx_desc.rot90)
	{
		static uint32_t buffer2[GFX_MAX_FB_WIDTH * GFX_MAX_FB_HEIGHT];
		const uint32_t *p = rgba8_buffer;
		for(int x = emu_width-1; x >= 0; --x)
		{
			for(int y = 0; y < emu_height; ++y)
				buffer2[x+y*GFX_MAX_FB_WIDTH] = *p++;
		}
		fb_update(&fb, buffer2, GFX_MAX_FB_WIDTH*+sizeof(buffer2[0]));
	}
	else
	{
		//show something that may not be right
		fb_update(&fb, rgba8_buffer, GFX_MAX_FB_WIDTH);
	}
}

SOKOL_APP_API_DECL double sapp_frame_duration(void)
{
 //measure and return averaged time
 return 1./60;
}

#define COMMON_IMPL
#include "prof.h" //all portable
#include "clock.h" //all portable


#endif
