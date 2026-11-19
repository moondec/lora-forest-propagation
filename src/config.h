// Board pins and radio settings (methodology Sections 4.2-4.3; pin map verified 17 Nov 2026).
#pragma once

#include <stdint.h>

#if defined(ROLE_TOWER) == defined(ROLE_MOBILE)
#error "Build with exactly one of ROLE_TOWER or ROLE_MOBILE (pio run -e tower | -e mobile)"
#endif

namespace cfg {

constexpr uint8_t kFwVersion = 2;  // 2: record type 4 (RxFrameRec), 16 MB layout
constexpr const char* kFwTag = "2.0.0";

#ifdef ROLE_TOWER
constexpr bool kTower = true;
constexpr uint8_t kRole = 1;
constexpr uint8_t kDefaultNodeId = 1;
#else
constexpr bool kTower = false;
constexpr uint8_t kRole = 2;
constexpr uint8_t kDefaultNodeId = 2;
#endif

// SX1262
constexpr int kPinNss = 23, kPinSck = 20, kPinMosi = 21, kPinMiso = 22, kPinBusy = 19, kPinDio1 = 7;
// PI4IOE5V6408 I/O expander: P7 radio reset, P6 RF switch enable, P5 LNA enable, P0 user button
constexpr int kPinSda = 10, kPinScl = 8;
constexpr uint8_t kExpanderAddr = 0x43;
// Grove UART to Unit GPS v1.1: ESP receives on G4, transmits on G5
constexpr int kPinGnssRx = 4, kPinGnssTx = 5;
constexpr uint32_t kGnssBaud = 115200;
// SSD1306 64x48 on the shared SPI bus; WS2812 status LED
constexpr int kPinOledCs = 6, kPinOledDc = 18, kPinOledRst = 15, kPinLed = 2;

// Radio (fixed for the whole experiment)
constexpr float kFreqMHz = 869.525f;
constexpr float kBwKHz = 125.0f;
constexpr uint8_t kCrDenom = 5;
constexpr uint8_t kSyncWord = 0x52;  // private: not 0x12 (RadioLib), 0x2B (Meshtastic), 0x34 (LoRaWAN)
constexpr int8_t kTxPowerDbm = 14;
constexpr uint16_t kPreamble = 8;
constexpr float kTcxoV = 3.0f;

}  // namespace cfg
