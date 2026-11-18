// Entry point; the role (tower or mobile) is chosen at build time (pio run -e tower | -e mobile).
#include <Arduino.h>

#include "app.h"

void setup() { app::setup(); }
void loop() { app::loop(); }
