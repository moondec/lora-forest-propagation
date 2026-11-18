// 0.66" SSD1306 (64x48): role banner so the boards cannot be confused, plus three status lines.
#pragma once

#include <stdint.h>

namespace display {

void begin(bool tower, uint8_t nodeId);
// Each line up to 10 characters; redrawn only if the text changed.
void show(const char* l1, const char* l2, const char* l3);

}  // namespace display
