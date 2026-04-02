#pragma once
#include "app.h"
#include <Arduino.h>

#ifndef DEBUG
#define DEBUG 1   // set to 1 to enable debug prints
#endif

#define USE_ZIGBEE 1

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


#define BUTTON_HOLD_MS  1000

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
