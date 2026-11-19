#include "display.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <string.h>

#include "config.h"

namespace display {

// Hardware SPI on the bus already started by board::begin(); U8g2 drives its own CS (G6).
// R2: rotated 180 degrees, the natural reading orientation of the mounted unit.
static U8G2_SSD1306_64X48_ER_F_4W_HW_SPI oled(U8G2_R2, cfg::kPinOledCs, cfg::kPinOledDc, cfg::kPinOledRst);
static char banner[12];
static char last[3][12];

void begin(bool tower, uint8_t nodeId) {
  snprintf(banner, sizeof banner, "%s %u", tower ? "TOWER" : "MOBILE", nodeId);
  oled.begin();
  oled.setContrast(130);
  last[0][0] = last[1][0] = last[2][0] = '\x01';  // force the first draw
  show("", "", "");
}

void show(const char* l1, const char* l2, const char* l3) {
  const char* lines[3] = {l1, l2, l3};
  bool changed = false;
  for (int i = 0; i < 3; ++i)
    if (strncmp(last[i], lines[i], sizeof last[i] - 1) != 0) changed = true;
  if (!changed) return;
  for (int i = 0; i < 3; ++i) snprintf(last[i], sizeof last[i], "%s", lines[i]);

  oled.clearBuffer();
  oled.setFont(u8g2_font_7x13B_tr);  // banner: role in bold, readable at arm's length
  oled.drawStr(0, 12, banner);
  oled.drawHLine(0, 14, 64);
  oled.setFont(u8g2_font_5x8_tr);
  for (int i = 0; i < 3; ++i) oled.drawStr(0, 24 + 9 * i, last[i]);
  oled.sendBuffer();
}

}  // namespace display
