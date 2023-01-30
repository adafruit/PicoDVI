// Display a basic info screen to test the different PicoDVI8 resolutions
//
#include <PicoDVI8.h>

// Create an non-interlaced 320x240 instance
PicoDVI8 tft = PicoDVI8(320, 480, dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_10, pimoroni_demo_hdmi_cfg, 2);
// Create an interlaced 320x480 instance
//PicoDVI8 tft = PicoDVI8(320, 480, dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_10, pimoroni_demo_hdmi_cfg, 1);
// Create an non-interlaced 400x240 instance
//PicoDVI8 tft = PicoDVI8(400, 480, dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_25, pimoroni_demo_hdmi_cfg, 2);
// Create an interlaced 400x480 instance
//PicoDVI8 tft = PicoDVI8(400, 480, dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_25, pimoroni_demo_hdmi_cfg, 1);

void centerText(Adafruit_GFX &tft,const char *str, int y) {
  int16_t  x1, y1;
  uint16_t w, h;

  tft.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);

  tft.setCursor((tft.width() - w) / 2, y);
  tft.print(str);
}

//
// Display the screen resolution, heap info and cpu clock frequency
//
void displayInfo(Adafruit_GFX &tft) {
  
  int y = 2;

  tft.setRotation(0);     //Portrait
  tft.fillScreen(PicoDVI8::BLACK);
  tft.setTextColor(PicoDVI8::WHITE, PicoDVI8::BLACK);
  tft.setTextSize(2);     // System font is 8 pixels.  ht = 8*2=16
  tft.setCursor(0, 0);

  tft.setRotation(0);

  tft.drawRect(10, 10, tft.width() - 20, tft.height() - 20, PicoDVI8::WHITE);
  tft.setTextSize(2);
  centerText(tft,"PicoDVI8 Display", y * 16);
  y += 2;
  tft.setTextSize(1);
  tft.setCursor(30, y++ * 16);
  tft.print("Width: ");
  tft.print(tft.width());
  tft.setCursor(30, y++ * 16);
  tft.print("Height: ");
  tft.print(tft.height());
  tft.setCursor(30, y++ * 16);
  tft.print("Total Heap:");
  tft.print(rp2040.getTotalHeap());
  tft.setCursor(30, y++ * 16);
  tft.print("Free Heap:");
  tft.print(rp2040.getFreeHeap());
  tft.setCursor(30, y++ * 16);

  uint f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
  tft.print("CLK sys:");
  tft.print(f_clk_sys);
  tft.print("khz");
  tft.fillRect(30,y++*16,tft.width()-60,16,PicoDVI8::RED);
  tft.fillRect(30,y++*16,tft.width()-60,16,PicoDVI8::BLUE);
  tft.fillRect(30,y++*16,tft.width()-60,16,PicoDVI8::GREEN);
}

void setup()
{
  int count = 0;

  Serial.begin(115200);
 
  // wait for up to 4 seconds
  while ((!Serial) && (count < 40)) {
    // wait for serial port to connect. Needed for native USB port only
    count++;
    delay(100);
  }
  
  Serial.println("Starting");

  if (tft.begin()) {
    displayInfo(tft);

  } else {
    Serial.println("Display failed to start");
  }

}


void loop()
{

}
