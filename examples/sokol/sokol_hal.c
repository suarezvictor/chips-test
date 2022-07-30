#ifdef USE_SOKOL_DIRECT
#define SOKOL_IMPL
#define SOKOL_GLCORE33
#include "sokol_app.h"
/*
REQUIRED BY pengo.c (provided by sokol_app.h):
sapp_widthf, sapp_heightf
*/

#include "../../../sokol/tests/compile/sokol_audio.c"
/*
REQUIRED BY pengo.c (provided by sokol_audio.c):
saudio_suspended, saudio_setup, saudio_shutdown, saudio_sample_rate, saudio_push
*/

#include "../../../sokol/tests/compile/sokol_time.c"
/*
REQUIRED BY pengo.c (provided by sokol_time.c): stm_since, stm_ms
*/
#else //not USE_SOKOL_DIRECT

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h> //memset
#include <signal.h>
#include "sdl_fb.h"

#include "sokol_app.h" //no implementation requested

#ifndef SOKOL_API_IMPL
#define SOKOL_API_IMPL
#endif
#ifndef SOKOL_ASSERT
#define SOKOL_ASSERT(x)
#endif

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
typedef struct {} saudio_desc;

SOKOL_API_IMPL void saudio_setup(const saudio_desc* desc) {
}

SOKOL_API_IMPL void saudio_shutdown(void) {
}

SOKOL_API_IMPL int saudio_sample_rate(void) {
  return 44100;
}

SOKOL_API_IMPL int saudio_push(const float* frames, int num_frames) {
  return num_frames;
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
	static int frame = 0;
	//printf("draw emu window %dx%d, time %d, frame/60 %d\n", emu_width, emu_height, stm_now()/1000000000, ++frame/60);
	//memset(rgba8_buffer, frame, sizeof(rgba8_buffer));
	if(gfx_desc.rot90)
	{
		//memset(rgba8_buffer+GFX_MAX_FB_WIDTH*100, 0xFF, GFX_MAX_FB_WIDTH*100);
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
