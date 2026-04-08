#pragma once
#include "app.h"
#include <Arduino.h>

#ifndef DEBUG
#define DEBUG               1   // set to 1 to enable debug prints
#endif

#ifndef FW_VERSION
#define FW_VERSION          "dev"
#endif

// Set to 1 to compile the optional Zigbee controller support.
// For arduino-cli on ESP32-C6, this also requires Zigbee-specific board options:
//   --fqbn esp32:esp32:esp32c6:PartitionScheme=zigbee_zczr,ZigbeeMode=zczr
#ifndef USE_ZIGBEE
#define USE_ZIGBEE          1
#endif

#if USE_ZIGBEE && !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(ARDUINO_ESP32C6_DEV)
#error "USE_ZIGBEE currently supports the ESP32-C6 build in this project."
#endif

#if USE_ZIGBEE && (!defined(CONFIG_ZB_ENABLED) || !CONFIG_ZB_ENABLED)
#error "USE_ZIGBEE=1 requires a Zigbee-enabled ESP32 core configuration. For arduino-cli use esp32:esp32:esp32c6:PartitionScheme=zigbee_zczr,ZigbeeMode=zczr."
#endif

#if DEBUG
  #define DBG(x)            Serial.println(x)
  #define DBGF(fmt, ...)    Serial.printf(fmt "\n", ##__VA_ARGS__)
  #define DBGV(label, val)  do { Serial.print(label "="); Serial.println(val); } while(0)
  #define DBG_SEP()         Serial.println(F("---"))
#else
  #define DBG(x)
  #define DBGF(fmt, ...)
  #define DBGV(label, val)
  #define DBG_SEP()
#endif

#define DBG_ERROR(x)         Serial.println(x)

// To configure a different display
#define USE_COLORDISPLAY      0

#define textSpace 14

// =======================
// Pins
// =======================

#if CONFIG_IDF_TARGET_ESP32C6
#define EPD_CS    1
#define EPD_DC    8
#define EPD_RST   14
#define EPD_BUSY  7
#define EPD_SCK   23
#define EPD_MOSI  22
#define PIN_DISPLAYPOWER   4    // display power enable
#define BAT_PIN   0   //35
#define DEMO_BUTTON GPIO_NUM_2
#elif CONFIG_IDF_TARGET_ESP32
#define EPD_CS    15
#define EPD_DC    27
#define EPD_RST   26
#define EPD_BUSY  25
#define EPD_SCK   13
#define EPD_MOSI  14
#define PIN_DISPLAYPOWER   4    // display power enable
#define BAT_PIN   35
// #define DEMO_BUTTON GPIO_NUM_33
#endif


#define BUTTON_HOLD_MS       2000     // 2 seconds hold enter demo mode
#define BUTTON_LONG_HOLD_MS  8000     // 8 seconds hold force reconfigure

inline constexpr uint32_t MDI_FAN_COOLING  = 0x000F1797;
inline constexpr uint32_t MDI_FAN_EXHAUST  = 0x000F0D43;
inline constexpr uint32_t MDI_FAN_AUX      = 0x000F0210;
inline constexpr uint32_t MDI_HUMIDITY     = 0x000F058E;
inline constexpr uint32_t MDI_THERMOMETER  = 0x000F050F;
inline constexpr uint32_t MDI_CLOCK        = 0x000F1452;
inline constexpr uint32_t MDI_LAYERS       = 0x000F0328;
inline constexpr uint32_t MDI_BED          = 0x000F0697;
inline constexpr uint32_t MDI_NOZZLE       = 0x000F0E5B;
inline constexpr uint32_t MDI_SPOOL        = 0x000F1294;
inline constexpr uint32_t MDI_ZIGBEE       = 0x000F0D41;
inline constexpr uint32_t MDI_ALERT        = 0x000F0026;
inline constexpr uint32_t MDI_POWER_ON     = 0x000F06A5;
inline constexpr uint32_t MDI_POWER_OFF    = 0x000F06A6;
inline constexpr uint32_t MDI_PRINT_ON     = 0x000F0E5B;
