#ifndef _GFX_H_
#define _GFX_H_


#include <stdlib.h>
#include "pico/stdlib.h"

// TODO should put this in scratch_x, it out to fit...
#include "font/font_8x8.h"
#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 8
#define FONT_N_CHARS 128
#define FONT_FIRST_ASCII 0

// Pick one:
#define MODE_640x480_60Hz
//#define MODE_800x600_60Hz
// #define MODE_960x540p_60Hz
//  #define MODE_1280x720_30Hz

#if defined(MODE_640x480_60Hz)
// DVDD 1.2V (1.1V seems ok too)
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

#elif defined(MODE_800x600_60Hz)
// DVDD 1.3V, going downhill with a tailwind
#define FRAME_WIDTH 800
#define FRAME_HEIGHT 600
#define VREG_VSEL VREG_VOLTAGE_1_30
#define DVI_TIMING dvi_timing_800x600p_60hz

#elif defined(MODE_960x540p_60Hz)
// DVDD 1.25V (slower silicon may need the full 1.3, or just not work)
#define FRAME_WIDTH 960
#define FRAME_HEIGHT 540
#define VREG_VSEL VREG_VOLTAGE_1_25
#define DVI_TIMING dvi_timing_960x540p_60hz

#elif defined(MODE_1280x720_30Hz)
// 1280x720p 30 Hz (nonstandard)
// DVDD 1.25V (slower silicon may need the full 1.3, or just not work)
#define FRAME_WIDTH 1280
#define FRAME_HEIGHT 720
#define VREG_VSEL VREG_VOLTAGE_1_25
#define DVI_TIMING dvi_timing_1280x720p_30hz

#else
#error "Select a video mode!"
#endif


#define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)
#define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT)

#define COLOUR_PLANE_SIZE_WORDS (CHAR_ROWS * CHAR_COLS * 4 / 32)

struct gfx
{
    uint8_t posx;
    uint8_t posy;
    uint8_t fore_col;
    uint8_t bkg_col;
    uint8_t line_start;
    uint8_t line_last;
};




extern char charbuf[];
extern uint32_t colourbuf[];

extern void x_set_char(uint x, uint y, char c);
extern char x_get_char(uint x, uint y);
// Pixel format RGB222
extern void x_set_pos(uint x, uint y);
extern void x_set_colour(uint8_t fg, uint8_t bg);
extern void x_print(char *str);
extern void x_print_at(uint x, uint y,char *str);
extern void x_set_colour_at(uint x, uint y,uint8_t fg, uint8_t bg);
extern void x_clear(uint x, uint y, uint w, uint h);

#endif