#ifndef _XEP80_H
#define _XEP80_H

#include <stdint.h>

#define CMD_XX_MASK 0xC0
#define CMD_00 0x00
#define CMD_X_CUR_LOW 0x00
#define CMD_01 0x40
#define CMD_01_MASK 0x70
#define CMD_X_CUR_UPPER 0x40
#define CMD_X_CUR_HIGH 0x50
#define CMD_LEFT_MAR_L 0x60
#define CMD_LEFT_MAR_H 0x70
#define CMD_10 0x80
#define CMD_10_MASK 0xF0
#define CMD_Y_CUR_LOW 0x80
#define CMD_1001 0x90
#define CMD_1001_MASK 0xF8
#define CMD_Y_CUR_HIGH 0x90
#define CMD_Y_CUR_STATUS 0x98
#define CMD_GRAPH_60HZ 0x99
#define CMD_GRAPH_50HZ 0x9A
#define CMD_RIGHT_MAR_L 0xA0
#define CMD_RIGHT_MAR_H 0xB0
#define CMD_11 0xC0
#define CMD_GET_CHAR 0xC0
#define CMD_REQ_X_CUR 0xC1
#define CMD_MRST 0xC2
#define CMD_PRT_STAT 0xC3
#define CMD_FILL_PREV 0xC4
#define CMD_FILL_SPACE 0xC5
#define CMD_FILL_EOL 0xC6
#define CMD_CLR_LIST 0xD0
#define CMD_SET_LIST 0xD1
#define CMD_SCR_NORMAL 0xD2
#define CMD_SCR_BURST 0xD3
#define CMD_CHAR_SET_A 0xD4
#define CMD_CHAR_SET_B 0xD5
#define CMD_CHAR_SET_INT 0xD6
#define CMD_TEXT_50HZ 0xD7
#define CMD_CUR_OFF 0xD8
#define CMD_CUR_ON 0xD9
#define CMD_CUR_BLINK 0xDA
#define CMD_CUR_ST_LINE 0xDB
#define CMD_SET_SCRL_WIN 0xDC
#define CMD_SET_PRINT 0xDD
#define CMD_WHT_ON_BLK 0xDE
#define CMD_BLK_ON_WHT 0xDF
#define CMD_VIDEO_CTRL 0xED
#define CMD_ATTRIB_A 0xF4
#define CMD_ATTRIB_B 0xF5

#define CHAR_SET_A 0
#define CHAR_SET_B 1
#define CHAR_SET_INTERNAL 2

#define XEP80_WIDTH 256
#define XEP80_HEIGHT 25
#define XEP80_CHAR_WIDTH 7
#define XEP80_MAX_CHAR_HEIGHT 12
#define XEP80_GRAPH_WIDTH 320
#define XEP80_GRAPH_HEIGHT 200
#define XEP80_LINE_LEN 80
#define XEP80_SCRN_WIDTH (XEP80_LINE_LEN * XEP80_CHAR_WIDTH)
#define XEP80_MAX_SCRN_HEIGHT (XEP80_HEIGHT * XEP80_MAX_CHAR_HEIGHT)

#define XEP80_ATARI_EOL 0x9b
#define XEP80_FONTS_REV_FONT_BIT 0x1
#define XEP80_FONTS_UNDER_FONT_BIT 0x2
#define XEP80_FONTS_BLK_FONT_BIT 0x4

#define XEP80_FONTS_UNDER_ROW 9

#define CHAR_VERT_FACTOR 2
#define STATUS_LINE_POSY 29


void ReceiveWord(uint16_t word);
void ColdStart(void);
void HandleBlink(void);
#endif