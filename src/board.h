// Unit C6L board support: I/O expander (radio reset, RF switch, LNA, button), LED, temperature.
#pragma once

#include <stdint.h>

namespace board {

// Initialises I2C, the expander and the shared SPI bus; releases radio reset.
bool begin();
bool buttonPressed();
void led(uint8_t r, uint8_t g, uint8_t b);
int8_t tempC();

}  // namespace board
