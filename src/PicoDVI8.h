#ifndef PICO_DVI_8_H
#define PICO_DVI_8_H

#include "PicoDVI.h"

#define PICODVI8_MAX_COLORS   256

//
// PicoDVI8 is a framebuffer that automatically builds the color palette 
// based on the colors in the calls to the GFXcanvas8.   Only 
// one instance of this class should be create at one one time, 
// since the scanBufferMain runs continuiosly on core1.  The color palette 
// will be reset when the fillScreen method is called.
//
// The frame buffer width/height can be set to 1/2 the monitor timing.
// So for 640x480, the max w is 320 and the max h is 240.  You can also
// set the w or h lower than 1/2 and it will pad out the right/bottom 
// with black lines. 
//
// If you want to use the full vertical resolution, you can set repeate to
// 1.   This will then use an interlaced scan line model (see comments 
// below for scanBufferMain for more info).  This works fine for lcd monitors,
// but did not look good on usb capture dongles.
//
class PicoDVI8 : public PicoDVI, public GFXcanvas8 {
public:
  PicoDVI8(const uint16_t w = 320, const uint16_t h = 240,
          const struct dvi_timing &t = dvi_timing_640x480p_60hz,
          vreg_voltage v = VREG_VOLTAGE_1_10,
          const struct dvi_serialiser_cfg &c = pimoroni_demo_hdmi_cfg,
          uint8_t repeate = 2);
  ~PicoDVI8(void);

  bool begin(void);

  static PicoDVI8 * getInstance() { return instance; }; 

  //
  // Generate the scanlines from the GFXcanvas8 framebuffer
  // This happens on the second core.  This is done a bit
  // differently for performance reasons.   
  //
  // The pixel clock is run at 10 clock cycles per pixes.
  // So for 640x480 the system clock is set to 252mhz, which
  // gives 8750 clock cycles (252,000,000 / 480 / 60 fps) to
  // generate the scan line.   The default method in 
  // dvi_scanbuf_main_16bpp takes about 28,000 clock cycles to 
  // convert the scanline to tmds.  So it can barley keep up with
  // generating every other scan line (hence the red lines
  // displaying randomly).
  //
  // So this version of the scanBufferMain, converts the 
  // color palette entries into the tmds 10 bit values 
  // and builds the scan line buffer directly.   This takes about
  // 16000 clock cycles, so you can do 240 scan lines per second
  // with no red lines.
  //
  // One of the reason to use a color pallet was to save ram, but
  // it would be nice to be able to be able to get a full 480 scan 
  // lines.   To acheve this, it uses an interlacing pattern.  So for 
  // one frame only the even scan lines are generated and a black line
  // is sent for the odd scan lines.   Then the next frame the odd scan
  // lines are sent with blak even lines.   This works find on LCD monitors
  // and looks like the old interlaced monitors (not as bright or crisp as
  // non-interlaced).  This doesn't seem to work for usb capture dongles 
  // (just looks like garbage on mine).  So try it, and if it works for you 
  // great.   If not, use the double scan line (1/2 resolution) mode.
  //
  static void _scanBufferMain(struct dvi_inst *inst) {
     instance->scanBufferMain(inst);
  }
  void scanBufferMain(struct dvi_inst *inst);

  //
  // GFX functions to build the pallete
  //
  void drawPixel(int16_t x, int16_t y, uint16_t color);
  void fillScreen(uint16_t color);
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);

  //
  // Pallet functions.   They should not need to be called as they are automatically
  // called as you use the framebuffer from the GFX functions above.   But if
  // you want to set a specifc pallet, you canuse them to do that.
  //
  void     resetPalette();
  uint8_t  addColor(uint16_t color);

  // The default colors in the pallete
  static const uint32_t BLACK      = (0x0000); 
  static const uint32_t NAVY       = (0x000F); 
  static const uint32_t DARK_GREEN = (0x03E0);
  static const uint32_t DARK_CYAN  = (0x03EF); 
  static const uint32_t MAROON     = (0x7800); 
  static const uint32_t PURPLE     = (0x780F); 
  static const uint32_t OLIVE      = (0x7BE0); 
  static const uint32_t LIGHT_GREY = (0xC618); 
  static const uint32_t DARK_GREY  = (0x7BEF); 
  static const uint32_t BLUE       = (0x001F); 
  static const uint32_t GREEN      = (0x07E0); 
  static const uint32_t CYAN       = (0x07FF); 
  static const uint32_t RED        = (0xF800); 
  static const uint32_t MAGENTA    = (0xF81F); 
  static const uint32_t YELLOW     = (0xFFE0); 
  static const uint32_t WHITE      = (0xFFFF); 


protected:

  uint16_t palette[PICODVI8_MAX_COLORS];

  void     setTMDS(uint8_t idx,uint16_t color);
  uint32_t rtmds[PICODVI8_MAX_COLORS];
  uint32_t gtmds[PICODVI8_MAX_COLORS];
  uint32_t btmds[PICODVI8_MAX_COLORS];

  uint8_t  getColorIdx(uint16_t color);

  uint16_t paletteIdx;   // index of last used color
 
  uint8_t  interlace;
  uint32_t *blankLine;

  uint8_t  exitMainLoop;

  static PicoDVI8 *instance;
};


#endif
