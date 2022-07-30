#ifdef USE_SOKOL_DIRECT

#define SOKOL_GLCORE33
#define COMMON_IMPL
#include <string.h> //memset
#include "common.h" //gfx_init, clock_init, prof_init, gfx_framebuffer, gfx_framebuffer_size, clock_frame_time, prof_push, prof_stats
/*
REQUIRED BY pengo.c (provided by common.h in examples):
  prof_init, clock_init
  clock_frame_time
  gfx_framebuffer, gfx_framebuffer_size
  prof_push, prof_stats
  gfx_init, gfx_shutdown
  gfx_draw
*/

#if 1 // ONLY FOR STATUS BAR
#include "../../../sokol/tests/compile/sokol_glue.c" //sapp_sgcontext
#include "../../../sokol/tests/compile/sokol_fetch.c" //sfetch_setup, sfetch_send, sfetch_dowork
#include "../../../sokol/sokol_gfx.h" //sg_destroy_pipeline, etc.
#include "../../../sokol/util/sokol_gl.h" //sgl_make_pipeline, etc
#include "../../../sokol/util/sokol_debugtext.h" //sdtx_color3b, sdtx_draw, sdtx_setup, sdtx_font_kc853, sdtx_font_z1013, sdtx_shutdown 
/*
REQUIRED BY pengo.c (provided by sokol_debugtext.h):
sdtx_canvas, sdtx_color3b, sdtx_pos, sdtx_printf
*/
#endif

#endif //USE_SOKOL_DIRECT
