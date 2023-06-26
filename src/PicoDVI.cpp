// SPDX-FileCopyrightText: 2023 P Burgess for Adafruit Industries
//
// SPDX-License-Identifier: BSD-3-Clause

/*!
 * @file PicoDVI.cpp
 *
 * Arduino-and-Adafruit-GFX wrapper around Luke Wren's PicoDVI library.
 */

#include "PicoDVI.h"
#include "libdvi/tmds_encode.h"

// PicoDVI class encapsulates some of the libdvi functionality -------------
// Subclasses then implement specific display types.

static struct {
  const dvi_timing &timing;
  vreg_voltage v;
  uint16_t width;
  uint16_t height;
  uint8_t v_rep;
} dvispec[] = {
    {dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_20, 320, 240, 2},
    {dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_20, 400, 240, 2},
    {dvi_timing_800x480p_30hz, VREG_VOLTAGE_1_20, 400, 240, 2},
    {dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_20, 640, 480, 1},
    {dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_20, 800, 480, 1},
    {dvi_timing_800x480p_30hz, VREG_VOLTAGE_1_20, 800, 480, 1},
    {dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_20, 640, 240, 2},
    {dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_20, 800, 240, 2},
    {dvi_timing_800x480p_30hz, VREG_VOLTAGE_1_20, 800, 240, 2},
    // Additional resolutions might get added here if the overclock issue can
    // be sorted out. Regardless, always keep this list 1:1 in sync with the
    // DVIresolution enum in PicoDVI.h.
    {dvi_timing_1280x720p_30hz, VREG_VOLTAGE_1_30, 1280, 720, 1},
};

static PicoDVI *dviptr = NULL; // For C access to active C++ object

/*!
  @brief  Runs on core 1 on startup; this is how Philhower RP2040 handles
          multiprocessing.
*/
void setup1(void) {
  while (dviptr == NULL) // Wait for PicoDVI::begin() to start on core 0
    yield();
  dviptr->_setup();
}

// Runs on core 1 after dviptr set
void PicoDVI::_setup(void) {
  while (wait_begin)
    ; // Wait for DVIGFX*::begin() to set this
  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  dvi_start(&dvi0);

  // This is disabled for now, for a transitional period while folks
  // might be mixing in older versions of arduino-pico, Adafruit_CPFS
  // and/or Adafruit_SPIFlash. Once is seems pretty solid that everyone's
  // using current versions that correctly locate PicoDVI functions in
  // RAM, this can be activated. Not urgent but a nice-to-have.
#if 0
  // Borrowed from CircuitPython: turn off core 1 flash access. Any attempt
  // after this will hard fault (not good, but preferable to the alternative
  // which would corrupt the CIRCUITPY flash filesystem if mounted). Linker
  // file MUST place the following files in RAM or library won't work:
  // *interp.c.obj *divider.S.obj *PicoDVI.cpp.o *dvi.c.o
  // (Earle Philhower arduino-pico >= 3.3.0 does this now.)
  MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
  MPU->RNR  = 6;
  MPU->RBAR = XIP_MAIN_BASE | MPU_RBAR_VALID_Msk;
  MPU->RASR = MPU_RASR_XN_Msk | MPU_RASR_ENABLE_Msk |
              (0x1b << MPU_RASR_SIZE_Pos);
  MPU->RNR  = 7;
#endif

  (*mainloop)(&dvi0);
}

PicoDVI::PicoDVI(const struct dvi_timing &t, const struct dvi_serialiser_cfg &c,
                 vreg_voltage v)
    : voltage(v) {
  dvi0.timing = &t;
  memcpy(&dvi0.ser_cfg, &c, sizeof dvi0.ser_cfg);
};

PicoDVI::~PicoDVI(void) { dviptr = NULL; }

void PicoDVI::begin(void) {
  dviptr = this;
  vreg_set_voltage(voltage);
  delay(10);
  set_sys_clock_khz(dvi0.timing->bit_clk_khz, true); // Run at TMDS bit clock
  dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
}

// DVIGFX16 class provides GFX-compatible 16-bit color framebuffer ---------

static void *gfxptr = NULL; // For C access to active C++ object

DVIGFX16::DVIGFX16(const DVIresolution r, const struct dvi_serialiser_cfg &c,
                   vreg_voltage v)
    : PicoDVI(dvispec[r].timing, c, v),
      GFXcanvas16(dvispec[r].width, dvispec[r].height) {
  dvi_vertical_repeat = dvispec[r].v_rep;
  dvi_monochrome_tmds = false;
}

DVIGFX16::~DVIGFX16(void) { gfxptr = NULL; }

static void scanline_callback_GFX16(void) {
  ((DVIGFX16 *)gfxptr)->_scanline_callback();
}

void DVIGFX16::_scanline_callback(void) {
  // Discard any scanline pointers passed back
  uint16_t *bufptr;
  while (queue_try_remove_u32(&dvi0.q_colour_free, &bufptr))
    ;
  bufptr = &getBuffer()[WIDTH * scanline];
  queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
  scanline = (scanline + 1) % HEIGHT;
}

bool DVIGFX16::begin(void) {
  uint16_t *bufptr = getBuffer();
  if ((bufptr)) {
    gfxptr = this;
    mainloop = dvi_scanbuf_main_16bpp; // in libdvi
    dvi0.scanline_callback = scanline_callback_GFX16;
    PicoDVI::begin();
    queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
    bufptr += WIDTH;
    queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
    wait_begin = false; // Set core 1 in motion
    return true;
  }
  return false;
}

// DVIGFX8 (8-bit, color-indexed framebuffer) is all manner of dirty pool.
// PicoDVI seems to have some palette support but I couldn't grasp the DMA
// stuff going on, so just doing a brute force thing here for now: in
// addition to the 8-bit framebuffer, two 16-bit (RGB565) scanlines are
// allocated...then, in the scanline callback, pixels are mapped from the
// 8-bit framebuffer through the palette into one of these buffers, allowing
// use of the same dvi_scanbuf_main_16bpp handler as DVIGFX16 above. Not
// optimal, sure...but not pessimal either. The allocation of those 16-bit
// scanlines is weird(tm) though. Rather than a separate malloc (which
// creates a nasty can of worms if that fails after a successful framebuffer
// allocation...unlikely but not impossible), the framebuffer size is
// tweaked so that W*H is always an even number, plus 4 extra rows are
// added: thus two 16-bit scanlines, word-aligned. That extra memory is for
// us, but allocated by GFX as part of the framebuffer all at once. The
// HEIGHT value is de-tweaked to the original value so clipping won't allow
// any drawing operations to spill into the 16-bit scanlines.
// This requires latest Adafruit_GFX as double-buffered mode plays games with
// the canvas pointer, wasn't possible until that was made protected (vs
// private). Drawing and palette-setting operations ONLY apply to the "back"
// state. Call swap() to switch the front/back buffers at the next vertical
// sync, for flicker-free and tear-free animation.

DVIGFX8::DVIGFX8(const DVIresolution r, const bool d,
                 const struct dvi_serialiser_cfg &c, vreg_voltage v)
    : PicoDVI(dvispec[r].timing, c, v),
      GFXcanvas8(
          dvispec[r].width,
          (d ? (dvispec[r].height * 2) : ((dvispec[r].height + 1) & ~1)) + 4),
      dbuf(d) {
  HEIGHT = _height = dvispec[r].height;
  buffer_save = buffer;
  dvi_vertical_repeat = dvispec[r].v_rep;
  dvi_monochrome_tmds = false;
}

DVIGFX8::~DVIGFX8(void) {
  buffer = buffer_save; // Restore pointer so canvas destructor works
  gfxptr = NULL;
}

static void __not_in_flash_func(scanline_callback_GFX8)(void) {
  ((DVIGFX8 *)gfxptr)->_scanline_callback();
}

void __not_in_flash_func(DVIGFX8::_scanline_callback)(void) {
  uint16_t *b16;
  while (queue_try_remove_u32(&dvi0.q_colour_free, &b16))
    ;                   // Discard returned pointer(s)
  b16 = row565[rowidx]; // Next row to send
  queue_add_blocking_u32(&dvi0.q_colour_valid, &b16); // Send it

  if (++scanline >= HEIGHT) {      // Next scanline...end of screen reached?
    if (swap_wait) {               // Swap buffers?
      back_index = 1 - back_index; // Yes plz
      buffer = buffer_save + WIDTH * HEIGHT * back_index;
      swap_wait = 0;
    }
    scanline = 0;
  }

  // Refresh from front buffer
  uint8_t *b8 = buffer_save + WIDTH * scanline; // New src
  if (dbuf)
    b8 += WIDTH * HEIGHT * (1 - back_index);
  rowidx = (rowidx + 1) & 1; // Swap row565[] bufs
  b16 = row565[rowidx];      // New dest
  uint16_t *p16 = dbuf ? palette[1 - back_index] : palette[0];
  for (int i = 0; i < WIDTH; i++)
    b16[i] = p16[b8[i]];
}

bool DVIGFX8::begin(void) {
  uint8_t *bufptr = getBuffer();
  if ((bufptr)) {
    gfxptr = this;
    row565[0] = (uint16_t *)&bufptr[dbuf ? WIDTH * HEIGHT * 2
                                         : (WIDTH * HEIGHT + 1) & ~1];
    row565[1] = row565[0] + WIDTH;
    memset(palette, 0, sizeof palette);
    // mainloop = mainloop8;
    mainloop = dvi_scanbuf_main_16bpp; // in libdvi
    dvi0.scanline_callback = scanline_callback_GFX8;
    PicoDVI::begin();
    // No need to initialize the row565 buffer contents as that memory is
    // cleared on canvas alloc, and the initial palette state is also all 0.
    uint16_t *b16 = row565[0];
    queue_add_blocking_u32(&dvi0.q_colour_valid, &b16);
    b16 = row565[1];
    queue_add_blocking_u32(&dvi0.q_colour_valid, &b16);
    wait_begin = false; // Set core 1 in motion
    return true;
  }
  return false;
}

void DVIGFX8::swap(bool copy_framebuffer, bool copy_palette) {
  if (dbuf) {
    // Request buffer swap at next frame end, wait for it to happen.
    for (swap_wait = 1; swap_wait;)
      ;

    if ((copy_framebuffer)) {
      uint32_t bufsize = WIDTH * HEIGHT;
      memcpy(buffer_save + bufsize * back_index,
             buffer_save + bufsize * (1 - back_index), bufsize);
    }

    if ((copy_palette)) {
      memcpy(palette[back_index], palette[1 - back_index], sizeof(palette[0]));
    }
  }
}

DVIGFX1::DVIGFX1(const DVIresolution r, const bool d,
                 const struct dvi_serialiser_cfg &c, vreg_voltage v)
    : PicoDVI(dvispec[r].timing, c, v),
      GFXcanvas1(dvispec[r].width,
                 d ? (dvispec[r].height * 2) : dvispec[r].height),
      dbuf(d) {
  dvi_vertical_repeat = dvispec[r].v_rep;
  dvi_monochrome_tmds = true;
  HEIGHT = _height = dvispec[r].height;
  buffer_save = buffer;
}

DVIGFX1::~DVIGFX1(void) {
  buffer = buffer_save; // Restore pointer so canvas destructor works
  gfxptr = NULL;
}

static void mainloop1(struct dvi_inst *inst) {
  (void)inst; // Avoid compiler warning
  ((DVIGFX1 *)gfxptr)->_mainloop();
}

void __not_in_flash_func(DVIGFX1::_mainloop)(void) {
  for (;;) {
    uint8_t *b8 = buffer_save;
    if (dbuf)
      b8 += ((WIDTH + 7) / 8) * HEIGHT * (1 - back_index);
    for (int y = 0; y < HEIGHT; y++) {
      const uint32_t *colourbuf =
          (const uint32_t *)(b8 + y * ((WIDTH + 7) / 8));
      uint32_t *tmdsbuf;
      queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
      tmds_encode_1bpp(colourbuf, tmdsbuf, WIDTH);
      queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
    }
    if (swap_wait) {               // Swap buffers?
      back_index = 1 - back_index; // Yes plz
      buffer = buffer_save + ((WIDTH + 7) / 8) * HEIGHT * back_index;
      swap_wait = 0;
    }
  }
}

bool DVIGFX1::begin(void) {
  if ((getBuffer())) {
    gfxptr = this;
    mainloop = mainloop1;
    PicoDVI::begin();
    wait_begin = false; // Set core 1 in motion
    return true;
  }
  return false;
}

void DVIGFX1::swap(bool copy_framebuffer) {
  if (dbuf) {
    // Request buffer swap at next frame end, wait for it to happen.
    for (swap_wait = 1; swap_wait;)
      ;

    if ((copy_framebuffer)) {
      uint32_t bufsize = ((WIDTH + 7) / 8) * HEIGHT;
      memcpy(buffer_save + bufsize * back_index,
             buffer_save + bufsize * (1 - back_index), bufsize);
    }
  }
}

// TEXT MODE IS A WORK IN PROGRESS

#define FONT_CHAR_WIDTH 8  ///< Character block width, pixels
#define FONT_CHAR_HEIGHT 8 ///< Character block height, pixels
#define FONT_N_CHARS 256   ///< Number of symbols in font
#define FONT_FIRST_ASCII 0 ///< Full CP437 is there, start at 0
#include "font_8x8.h"

#define DEFPARAM(i, d) (esc_param[(i)] ?: (d))

DVItext1::vt_action DVItext1::csi_common(int c, int i) {
  if (c >= '0' && c <= '9') {
    esc_param[i] = esc_param[i] * 10 + c - '0';
    return DVItext1::vt_action::NO_OUTPUT;
  }
  esc_st = NORMAL;
  if (c == 'K') {
    return DVItext1::vt_action::CLEAR_EOL;
  } else if (c == 'J') {
    if (esc_param[0] == 2) {
      return DVItext1::vt_action::CLEAR_SCREEN;
    }
  } else if (c == 'D') {
    return DVItext1::vt_action::CURSOR_LEFT;
  } else if (c == 'H') {
    return DVItext1::vt_action::CURSOR_POSITION;
  } else if (c == 'm') {
    return DVItext1::vt_action::CHAR_ATTR;
  }
  return DVItext1::vt_action::NO_OUTPUT;
}

DVItext1::vt_action DVItext1::handle_escape_code(int c) {
  switch (esc_st) {
  default:
  case NORMAL:
    if (c == 27) {
      esc_st = ESC;
      return DVItext1::vt_action::NO_OUTPUT;
    }
    if (c == 7) {
      // bell
      return DVItext1::vt_action::BELL;
    }
    if (c == 8) {
      esc_param[0] = 1;
      return DVItext1::vt_action::CURSOR_LEFT;
    }
    return DVItext1::vt_action::PRINTABLE;

  case ESC:
    if (c == '[') {
      esc_st = CSI;
      esc_param[0] = esc_param[1] = 0;
    } else if (c == ']') {
      esc_st = TITLE_PRE;
    } else {
      esc_st = NORMAL;
    }
    return DVItext1::vt_action::NO_OUTPUT;

  case CSI:
    if (c == ';') {
      esc_st = P2;
      return DVItext1::vt_action::NO_OUTPUT;
    } else {
      return csi_common(c, 0);
    }

  case P2:
    return csi_common(c, 1);

  case TITLE_PRE:
    if (c == ';') {
      esc_st = TITLE;
      return DVItext1::vt_action::NO_OUTPUT;
    }

  case TITLE:
    if (c == 0x1b) {
      esc_st = TITLE_END;
    }
    return DVItext1::vt_action::NO_OUTPUT;

  case TITLE_END:
    if (c == 0x5c) {
      esc_st = NORMAL;
    } else {
      esc_st = TITLE;
    }
    return DVItext1::vt_action::NO_OUTPUT;
  }
}

DVItext1::DVItext1(const DVIresolution r, const struct dvi_serialiser_cfg &c,
                   vreg_voltage v)
    : PicoDVI(dvispec[r].timing, c, v),
      GFXcanvas16(dvispec[r].width / 8, dvispec[r].height / 8) {
  dvi_vertical_repeat = dvispec[r].v_rep;
  dvi_monochrome_tmds = true;
}

DVItext1::~DVItext1(void) { gfxptr = NULL; }

// Character framebuffer is actually a small GFXcanvas16, so...
size_t DVItext1::write(uint8_t c) {
  auto st = esc_st;

  auto action = handle_escape_code(c);

#if defined(DEBUG_ESCAPE_CODES)
  if (st != esc_st) {
    Serial.printf("st %d -> %d [c=%d]\r\n", st, esc_st, c);
  }

  if (action != DVItext1::vt_action::PRINTABLE &&
      action != DVItext1::vt_action::NO_OUTPUT) {
    Serial.printf("action %d param0 %d param1 %d\r\n", action, esc_param[0],
                  esc_param[1]);
  }
#endif

  switch (action) {
  case DVItext1::vt_action::PRINTABLE:
    if (c == '\r') { // Carriage return
      cursor_x = 0;
    } else if ((c == '\n') || (cursor_x >= WIDTH)) { // Newline OR right edge
      cursor_x = 0;
      if (cursor_y >= (HEIGHT - 1)) { // Vert scroll?
        memmove(getBuffer(), getBuffer() + WIDTH, WIDTH * (HEIGHT - 1) * 2);
        drawFastHLine(0, HEIGHT - 1, WIDTH, ' '); // Clear bottom line
        cursor_y = HEIGHT - 1;
      } else {
        cursor_y++;
      }
    }
    if ((c != '\r') && (c != '\n')) {
      drawPixel(cursor_x, cursor_y, c | attr);
      cursor_x++;
    }
    break;

  case DVItext1::vt_action::BELL: {
    auto b = reinterpret_cast<uint32_t *>(getBuffer());
    for (size_t i = 0; i < WIDTH * HEIGHT / 2; i++)
      b[i] ^= 0xff00ff00;
    delay(100);
    for (size_t i = 0; i < WIDTH * HEIGHT / 2; i++)
      b[i] ^= 0xff00ff00;
    break;
  }

  case DVItext1::vt_action::NO_OUTPUT:
    break;

  case DVItext1::vt_action::CLEAR_EOL:
    if (cursor_x < WIDTH) {
      drawFastHLine(cursor_x, cursor_y, WIDTH - cursor_x, ' ');
    }
    break;

  case DVItext1::vt_action::CLEAR_SCREEN:
    // does NOT reset cursor position!
    memset(getBuffer(), 0, WIDTH * HEIGHT * 2);
    break;

  case DVItext1::vt_action::CURSOR_LEFT:
    cursor_x = std::max(0, cursor_x - DEFPARAM(0, 1));
    break;

  case DVItext1::vt_action::CURSOR_POSITION:
    cursor_x = std::max(0, min(WIDTH - 1, DEFPARAM(1, 1) - 1));
    cursor_y = std::max(0, min(HEIGHT - 1, DEFPARAM(0, 1) - 1));

  case DVItext1::vt_action::CHAR_ATTR:
    attr = esc_param[0] ? 0xff00 : 0;
  }
  return 1;
}

// TO DO: alloc this dynamically as part of object (maybe part of canvas)
static uint8_t scanbuf[1280 / 8] __attribute__((aligned(4)));

#ifdef TERM_USE_INTERRUPT

void inline __not_in_flash_func(DVItext1::_prepare_scanline)(uint16_t y) {
  uint16_t *row = getBuffer() + (y / FONT_CHAR_HEIGHT) * WIDTH;
  uint32_t offset = (y & 7) * FONT_N_CHARS;

  // Blit font into 1bpp scanline buffer, then encode scanbuf into tmdsbuf
  for (uint16_t x = 0; x < WIDTH; x++) {
    // Though this only handles 8-bit character output (e.g. ASCII), the
    // character buffer uses 16-bit cells. The LOWER BYTE contains the 8-bit
    // character value, while (for now) the UPPER BYTE is either 0 (display
    // normally) or 255 (inverse). Eventually the upper byte might be
    // switched to bitfields, e.g. bit 0 = inverse, bit 1 = blink, maybe an
    // underscore cursor or something, etc. This is slightly wasteful, but
    // greatly simplifies keeping character values and attributes always
    // moving together (and the display buffer really isn't THAT big).
    uint8_t mask = row[x] >> 8;
    uint8_t c = (row[x] & 255) - FONT_FIRST_ASCII;
    scanbuf[x] = font_8x8[offset + c] ^ mask;
  }
  uint32_t *tmdsbuf;
  queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
  tmds_encode_1bpp((const uint32_t *)scanbuf, tmdsbuf, WIDTH * 8);
  queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
}

void __not_in_flash_func(text1_scanline_callback)(void) {
  static uint y = 1;
  ((DVItext1 *)gfxptr)->_prepare_scanline(y);
  y = (y + 1) % (((DVItext1 *)gfxptr)->height() * 8);
}

static void mainlooptext1(struct dvi_inst *inst) {
  // Idle func, everything happens in interrupt
  for (;;)
    delay(1000);
}

#else

// Old way, without interrupt
// This is a little simpler and might stick with it
// since nothing important to do in idle func above.

static void __not_in_flash_func(mainlooptext1)(struct dvi_inst *inst) {
  (void)inst; // Avoid compiler warning
  ((DVItext1 *)gfxptr)->_mainloop();
}

void __not_in_flash_func(DVItext1::_mainloop)(void) {
  for (;;) {
    for (uint16_t y = 0; y < HEIGHT; y++) {
      uint16_t *row = getBuffer() + y * WIDTH;
      for (uint8_t y1 = 0; y1 < 8; y1++) {
        uint32_t offset = y1 * FONT_N_CHARS;
        for (uint16_t x = 0; x < WIDTH; x++) {
          // See notes above
          uint8_t mask = row[x] >> 8;
          uint8_t c = (row[x] & 255) - FONT_FIRST_ASCII;
          scanbuf[x] = font_8x8[offset + c] ^ mask;
        }
        uint32_t *tmdsbuf;
        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
        tmds_encode_1bpp((const uint32_t *)scanbuf, tmdsbuf, WIDTH * 8);
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
      }
    }
  }
}

#endif // end TERM_USE_INTERRUPT

bool DVItext1::begin(void) {
  if ((getBuffer())) {
    fillScreen(' ');
    gfxptr = this;
#ifdef TERM_USE_INTERRUPT
    dvi0.scanline_callback = text1_scanline_callback;
#endif
    mainloop = mainlooptext1;
    PicoDVI::begin();

    // Must do this AFTER begin because tmdsbuf (accessed in func)
    // doesn't exist yet until dvi_init (in begin) is called.
#ifdef TERM_USE_INTERRUPT
    _prepare_scanline(0);
#endif

    wait_begin = false; // Set core 1 in motion
    return true;
  }
  return false;
}
