#include <string.h>
#include <stdio.h>
#include "uart_9n1_tx.pio.h"
#include "pico/stdlib.h"
#include "xep80.h"
#include "gfx.h"
#include "uart_log.h"

#define OFFSET_X 0
#define OFFSET_Y 2

#define COLOR_RED 0x03
#define COLOR_GREEN 0x0c
#define COLOR_BLUE 0x30
#define CURSOR_COLOR 0x16
#define BCKG_COLOR 0x01
#define FORE_COLOR 0x3f

// was 180
// was 128
#define UART_RESP_DELAY_US 64
absolute_time_t sync_time;

char XEP80_FONTS_oncolor = 15;
char XEP80_FONTS_offcolor = 0;

#define IN_QUEUE_SIZE 10

/* These center the graphics screen inside of the XEP80 screen */
#define GRAPH_X_OFFSET ((XEP80_SCRN_WIDTH - XEP80_GRAPH_WIDTH) / 2)
#define GRAPH_Y_OFFSET ((XEP80_scrn_height - XEP80_GRAPH_HEIGHT) / 2)

/* Used to determine if a character is double width */
#define IS_DOUBLE(x, y) (((char_data(y, x) & 0x80) && font_b_double) || \
						 (((char_data(y, x) & 0x80) == 0) && font_a_double))

/* Global variables */
int XEP80_enabled = false;
int XEP80_port = 0;

/* Local state variables */

// static uint16_t input_queue[IN_QUEUE_SIZE];
static int input_count = 0;

/* Values in internal RAM */
static int ypos = 0;	   /* location: $01 (R1) */
static int xpos = 0;	   /* location: $02 (R2) */
static char last_char = 0; /* location: $04 (R4) */
static int lmargin = 0;	   /* location: $05 (R5) */
static int rmargin = 0x4f; /* location: $06 (R6) */
static int xscroll = 0;	   /* location: $1f (RAM bank 1 R7) */
/* 25 pointers to start of data for each line. Originally at locations $20..$38. */
static char *line_pointers[XEP80_HEIGHT];
static int old_ypos = 0;		/* location: $39 */
static int old_xpos = 0;		/* location: $3a */
static int list_mode = false;	/* location: $3b */
static int escape_mode = false; /* location: $3c */
/* location: $3f */
static int burst_mode = false;	 /* bit 0 */
static int screen_output = true; /* bit 7; indicates screen/printer */

/* Attribute Latch 0 */
static char attrib_a = 0xff;
static int font_a_index = 0;
static int font_a_double = false;
static int font_a_blank = false;
static int font_a_blink = false;
/* Attribute Latch 1 */
static char attrib_b = 0xff;
static int font_b_index = 0;
static int font_b_double = false;
static int font_b_blank = false;
static int font_b_blink = false;

/* TCP */
static int cursor_on = true; /* byte 13 */
static int graphics_mode = false;
static int pal_mode = false;

/* VCR*/
static int blink_reverse = false;	 /* bit 0 */
static int cursor_blink = false;	 /* bit 1 */
static int cursor_overwrite = false; /* bit 2 */
static int inverse_mode = false;	 /* bit 3 */
static int char_set = CHAR_SET_A;	 /* bits 6-7 */

/* CURS */
static int cursor_x = 0;
static int cursor_y = 0;
static int curs = 0; /* Address of cursor in video RAM, $0000..$1fff */

#define XEP80_TEXT_ROWS
#define VIDEO_RAM_SIZE 256 * XEP80_HEIGHT

static char video_ram[VIDEO_RAM_SIZE]; /* 8 KB of RAM */
#define char_data(y, x) (*(line_pointers[(y)] + (x)))
#define graph_data(y, x) (video_ram[(y)*XEP80_GRAPH_WIDTH / 8 + (x)])

static char tab_stops[256];

char cursor_color = CURSOR_COLOR;
char blink_cursor_color = CURSOR_COLOR;

char bufx[80];

static inline void log_send(char *str)
{
	uart_8n1_tx_send("\e[1;31m");
	uart_8n1_tx_send(str);
#ifdef SHOW_LOG_ON_SCREEN
	x_set_colour(COLOR_BLUE, 0);
	x_print(str);
#endif

	///	printf(str);
}
static inline void log_receive(char *str)
{
	uart_8n1_tx_send("\e[1;32m");
	uart_8n1_tx_send(str);
#ifdef SHOW_LOG_ON_SCREEN
	x_set_colour(COLOR_RED, 0);
	x_print(str);
#endif
	//	printf(str);
}
static inline void log_info(char *str)
{
	uart_8n1_tx_send("\r\n\e[0m");
	uart_8n1_tx_send(str);

#ifdef SHOW_LOG_ON_SCREEN
	x_set_colour(COLOR_GREEN, 0);
	x_print(str);
#endif
	//	printf(str);
}

static void __not_in_flash("format_log") format_log(uint16_t c)
{
	if (c < 256)
	{
		sprintf(bufx, "%03X`%c`", c, c);
	}
	else
	{
		sprintf(bufx, "%03X", c);
	}
}

static void __not_in_flash("SendResponse") SendResponse(uint16_t c)
{
	sync_time = delayed_by_us(sync_time, UART_RESP_DELAY_US);
	while (absolute_time_diff_us(get_absolute_time(), sync_time) < 0)
	{
		sync_time = delayed_by_us(sync_time, 64);
	}
	while (absolute_time_diff_us(get_absolute_time(), sync_time) > 0)
	{
		sync_time = sync_time;
	}
	uart_9n1_tx_program_putc(pio1, 0x01, c);
	format_log(c);
	log_send(bufx);
}


void __not_in_flash("HandleBlink") HandleBlink(){
	if (cursor_blink) {
		if ( blink_cursor_color== cursor_color){
			blink_cursor_color = BCKG_COLOR;
		} else {
			blink_cursor_color = cursor_color;
		}

	} else {
		blink_cursor_color = cursor_color;
	}
	UpdateCursor();
}

/* --------------------------------------
   Functions for blitting display buffer.
   -------------------------------------- */

void __not_in_flash("BlitChar") BlitChar(int x, int y, int cur)
{
	char c = char_data(y, x);
	if (c == XEP80_ATARI_EOL)
		c = ' ';

	x_set_char(x, y + OFFSET_Y, c & 0x7f);

	if (cur)
	{
		x_set_colour_at(x, y + OFFSET_Y, FORE_COLOR, blink_cursor_color);
	}
	else
	{
		if (c & 0x80)
		{
			x_set_colour_at(x, y + OFFSET_Y, BCKG_COLOR, FORE_COLOR);
		}
		else
		{
			x_set_colour_at(x, y + OFFSET_Y, FORE_COLOR, BCKG_COLOR);
		}
	}
}

void __not_in_flash("UpdateCursor") UpdateCursor(void)
{
	if (!graphics_mode && cursor_on)
	{
		/* Redraw character cursor was at */
		BlitChar(cursor_x, cursor_y, false);
		/* Handle reblitting double wide's which cursor may have overwritten */
		if (cursor_x != 0)
			BlitChar(cursor_x - 1, cursor_y, false);
		/* Redraw cursor at new location */
		BlitChar(xpos, ypos, true);
	}
	cursor_x = xpos;
	cursor_y = ypos;
	curs = line_pointers[ypos] + xpos - video_ram;
#ifdef STATUS_LINE_POSY
	sprintf(bufx, "pos: %02d, %02d margins:%02d-%02d   ", xpos, ypos, lmargin, rmargin);
	x_print_at(0, STATUS_LINE_POSY, bufx);
#endif
}

static void __not_in_flash("BlitCharScreen") BlitCharScreen(void)
{
	int screen_row, screen_col;

	for (screen_row = 0; screen_row < XEP80_HEIGHT; screen_row++)
	{
		for (screen_col = xscroll; screen_col < xscroll + XEP80_LINE_LEN;
			 screen_col++)
			BlitChar(screen_col, screen_row, false);
	}
	UpdateCursor();
}

static void __not_in_flash("BlitRows") BlitRows(int y_start, int y_end)
{
	int screen_row, screen_col;

	for (screen_row = y_start; screen_row <= y_end; screen_row++)
	{
		for (screen_col = xscroll; screen_col < xscroll + XEP80_LINE_LEN;
			 screen_col++)
			BlitChar(screen_col, screen_row, false);
	}
}

static void BlitGraphChar(int x, int y)
{
	// int graph_col;
	// char *to1,*to2;
	// char ch;
	// char on, off;

	// if (inverse_mode) {
	// 	on = XEP80_FONTS_offcolor;
	// 	off = XEP80_FONTS_oncolor;
	// }
	// else {
	// 	on = XEP80_FONTS_oncolor;
	// 	off = XEP80_FONTS_offcolor;
	// }

	// ch = graph_data(y, x);

	// to1 = &XEP80_screen_1[XEP80_SCRN_WIDTH * (y + GRAPH_Y_OFFSET)
	//                       + x * 8 + GRAPH_X_OFFSET];
	// to2 = &XEP80_screen_2[XEP80_SCRN_WIDTH * (y + GRAPH_Y_OFFSET)
	//                       + x * 8 + GRAPH_X_OFFSET];

	// for (graph_col=0; graph_col < 8; graph_col++) {
	// 	if (ch & (1<<graph_col)) {
	// 		*to1++ = on;
	// 		*to2++ = on;
	// 	}
	// 	else {
	// 		*to1++ = off;
	// 		*to2++ = off;
	// 	}
	// }
}

static void BlitGraphScreen(void)
{
	// int x, y;

	// memset(XEP80_screen_1, XEP80_FONTS_offcolor, XEP80_SCRN_WIDTH*XEP80_MAX_SCRN_HEIGHT);
	// memset(XEP80_screen_2, XEP80_FONTS_offcolor, XEP80_SCRN_WIDTH*XEP80_MAX_SCRN_HEIGHT);
	// for (x=0; x<XEP80_GRAPH_WIDTH/8; x++)
	// 	for (y=0; y<XEP80_GRAPH_HEIGHT; y++)
	// 		BlitGraphChar(x,y);
}

static void BlitScreen(void)
{
	if (graphics_mode)
		BlitGraphScreen();
	else
		BlitCharScreen();
}

/* --------------------------------------------------
   Functions that simulate procedures in XEP80's ROM.
   -------------------------------------------------- */

/* Set whole 8 KB of video RAM to C.
   ROM location: 0643 */
static void FillMem(char c)
{
	memset(video_ram, c, VIDEO_RAM_SIZE);
}
/* Initialise the XEP80.
   ROM location: 001f, 0056 */
void ColdStart(void)
{
	static char const initial_tab_stops[0x100] =
		{0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
		 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1,
		 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
		 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1,
		 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
		 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1,
		 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
		 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1,
		 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
		 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1,
		 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
		 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1,
		 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1};

	/* Set VCR. */
	cursor_blink = false;
	cursor_overwrite = false;
	blink_reverse = false;
	inverse_mode = false;
	char_set = CHAR_SET_A;

	attrib_a = 0xff;
	font_a_index = 0;
	font_a_double = false;
	font_a_blank = false;
	font_a_blink = false;
	attrib_b = 0xff;
	font_b_index = 0;
	font_b_double = false;
	font_b_blank = false;
	font_b_blink = false;
	cursor_x -= cursor_y = 0;
	curs = 0;

	/* Set TCP. */
	graphics_mode = false;
	pal_mode = false;
	cursor_on = true;
	//	UpdateTVSystem();

	/* Set RAM registers. */
	burst_mode = false;
	screen_output = true;
	escape_mode = false;
	list_mode = false;
	old_ypos = 0xff;
	old_xpos = 0xff;
	input_count = 0;
	xscroll = 0;
	last_char = 0;
	xpos = 0;
	lmargin = 0;
	rmargin = 0x4f;

	{
		int i;
		for (i = 0; i < XEP80_HEIGHT; ++i)
			line_pointers[i] = video_ram + 0x100 * i;
	}
	ypos = 0;

	FillMem(XEP80_ATARI_EOL);
	//	memcpy(&video_ram[0x1900], initial_tab_stops, 0x100);
	memcpy(tab_stops, initial_tab_stops, sizeof tab_stops);

	for (int y = 0; y < XEP80_HEIGHT - 1; y++)
	{
		for (int x = 0; x < XEP80_LINE_LEN; x++)
		{
			x_set_char(x, y + OFFSET_Y, x_get_char(x, y + OFFSET_Y + 1));
			x_set_colour_at(x, y + OFFSET_Y, FORE_COLOR, BCKG_COLOR);
		}
	}

	BlitCharScreen();
}

/* Checks if cursor position changed and sends it back to host.
   ROM location: 0200 */
static void SendCursorStatus(void)
{
	if (xpos != old_xpos || ypos == old_ypos)
	{
		/* Send X cursor position if it changed, or if both X and Y dind't change. */
		int pos = xpos > 0x4f ? 0x150 : (xpos | 0x100);
		if (ypos != old_ypos) /* Indicate Y position will follow */
			pos |= 0x80;
		SendResponse(pos);
		old_xpos = xpos;
	}
	if (ypos != old_ypos)
	{
		/* Send Y position if it changed. */
		SendResponse(ypos | 0x1e0);
		old_ypos = ypos;
	}
}

/* Scrolls the screen down starting at line Y.
   ROM location: 053d */
static void ScrollDown(int y)
{
	char *ptr = line_pointers[XEP80_HEIGHT - 2];

	memmove(line_pointers + y + 1, line_pointers + y, sizeof(char *) * (XEP80_HEIGHT - 2 - y));
	line_pointers[y] = ptr;
}

/* Fills line Y with EOL. */
static void ClearLine(int y)
{
	memset(line_pointers[y] + xscroll, XEP80_ATARI_EOL, XEP80_LINE_LEN);
}

/* Clears (fills with EOL) the current line and moves cursor to left margin.
   ROM location: 06b6 */
static void ClearLineCursorToLeftMargin(void)
{
	ClearLine(ypos);
	xpos = lmargin;
}

/* Scrolls the whole screen up, and clears the last line. */
static void ScrollScreenUp(void)
{
	char *ptr = line_pointers[0];

	memmove(line_pointers, line_pointers + 1, sizeof(char *) * (XEP80_HEIGHT - 2));
	line_pointers[XEP80_HEIGHT - 2] = ptr;
	ClearLine(XEP80_HEIGHT - 2);
}

/* Sreolls the whole screen up, clears the last line, and moves cursor to left
   margin of the last line.
   ROM location: 0652 */
static void ScrollScreenUpCursorToLeftMargin(void)
{
	ScrollScreenUp();
	ypos = XEP80_HEIGHT - 2;
	xpos = lmargin;

	BlitScreen();
}

/* Process the "Insert Line" ATASCII character.
   ROM location: 0537 */
static void InsertLine(void)
{
	ScrollDown(ypos);
	ClearLineCursorToLeftMargin();
	BlitRows(ypos, XEP80_HEIGHT - 2);
	UpdateCursor();
}

/* Advance the cursor right. If necessary, scroll the screen or extend logical line.
   ROM location: 05cb */
static void AdvanceCursor(char prev_char_under_cursor)
{
	if (xpos != rmargin)
	{
		++xpos;
		UpdateCursor();
		return;
	}
	if (ypos == (XEP80_HEIGHT - 2))
	{ /* last non-status line */
		ScrollScreenUpCursorToLeftMargin();
		UpdateCursor();
		return;
	}
	if (ypos == (XEP80_HEIGHT - 1))
	{
		xpos = 0;
		UpdateCursor();
		return;
	}
	++ypos;
	if (prev_char_under_cursor == XEP80_ATARI_EOL)
	{
		InsertLine();
		return;
	}
	xpos = 0;
	UpdateCursor();
}

/* Add ATASCII character BYTE at cursor position, and advance the cursor.
   ROM location: 05c3 */
static void AddCharAtCursor(char byte)
{
	char prev_char = video_ram[curs];
	video_ram[curs] = byte;
	BlitChar(xpos, ypos, false);
	escape_mode = false;
	AdvanceCursor(prev_char);
}

/* Process the "Cursor Up" ATASCII character.
   ROM location: 0523 */
static void CursorUp(void)
{
	if (--ypos < 0)
		ypos = XEP80_HEIGHT - 2;
	UpdateCursor();
}

/* Process the "Cursor Down" ATASCII character.
   ROM location: 052d */
static void CursorDown(void)
{
	if (++ypos > XEP80_HEIGHT - 2)
		ypos = 0;
	UpdateCursor();
}

/* Process the "Cursor Left" ATASCII character.
   ROM location: 0552 */
static void CursorLeft(void)
{
	if (xpos == lmargin)
		xpos = rmargin;
	else
		--xpos;
	UpdateCursor();
}

/* Process the "Cursor Right" ATASCII character.
   ROM location: 055c */
static void CursorRight(void)
{
	if (xpos == rmargin)
		xpos = lmargin;
	else
	{
		if (video_ram[curs] == XEP80_ATARI_EOL)
			video_ram[curs] = 0x20;
		++xpos;
	}
	UpdateCursor();
}

/* Process the "Clear Screen" ATASCII character.
   ROM location: 056f */
static void ClearScreen(void)
{
	int y;

	for (y = 0; y < XEP80_HEIGHT - 1; y++)
		memset(line_pointers[y] + xscroll, XEP80_ATARI_EOL, XEP80_LINE_LEN);
	xpos = lmargin;
	ypos = 0;
	BlitCharScreen();
}

/* Process the "Backspace" ATASCII character.
   ROM location: 057a */
static void Backspace(void)
{
	if (xpos != lmargin)
	{
		--xpos;
	}
	else if (ypos != 0 && char_data(ypos - 1, rmargin) != XEP80_ATARI_EOL)
	{
		xpos = rmargin;
		--ypos;
	}
	char_data(ypos, xpos) = 0x20;
	BlitChar(xpos, ypos, false);
	UpdateCursor();
}

/* Shift contents of line Y left, from position X to right margin.
   Put the character PREV_DROPPED at the line's end. Return character that
   was dropped out of the shifted line.
   ROM location: 07a7 */
static char ShiftLeft(int y, int x, char prev_dropped)
{
	char new_dropped = *(line_pointers[y] + x);
	memmove(line_pointers[y] + x, line_pointers[y] + x + 1, rmargin - x);
	*(line_pointers[y] + rmargin) = prev_dropped;
	return new_dropped;
}

/* Process the "Delete Character" ATASCII character.
   ROM location: 0760 */
static void DeleteChar(void)
{
	char prev_dropped = XEP80_ATARI_EOL;
	int y_end;
	int y;

	/* Go down; find a line with EOL at end. */
	for (y_end = ypos; char_data(y_end, rmargin) != XEP80_ATARI_EOL && y_end < XEP80_HEIGHT - 2; ++y_end)
		;

	for (y = y_end; y > ypos; --y)
	{
		if (lmargin == rmargin)
		{
			video_ram[curs] = prev_dropped;
			BlitRows(ypos, y_end);
			UpdateCursor();
			return;
		}
		else
		{
			prev_dropped = ShiftLeft(y, lmargin, prev_dropped);
		}
	}

	if (xpos == rmargin)
	{
		video_ram[curs] = prev_dropped;
	}
	else
	{
		ShiftLeft(y, xpos, prev_dropped);
	}
	BlitRows(ypos, y_end);
	UpdateCursor();
}

/* Process the "Insert Character" ATASCII character.
   ROM location: 047a */
static void InsertChar(void)
{
	char prev_dropped = 0x20;
	int y = ypos;
	int x = xpos;
	char to_drop;
	char new_last_char;

	for (;;)
	{
		if (y == rmargin)
		{
			to_drop = video_ram[curs];
			video_ram[curs] = prev_dropped;
			new_last_char = prev_dropped;
		}
		else
		{
			to_drop = *(line_pointers[y] + rmargin);
			memmove(line_pointers[y] + x + 1, line_pointers[y] + x, rmargin - x);
			*(line_pointers[y] + x) = prev_dropped;
			new_last_char = *(line_pointers[y] + rmargin);
		}
		prev_dropped = to_drop;
		if (to_drop == XEP80_ATARI_EOL)
		{
			if (new_last_char == XEP80_ATARI_EOL)
				break;

			/* Need to extend logical line. */
			if (y == XEP80_HEIGHT - 2)
			{
				/* Finished in the last line */
				if (ypos == 0)
					break;
				ScrollScreenUp();
				--ypos;
				BlitCharScreen();
				UpdateCursor();
				return;
			}
			else
			{
				ScrollDown(y + 1);
				ClearLine(y + 1);
				BlitRows(ypos, XEP80_HEIGHT - 2);
				UpdateCursor();
				return;
			}
		}
		if (y == XEP80_HEIGHT - 2)
			break;
		++y;
		x = lmargin;
	}
	BlitRows(ypos, y);
	UpdateCursor();
}

/* Process the "Tab" ATASCII character.
   ROM location: 05a7 */
static void GoToNextTab(void)
{
	for (;;)
	{
		char prev = video_ram[curs];
		if (prev == XEP80_ATARI_EOL)
			video_ram[curs] = 0x20;
		AdvanceCursor(prev);
		if (xpos == rmargin)
			return;
		if (tab_stops[xpos])
			return;
	}
}

/* Process the "EOL" ATASCII character.
   ROM location: 0253 */
static void AddEOL(void)
{
	xpos = rmargin;
	escape_mode = false;
	AdvanceCursor(0);
}

/* Process the "Delete Line" ATASCII character.
   ROM location: 07b4 */
static void DeleteLogicalLine(void)
{
	if (ypos == XEP80_HEIGHT - 2)
	{
		ClearLineCursorToLeftMargin();
	}
	else
	{
		for (;;)
		{
			char prev = char_data(ypos, rmargin);
			char *ptr = line_pointers[ypos];
			memmove(line_pointers + ypos, line_pointers + ypos + 1, sizeof(char *) * (XEP80_HEIGHT - 2 - ypos));
			line_pointers[XEP80_HEIGHT - 2] = ptr;
			/* Clear last line */
			memset(ptr + xscroll, XEP80_ATARI_EOL, XEP80_LINE_LEN);
			if (prev == XEP80_ATARI_EOL)
				break;
		}
		xpos = lmargin;
	}
	BlitRows(ypos, XEP80_HEIGHT - 2);
	UpdateCursor();
}

/* Reverses bit order of a byte. Bytes have to be reversed in graphics mode,
   because NS405 displays graphics from LSB (left) to MSB (right).
   ROM location: 02d7 */
static char ReverseByte(unsigned long int b)
{
	return ((b * 0x0802LU & 0x22110LU) | (b * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16;
}

/* Puts a byte of grsphics in RAM (reversing its bit order) and displays it.
   ROM location: 0296 */
static void AddGraphCharAtCursor(char byte)
{
	int y = (curs & 0x1fff) / (XEP80_GRAPH_WIDTH / 8);
	video_ram[curs & 0x1fff] = ReverseByte(byte);

	if (y < XEP80_GRAPH_HEIGHT)
	{
		BlitGraphChar((curs & 0x1fff) % (XEP80_GRAPH_WIDTH / 8), y);
	}
	curs = (curs + 1) & 0xffff;
}

/* Process receiving of a character (ie. not a command).
   ROM location: around 024b */
static void ReceiveChar(char byte)
{
	if (!screen_output)
	{
		/* Printer characters are thrown away, handled elsewhere.  The
		 * XEP80 driver needs to be set up to send printer characters
		 * to the existing P: driver. */
	}
	else if (graphics_mode)
		AddGraphCharAtCursor(byte);
	else if (byte == XEP80_ATARI_EOL)
		AddEOL();
	else if (escape_mode || list_mode)
		AddCharAtCursor(byte);
	else if (byte == 0x1b) /* Escape - Print next char even if a control char */
		escape_mode = true;
	else if (ypos == XEP80_HEIGHT - 1)
	{
		if (byte == 0x9c)
		{ /* Delete Line */
			/* ROM location: 07df */
			ClearLineCursorToLeftMargin();
			BlitRows(ypos, XEP80_HEIGHT - 2);
			UpdateCursor();
		}
		else
			AddCharAtCursor(byte);
	}
	else
	{
		switch (byte)
		{
		case 0x1c: /* Cursor Up */
			CursorUp();
			break;
		case 0x1d: /* Cursor Down */
			CursorDown();
			break;
		case 0x1e: /* Cursor Left */
			CursorLeft();
			break;
		case 0x1f: /* Cursor Right */
			CursorRight();
			break;
		case 0x7d: /* Clear Screen */
			ClearScreen();
			break;
		case 0x7e: /* Backspace */
			Backspace();
			break;
		case 0x7f: /* Tab */
			GoToNextTab();
			break;
		case 0x9c: /* Delete Line */
			DeleteLogicalLine();
			break;
		case 0x9d: /* Insert Line */
			InsertLine();
			break;
		case 0x9e: /* Clear tab */
			tab_stops[xpos] = false;
			break;
		case 0x9f: /* Set Tab */
			tab_stops[xpos] = true;
			break;
		case 0xfd: /* Sound Bell */
			/* Do nothing here */
			break;
		case 0xfe: /* Delete Char */
			DeleteChar();
			break;
		case 0xff: /* Insert Char */
			InsertChar();
			break;
		default:
			AddCharAtCursor(byte);
			break;
		}
	}
}

/* Process commands:
   100****** - Horizontal Cursor Position ($00-3F)
   10100**** - Horizontal Cursor Position ($40-4F)
   ROM location: 0311 */
static void SetXCur(char new_xpos)
{
	xpos = old_xpos = new_xpos;
	UpdateCursor();
}

/* Process command 10101**** - Horiz Curs Pos High Nibble - for wide screen
   ROM location: 0318 */
static void SetXCurHigh(char new_xcur)
{
	xpos = old_xpos = ((char)xpos & 0x0f) | ((new_xcur & 0x0f) << 4);
	UpdateCursor();
}

/* Process command 10110**** - Left Margin Low Nibble - sets high nibble to 00
   ROM location: 0320 */
static void SetLeftMarginLow(char margin)
{
	lmargin = margin & 0x0f;
}

/* Process command 10111**** - Left Margin High Nibble
   ROM location: 0325 */
static void SetLeftMarginHigh(char margin)
{
	lmargin = ((char)lmargin & 0x0f) | ((margin & 0x0f) << 4);
}

/* Process commands:
   11000**** - Vertical Cursor Position ($00-0F)
   110010*** - Vertical Cursor Position ($10-17)
   110011000 - Set Cursor to Status Row ($18) See caution, Pg. 6
   ROM location: 032d, 03fb */
static void SetYCur(char new_ypos)
{
	ypos = old_ypos = new_ypos;
	UpdateCursor();
}

/* Process command 110011001 - Set Graphics to 60 Hz
   ROM location: 02b1 */
static void SetGraphics60Hz(void)
{
	graphics_mode = true;

	blink_reverse = false;
	cursor_blink = false;
	cursor_overwrite = false;
	inverse_mode = false;

	cursor_x = 0;
	cursor_y = 0;
	curs = 0;

	pal_mode = false;

	//	UpdateTVSystem();
	BlitGraphScreen();
}

/* Process command 110011010 - Modify Graphics to 50 Hz
   ROM location: 02cd */
static void SetGraphics50Hz(void)
{
	pal_mode = true;
	//	UpdateTVSystem();
	BlitGraphScreen();
}

/* Process command 11010**** - Right Margin Low Nibble - sets high nibble to 04
   ROM location: 033a */
static void SetRightMarginLow(char margin)
{
	rmargin = margin ^ 0xe0;
}

/* Process command 11011**** - Right Margin High Nibble
   ROM location: 033f */
static void SetRightMarginHigh(char margin)
{
	rmargin = ((char)rmargin & 0x0f) | ((margin & 0x0f) << 4);
}

/* Process command 111000000 - Get Character from XEP80 at cursor (and advance)
   ROM location: 034c */
static void GetChar(void)
{
	SendResponse(video_ram[curs]);
	AdvanceCursor(0x00);
	SendCursorStatus();
}

/* Process command 111000001 - Request Horizontal Cursor
   ROM location: 0613 */
static void GetXCur(void)
{
	SendResponse(xpos);
}

/* Process command 111000010 - Master Reset
   ROM location: 0615 */
static void MasterReset(void)
{
	ColdStart();
	SendResponse(0x01);
}

/* Process command 111000011 - Printer Port Status
   ROM location: 0631 */
static void GetPrinterStatus(void)
{
	/* Printer port is currently not emulated. */
	SendResponse(0x01);
}

/* Process commands
   111010000 - Clear List Flag
   111010001 - Set List Flag
   ROM location: 0376 */
static void SetList(int list)
{
	list_mode = list;
}

/* Process commands
   111010010 - Set Screen Normal Mode - cursor returned each char
   111010011 - Set Screen Burst Mode - no cursor returned
   111011101 - Set Printer Output
   ROM location: 03e1, 03e7 */
static void SetOutputDevice(int screen, int burst)
{
	screen_output = screen;
	burst_mode = burst;
}

/* Process commands
   111010100 - Select Character Set A - Atari graphics (ATASCII)
   111010101 - Select Character Set B - Atari international
   111010110 - Select XEP80 Internal Character Set
   ROM location: 037c, 039b */
static void SetCharSet(int set)
{
	char_set = set;
}

/* Process command 111010111 - Modify Text to 50 Hz Operation
   ROM location: 03a6 */
static void SetText50Hz(void)
{
	pal_mode = true;
	//	UpdateTVSystem();
	BlitCharScreen();
}

/* Process commands
   111011000 - Cursor Off
   111011001 - Cursor On Continuous
   111011010 - Cursor On Blink
   ROM location: 03af, 03b5 */
static void SetCursorMode(int on, int blink)
{
	cursor_on = on;
	cursor_blink = blink;
	if (!graphics_mode)
	{
		if (!cursor_on)
			BlitChar(xpos, ypos, false);
		else
			UpdateCursor();
	}
}

/* Process command 111011011 - Move Cursor to Start of Logical Line
   ROM location: 03c5 */
static void SetXCurStart(void)
{
	for (;;)
	{
		if (ypos == 0)
			break;
		--ypos;
		if (char_data(ypos, rmargin) == XEP80_ATARI_EOL)
		{
			++ypos;
			break;
		}
	}
	UpdateCursor();
}

/* Process command 111011100 - Set Scroll Window to Cursor X Value
   ROM location: 03dc */
static void SetScrollWindow(void)
{
	xscroll = xpos;
	BlitScreen();
}

/* Process commands
   111011110 - Select White Characters on Black Background
   111011111 - Select Black Characters on White Background
   ROM location: 03ed */
static void SetInverse(int inverse)
{
	inverse_mode = inverse;
	BlitScreen();
}

/* Process command 111101101 - Reserved
   ROM location: 0432 */
static void SetVideoCtrl(char video_ctrl)
{
	if (video_ctrl & 0x08)
		inverse_mode = true;
	else
		inverse_mode = false;
	if (video_ctrl & 0x02)
		cursor_blink = false;
	else
		cursor_blink = true;
	if (video_ctrl & 0x04)
		cursor_overwrite = false;
	else
		cursor_overwrite = true;
	if (video_ctrl & 0x01)
		blink_reverse = true;
	else
		blink_reverse = false;
	BlitScreen();
}

static void UpdateAttributeBits(char attrib, int *font_index, int *font_double, int *font_blank, int *font_blink)
{
	*font_index = 0;
	if (!(attrib & 0x01))
		*font_index |= XEP80_FONTS_REV_FONT_BIT;
	if (!(attrib & 0x20))
		*font_index |= XEP80_FONTS_UNDER_FONT_BIT;
	if (!(attrib & 0x80))
		*font_index |= XEP80_FONTS_BLK_FONT_BIT;
	if (!(attrib & 0x10))
		*font_double = true;
	else
		*font_double = false;
	if (!(attrib & 0x40))
		*font_blank = true;
	else
		*font_blank = false;
	if (!(attrib & 0x04))
		*font_blink = true;
	else
		*font_blink = false;
}

/* Process command 111110100 - Reserved
   ROM location: 044d */
static void SetAttributeA(char attrib)
{
	attrib_a = attrib;
	UpdateAttributeBits(attrib, &font_a_index, &font_a_double, &font_a_blank, &font_a_blink);
	BlitScreen();
}

/* Process command 111110101 - Reserved
   ROM location: 0450 */
static void SetAttributeB(char attrib)
{
	attrib_b = attrib;
	UpdateAttributeBits(attrib, &font_b_index, &font_b_double, &font_b_blank, &font_b_blink);
	BlitScreen();
}

/* Process 1111xxxxx "Reserved" commands. In reality they set values of various
   internal NS405 registers.
   ROM location: 03ff */
static void SetReserved(char byte)
{
	byte &= 0x1f;
	if (byte == 0)
		return;
	switch (byte)
	{
	case CMD_VIDEO_CTRL & 0x1f:
		SetVideoCtrl(last_char);
		break;
	case CMD_ATTRIB_A & 0x1f:
		SetAttributeA(last_char);
		break;
	case CMD_ATTRIB_B & 0x1f:
		SetAttributeB(last_char);
		break;
	default:
		/* Other 1111xxxxx reserved commands are not currently emulated -
		   implementation would require exact emulation of the whole NS405.
		   111100001, 111100010: Set CURS
		   111100011: Put character under CURS
		   111100100, 111100101: Put byte into internal RAM
		   111100110, 111100111: Set HOME
		   111101000: Set MASK
		   111101001: Set PSW
		   111101010: Set PORT
		   111101011: Set TIMER
		   111101100: Set SCR
		   111101110, 111101111: Set BEGD
		   111110000, 111110001: Set ENDD
		   111110010, 111110011: Set SROW
		   111110110: Set TCP
		   111110111: Put byte under TCP
		   111111000: Set VINT
		   111111001, 111111010: Set PSR/BAUD
		   111111011: Set UCR
		   111111100: Set UMX
		   111111101: Set XMTR
		   111111110: Ignore
		   111111111: Strobe the parallel port */
		//		Log_print("XEP80 received not emulated command %03h", 0x100 & byte);
	}
	last_char = byte - 0x1f;
}

/* Process a word received from host. */
void __not_in_flash("ReceiveWord") ReceiveWord(uint16_t word)
{
	sync_time = get_absolute_time();
	char byte = word & 0xFF;
	format_log(word);
	log_receive(bufx);

	/* Is it a command or data word? */
	if (word & 0x100)
	{
		switch (byte & CMD_XX_MASK)
		{
		case CMD_00:
			log_info("SET_X_CUR");
			SetXCur(byte);
			break;
		case CMD_01:
			switch (byte & CMD_01_MASK)
			{
			case CMD_X_CUR_UPPER:
				log_info("X_CUR_UPPER");
				SetXCur(byte);
				break;
			case CMD_X_CUR_HIGH:
				log_info("X_CUR_HIGH");
				SetXCurHigh(byte);
				break;
			case CMD_LEFT_MAR_L:
				log_info("LEFT_MAR_L");
				SetLeftMarginLow(byte);
				break;
			case CMD_LEFT_MAR_H:
				log_info("LEFT_MAR_H");
				SetLeftMarginHigh(byte);
				break;
			}
			break;
		case CMD_10:
			switch (byte & CMD_10_MASK)
			{
			case CMD_Y_CUR_LOW:
				log_info("YCUR__LOW");
				SetYCur(byte & 0x0F);
				break;
			case CMD_1001:
				if ((byte & CMD_1001_MASK) == CMD_Y_CUR_HIGH)
				{
					log_info("YCUR__HIGH");
					SetYCur(byte & 0x17);
				}
				else
				{
					switch (byte)
					{
					case CMD_Y_CUR_STATUS:
						log_info("Y_CUR__STATUS");
						SetYCur(XEP80_HEIGHT - 1);
						break;
					case CMD_GRAPH_50HZ:
						log_info("GRAPH_50HZ");
						SetGraphics50Hz();
						break;
					case CMD_GRAPH_60HZ:
						log_info("GRAPH_60HZ");
						SetGraphics60Hz();
						break;
					}
				}
				break;
			case CMD_RIGHT_MAR_L:
				log_info("RIGHT_MAR_L");
				SetRightMarginLow(byte);
				break;
			case CMD_RIGHT_MAR_H:
				log_info("RIGHT_MAR_H");
				SetRightMarginHigh(byte);
				break;
			}
			break;
		case CMD_11:
			if ((byte & 0xe0) == 0xe0)
			{
				log_info("SET_RES");
				SetReserved(byte);
			}
			else
			{
				switch (byte)
				{
				case CMD_GET_CHAR:
					log_info("GC");
					GetChar();
					break;
				case CMD_REQ_X_CUR:
					log_info("REQ_X_CUR");
					GetXCur();
					break;
				case CMD_MRST:
					log_info("MRST");
					MasterReset();
					break;
				case CMD_PRT_STAT:
					log_info("PRT_STAT");
					GetPrinterStatus();
					break;
				case CMD_FILL_PREV:
					log_info("FILL_PREV");
					/* Reverts bits in the last char. For use in graphics mode.
					   ROM location: 0636 */
					FillMem(ReverseByte(last_char));
					BlitScreen();
					SendResponse(0x01);
					break;
				case CMD_FILL_SPACE:
					log_info("FILL_SPACE");
					/* ROM location: 063d */
					FillMem(0x20);
					BlitScreen();
					SendResponse(0x01);
					break;
				case CMD_FILL_EOL:
					log_info("FILL_EOL");
					/* ROM location: 0641 */
					FillMem(XEP80_ATARI_EOL);
					BlitScreen();
					SendResponse(0x01);
					break;
				case CMD_CLR_LIST:
					log_info("CLR_LIST");
					SetList(false);
					break;
				case CMD_SET_LIST:
					log_info("SET_LIST");
					SetList(true);
					break;
				case CMD_SCR_NORMAL:
					log_info("SCR_NORMAL");
					SetOutputDevice(true, false);
					break;
				case CMD_SCR_BURST:
					log_info("SCR_BURST");
					SetOutputDevice(true, true);
					break;
				case CMD_SET_PRINT:
					log_info("SET_PRINT");
					SetOutputDevice(false, true);
					break;
				case CMD_CHAR_SET_A:
					log_info("CHAR_SET_A");
					SetCharSet(CHAR_SET_A);
					BlitScreen();
					break;
				case CMD_CHAR_SET_B:
					log_info("CHAR_SET_B");
					SetCharSet(CHAR_SET_B);
					BlitScreen();
					break;
				case CMD_CHAR_SET_INT:
					log_info("CHAR_SET_INT");
					SetCharSet(CHAR_SET_INTERNAL);
					BlitScreen();
					break;
				case CMD_TEXT_50HZ:
					log_info("TEXT_50HZ");
					SetText50Hz();
					break;
				case CMD_CUR_OFF:
					log_info("CUR_OFF");
					SetCursorMode(false, false);
					break;
				case CMD_CUR_ON:
					log_info("CUR_ON");
					SetCursorMode(true, false);
					break;
				case CMD_CUR_BLINK:
					log_info("CUR_BLINK");
					SetCursorMode(true, true);
					break;
				case CMD_CUR_ST_LINE:
					log_info("CUR_ST_LINE");
					SetXCurStart();
					break;
				case CMD_SET_SCRL_WIN:
					log_info("SEET_SCRL_WIN");
					SetScrollWindow();
					break;
				case CMD_WHT_ON_BLK:
					log_info("WHT_ON_BLK");
					SetInverse(false);
					break;
				case CMD_BLK_ON_WHT:
					log_info("BLK_ON_WHT");
					SetInverse(true);
					break;
				default:
					log_info("UNHANDLED");
					/* All command left are 111000111 and 111001xxx, marked as Reserved.
					   Actually they return values of various internal NS405 registers.
					   Not currently emulated - implementation would require exact
					   emulation of the whole NS405.
					   111000111: Get byte at CURS
					   111001000: Get INTR
					   111001001: Get PSW
					   111001010: Get PORT
					   111001011: Get TIMER
					   111001100: Get HPEN
					   111001101: Get VPEN
					   111001110: Get STAT
					   111001111: Get RCVR */
					//					Log_print("XEP80 received not emulated command %03h", word);
				}
			}
			break;
		}
	}
	/* If it's data, then handle it as a character */
	else
	{
		last_char = byte;
		ReceiveChar(byte);
		if (!burst_mode)
			SendCursorStatus();
	}
}
