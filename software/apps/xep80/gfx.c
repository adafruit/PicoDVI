/*


*/

#include "gfx.h"

char charbuf[CHAR_ROWS * CHAR_COLS];
uint32_t colourbuf[3 * COLOUR_PLANE_SIZE_WORDS];

struct gfx gfx_state = {0, 28, 255, 0, 28, 58};

void x_set_char(uint x, uint y, char c)
{
	if (x >= CHAR_COLS || y >= CHAR_ROWS)
		return;
	charbuf[x + y * CHAR_COLS] = c;
}
char x_get_char(uint x, uint y)
{
	if (x >= CHAR_COLS || y >= CHAR_ROWS)
		return 0;
	return charbuf[x + y * CHAR_COLS];
}

void x_set_pos(uint x, uint y)
{
	if (x >= CHAR_COLS || y >= CHAR_ROWS)
		return;
	gfx_state.posx = x;
	gfx_state.posy = y;
}

void x_set_colour(uint8_t fg, uint8_t bg)
{

	gfx_state.fore_col = fg;
	gfx_state.bkg_col = bg;
}

// Pixel format RGB222
void set_colour()
{
	x_set_colour_at(gfx_state.posx, gfx_state.posy, gfx_state.fore_col, gfx_state.bkg_col);
}

void x_set_colour_at(uint x, uint y, uint8_t fg, uint8_t bg)
{
	uint char_index = x + y * CHAR_COLS;
	uint bit_index = char_index % 8 * 4;
	uint word_index = char_index / 8;
	for (int plane = 0; plane < 3; ++plane)
	{
		uint32_t fg_bg_combined = (fg & 0x3) | (bg << 2 & 0xc);
		colourbuf[word_index] = (colourbuf[word_index] & ~(0xfu << bit_index)) | (fg_bg_combined << bit_index);
		fg >>= 2;
		bg >>= 2;
		word_index += COLOUR_PLANE_SIZE_WORDS;
	}
}

void x_print(char *str)
{
	while (*str != '\0')
	{
		set_colour();
		x_set_char(gfx_state.posx, gfx_state.posy, *str);
		str++;
		gfx_state.posx++;
		if (gfx_state.posx >= CHAR_COLS)
		{
			gfx_state.posx = 0;
			gfx_state.posy++;
		}
		if (gfx_state.posy >= gfx_state.line_last)
		{
			gfx_state.posy = gfx_state.line_start;
			x_clear(0, gfx_state.line_start, CHAR_COLS, gfx_state.line_last - gfx_state.line_start);
		}
	}
}

void x_print_at(uint x, uint y, char *str)
{
	while (*str != '\0')
	{
		x_set_char(x, y, *str);
		str++;
		x++;
	}
}

void x_clear(uint x, uint y, uint w, uint h)
{
	for (uint xx = 0; xx < w; xx++)
	{
		for (uint yy = 0; yy < h; yy++)
		{
			x_set_colour_at(x + xx, y + yy, 0xff, 0);
			x_set_char(x + xx, y + yy, ' ');
		}
	}
}