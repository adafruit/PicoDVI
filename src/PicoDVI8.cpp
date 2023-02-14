#include "PicoDVI8.h"
#include "libdvi/tmds_encode.h"

//
// Needed access to be called from setup1() on core1
// 
extern PicoDVI *dviptr;

#ifndef MIN
#define MIN(a,b)    (((a) < (b)) ? a : b)
#endif

//
// Include the tmds values to build our color palette
//
const uint32_t tmds_table[] = {
#include "libdvi/tmds_table.h"
};

//
// allow us to change instances of the PicoDVI8 class
//
void loop1(void) {
  while (wait_begin)
    ; // Wait for DVIGFX*::begin() to do its thing on core 0
  dviptr->_setup();
}

PicoDVI8 * PicoDVI8::instance = NULL;

PicoDVI8::PicoDVI8(const uint16_t w, const uint16_t h,
          const struct dvi_timing &t,
          vreg_voltage v,
          const struct dvi_serialiser_cfg &c,
          uint8_t repeate) :
       PicoDVI(t, c, v), 
       GFXcanvas8(MIN(w,t.h_active_pixels / DVI_SYMBOLS_PER_WORD), 
                  MIN(h,t.v_active_lines / repeate)) {

   int linePixelCount = t.h_active_pixels / DVI_SYMBOLS_PER_WORD;

   instance = this;
   exitMainLoop = false;
   
   dvi_vertical_repeat = repeate;
   dvi_monochrome_tmds = false;

   if (repeate == 1) {
       interlace = true;
   } else {
       interlace = false;
   }


   blankLine = (uint32_t *)malloc(3 * linePixelCount * sizeof(uint32_t));

   uint32_t * p = blankLine;

   // index 0 is black
   for (int i = 0; i < 3*linePixelCount; i++) {
      *p++ = tmds_table[0];
   }

   resetPalette();
}

PicoDVI8:: ~PicoDVI8(void) {

  instance = NULL;

  if (blankLine) {
      free(blankLine);
  }
  blankLine = NULL;

  // reset core 1 wait flag for the PicoDVI baseclass
  // This is kinda a moot point, as the libdvi code
  // can't be restarted at this point.   But if that changes
  // in the future, this might work...
  wait_begin = true; 

  exitMainLoop = true;

  // give it some time to exit 
  delay(10);

} 

void __not_in_flash_func(PicoDVI8::scanBufferMain)(struct dvi_inst *inst) {

  uint pixwidth = inst->timing->h_active_pixels;
  uint words_per_channel = pixwidth / DVI_SYMBOLS_PER_WORD;

  uint screenScanLines = inst->timing->v_active_lines / dvi_vertical_repeat;
  uint screenWidth = inst->timing->h_active_pixels / DVI_SYMBOLS_PER_WORD;

  uint16_t scanline = 0;
  uint8_t  even = true;

  while (!exitMainLoop) {

    uint32_t *tmdsbuf;
    uint8_t *b8;

    queue_remove_blocking_u32(&inst->q_tmds_free, &tmdsbuf);

    // discard our blankline buffer
    if (tmdsbuf == blankLine) {
        tmdsbuf = NULL;
    }

    if (tmdsbuf) {

      uint32_t * pB = tmdsbuf;
      uint32_t * pG = tmdsbuf + words_per_channel;
      uint32_t * pR = tmdsbuf + 2 * words_per_channel;
  
      if (scanline < HEIGHT) {
            
        b8  = &getBuffer()[WIDTH * scanline]; // New scanline

        for (int i = 0; i < WIDTH; i++) {
             uint8_t  idx = b8[i];
       
             *pB++ = btmds[idx];
             *pG++ = gtmds[idx];
             *pR++ = rtmds[idx];
         }
     
         // index 0 is black
         for (int i = WIDTH; i < screenWidth; i++) {
             *pB++ = tmds_table[0];
             *pG++ = tmds_table[0];
             *pR++ = tmds_table[0];
         }
      } else {

         // re-queue the free buffer and use our blank line instead
         queue_add_blocking_u32(&inst->q_tmds_free, &tmdsbuf);

         tmdsbuf = blankLine;
      }
   
      queue_add_blocking_u32(&inst->q_tmds_valid, &tmdsbuf);

      //
      // If interlacing, add blank lines every other line.
      // So 1/2 the lines are generated each frame.
      //
      if (interlace) {

          //
          // scanline % 2 produces 0 for even values and 1 for odd,
          // so invert it to convert it to a bool
          //
          if ((!(scanline % 2) == even)) {

              // don't add a blank line after the last odd scanline
              if (scanline+1 != screenScanLines) {
                 queue_add_blocking_u32(&inst->q_tmds_valid, &blankLine);
                 scanline = (scanline + 1) % screenScanLines;     
              }
          }

          //
          //  last even line is 478, scanline == 479 after injection
          //  so queue line zero as blank and switch to odd.
          //
          if ((even) && ((scanline + 1) == screenScanLines)) {
              queue_add_blocking_u32(&inst->q_tmds_valid, &blankLine);
              scanline = (scanline + 1) % screenScanLines;     
              even = false;
          } else 
          // last odd scanline is 479, since we skipped injection
          if ((!even) && (scanline+1 == screenScanLines)) {
              even = true;
          }
      }

      scanline = (scanline + 1) % screenScanLines;     // Next scanline index
    }
  }
}

bool PicoDVI8::begin(void) {

  dviptr = this;

  resetPalette();

  mainloop = _scanBufferMain; 
  dvi0.scanline_callback = NULL;

  PicoDVI::begin();

  wait_begin = false; // Set core 1 in motion

  return true;
}

void     PicoDVI8::resetPalette() {

   inverted = false;
 
   paletteIdx = 0; 
   addColor(BLACK);
   addColor(NAVY);
   addColor(DARK_GREEN);
   addColor(DARK_CYAN);
   addColor(MAROON);
   addColor(PURPLE);
   addColor(OLIVE);
   addColor(LIGHT_GREY);
   addColor(DARK_GREY);
   addColor(BLUE); 
   addColor(GREEN);
   addColor(CYAN);
   addColor(RED);
   addColor(MAGENTA);
   addColor(YELLOW);
   addColor(WHITE);

   for (int i = paletteIdx; i < PICODVI8_MAX_COLORS; i++) {
      setTMDS(i,0x0000);
      palette[i] = 0x0000;
   }
}

uint8_t  PicoDVI8::getColorIdx(uint16_t color) {

   for (int i = 0; i < paletteIdx; i++) {
     if (palette[i] == color) {
         return (i);
     }
   }
   return (addColor(color));
}

void PicoDVI8::setTMDS(uint8_t idx,uint16_t color) {

  if (inverted) {
     color = ~color;
  }

  rtmds[idx] = tmds_table[(color & 0b1111100000000000) >> 10] ;
  gtmds[idx] = tmds_table[(color & 0b0000011111100000) >>  5] ;
  btmds[idx] = tmds_table[(color & 0b0000000000011111) <<  1] ;

}

uint8_t  PicoDVI8::addColor(uint16_t color) {

    if (paletteIdx < (PICODVI8_MAX_COLORS-1)) {
        setTMDS(paletteIdx,color);
        palette[paletteIdx] = color;
        return (paletteIdx++);
    }

    //
    // Look for a similar color by masking
    // out the lower color bits.
    //
    uint32_t mask = 0b0000100001100001;
    for (int j = 0; j < 3; j++) {
       uint16_t workingMask = ~mask;
       uint16_t maskColor = color & workingMask;

       for (int i = 0; i < paletteIdx; i++) {
         if ((palette[i] & workingMask) == maskColor) {
             return (i);
         }
       }
       mask = (mask << 1) | 0b0000100001100001;
    }

    //  If no matches were found, reuse the last spot 
    Serial.println("Ran out of palette entries");

    setTMDS(paletteIdx,color);
    palette[paletteIdx] = color;

    return (paletteIdx);
}


void PicoDVI8::drawPixel(int16_t x, int16_t y, uint16_t color) {
    
   GFXcanvas8::drawPixel(x,y,getColorIdx(color));
}

void PicoDVI8::fillScreen(uint16_t color) {
   resetPalette();
   GFXcanvas8::fillScreen(getColorIdx(color));
}

void PicoDVI8::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
   GFXcanvas8::drawFastVLine(x,y,h,getColorIdx(color));
}

void PicoDVI8::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
   GFXcanvas8::drawFastHLine(x,y,w,getColorIdx(color));
}

uint16_t PicoDVI8::getPixel(int16_t x, int16_t y) const {
   if ((x < WIDTH) && (y < HEIGHT)) {
       return (palette[getBuffer()[y*WIDTH + x]]);
   }
   return (0);
}

void PicoDVI8::invertDisplay(bool i) {
   if (i != inverted) {
     inverted = i;
     for (int i = 0; i < paletteIdx; i++) {
        // rebuild he TMDS palette
        setTMDS(i,palette[i]);
     }
   }
}
