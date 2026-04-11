#pragma once
#include "app.h"
#include <Arduino.h>

#ifndef DEBUG
#define DEBUG               1   // set to 1 to enable debug prints
#endif

#ifndef FW_VERSION
#define FW_VERSION          ".25"
#endif

// Set to 1 to compile the optional Zigbee controller support.
// For arduino-cli on ESP32-C6, this also requires Zigbee-specific board options:
//   --fqbn esp32:esp32:esp32c6:PartitionScheme=zigbee_zczr,ZigbeeMode=zczr
#ifndef USE_ZIGBEE
#define USE_ZIGBEE          0
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

inline constexpr int16_t PIN_UNASSIGNED = -1;

struct PinConfig {
  int16_t epdCs;
  int16_t epdDc;
  int16_t epdRst;
  int16_t epdBusy;
  int16_t epdSck;
  int16_t epdMosi;
  int16_t displayPower;
  int16_t battery;
  int16_t demoButton;
};

enum class PinPreset : uint8_t {
  Esp32Waveshare,
  Esp32Default,
  Esp32C6Default,
  Esp32C6SuperMini,
  Custom
};

inline constexpr PinConfig makePinPreset(PinPreset preset) {
  switch (preset) {
    case PinPreset::Esp32Waveshare:
      return PinConfig{15, 27, 26, 25, 13, 14, 33, 35, PIN_UNASSIGNED};
    case PinPreset::Esp32C6Default:
      return PinConfig{1, 8, 14, 7, 23, 22, 4, 0, 2};
    case PinPreset::Esp32C6SuperMini:
      return PinConfig{4, 20, 21, 22, 7, 5, 1, PIN_UNASSIGNED, 2};
    case PinPreset::Esp32Default:
      return PinConfig{15, 27, 26, 25, 13, 14, 4, 35, 33};
    case PinPreset::Custom:
    default:
      return PinConfig{
        PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED,
        PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED,
        PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED
      };
  }
}

// Example override before the display is first touched:
//   applyPinPreset(PinPreset::Esp32Default);
//   setCustomPinConfig({15, 27, 26, 25, 13, 14, 4, 35, PIN_UNASSIGNED});
#if defined(DEFAULT_PIN_PRESET_ESP32_WAVESHARE)
inline constexpr PinPreset DEFAULT_PIN_PRESET = PinPreset::Esp32Waveshare;
#elif defined(DEFAULT_PIN_PRESET_ESP32)
inline constexpr PinPreset DEFAULT_PIN_PRESET = PinPreset::Esp32Default;
#elif defined(DEFAULT_PIN_PRESET_ESP32C6_FIREBEETLE)
inline constexpr PinPreset DEFAULT_PIN_PRESET = PinPreset::Esp32C6Default;
#elif defined(DEFAULT_PIN_PRESET_ESP32C6_SUPERMINI)
inline constexpr PinPreset DEFAULT_PIN_PRESET = PinPreset::Esp32C6SuperMini;
#elif CONFIG_IDF_TARGET_ESP32C6
  #if defined(ARDUINO_DFROBOT_FIREBEETLE_2_ESP32C6)
inline constexpr PinPreset DEFAULT_PIN_PRESET = PinPreset::Esp32C6Default;
  #elif defined(ARDUINO_MAKERGO_C6_SUPERMINI)
inline constexpr PinPreset DEFAULT_PIN_PRESET = PinPreset::Esp32C6SuperMini;
  #else
inline constexpr PinPreset DEFAULT_PIN_PRESET = PinPreset::Esp32C6Default;
  #endif
#elif CONFIG_IDF_TARGET_ESP32
inline constexpr PinPreset DEFAULT_PIN_PRESET = PinPreset::Esp32Default;
#else
inline constexpr PinPreset DEFAULT_PIN_PRESET = PinPreset::Custom;
#endif

extern PinConfig activePins;

inline void applyPinPreset(PinPreset preset) {
  activePins = makePinPreset(preset);
}

inline void setCustomPinConfig(const PinConfig& customPins) {
  activePins = customPins;
}

inline bool isPinAssigned(int16_t pin) {
  return pin >= 0;
}

inline bool hasDisplayPowerPin() {
  return isPinAssigned(activePins.displayPower);
}

inline bool hasBatteryPin() {
  return isPinAssigned(activePins.battery);
}

inline bool hasDemoButtonPin() {
  return isPinAssigned(activePins.demoButton);
}

inline gpio_num_t getDemoWakeGpio() {
  return static_cast<gpio_num_t>(activePins.demoButton);
}

#define BUTTON_HOLD_MS       2000     // 2 seconds hold enter demo mode
#define BUTTON_LONG_HOLD_MS  6000     // 6 seconds hold force reconfigure

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
