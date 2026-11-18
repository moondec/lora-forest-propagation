#include "board.h"

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <driver/temperature_sensor.h>

#include "config.h"

namespace board {

// PI4IOE5V6408 registers (as in the Meshtastic m5stack_unitc6l variant, commit 968692d)
constexpr uint8_t kRegChipReset = 0x01, kRegIoDir = 0x03, kRegOutSet = 0x05, kRegOutHiZ = 0x07,
                  kRegInDefault = 0x09, kRegPullEn = 0x0B, kRegPullSel = 0x0D, kRegInState = 0x0F,
                  kRegIntMask = 0x11, kRegIrqState = 0x13;

static bool writeReg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(cfg::kExpanderAddr);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

static bool readReg(uint8_t reg, uint8_t* v) {
  Wire.beginTransmission(cfg::kExpanderAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(cfg::kExpanderAddr, static_cast<uint8_t>(1)) != 1) return false;
  *v = Wire.read();
  return true;
}

bool begin() {
  // Both SPI chip-selects idle high before the bus starts, so neither device answers by accident.
  pinMode(cfg::kPinOledCs, OUTPUT);
  digitalWrite(cfg::kPinOledCs, HIGH);
  pinMode(cfg::kPinNss, OUTPUT);
  digitalWrite(cfg::kPinNss, HIGH);
  SPI.begin(cfg::kPinSck, cfg::kPinMiso, cfg::kPinMosi, -1);

  Wire.begin(cfg::kPinSda, cfg::kPinScl, 400000);
  uint8_t v;
  bool ok = writeReg(kRegChipReset, 0xFF);
  delay(10);
  ok &= readReg(kRegChipReset, &v);
  ok &= writeReg(kRegIoDir, 0b11100000);      // P5-P7 outputs
  ok &= writeReg(kRegOutHiZ, 0b00011100);     // P2-P4 high impedance
  ok &= writeReg(kRegPullSel, 0b11100011);    // pull-ups
  ok &= writeReg(kRegPullEn, 0b11100011);
  ok &= writeReg(kRegInDefault, 0b00000011);  // buttons idle high
  ok &= writeReg(kRegIntMask, 0b11111100);
  ok &= writeReg(kRegOutSet, 0b10000000);     // P7 high: radio out of reset
  ok &= readReg(kRegIrqState, &v);            // clear pending interrupt
  delay(10);
  ok &= writeReg(kRegOutSet, 0b11100000);     // + RF switch and LNA enabled
  delay(10);
  return ok;
}

bool buttonPressed() {
  uint8_t v;
  if (!readReg(kRegInState, &v)) return false;
  return (v & 0x01) == 0;  // P0, active low
}

void led(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(cfg::kPinLed, r, g, b); }

// The core's temperatureRead() installs the sensor for 10-50 C only. Winter fieldwork (30 m on the
// SA tower in January) goes below -10 C, so use the -30..50 C range. This is chip temperature.
static temperature_sensor_handle_t tsens = nullptr;

int8_t tempC() {
  if (!tsens) {
    temperature_sensor_config_t c = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-30, 50);
    if (temperature_sensor_install(&c, &tsens) != ESP_OK || temperature_sensor_enable(tsens) != ESP_OK) {
      tsens = nullptr;
      return INT8_MIN;
    }
  }
  float t = 0;
  if (temperature_sensor_get_celsius(tsens, &t) != ESP_OK) return INT8_MIN;
  return static_cast<int8_t>(lroundf(t));
}

}  // namespace board
