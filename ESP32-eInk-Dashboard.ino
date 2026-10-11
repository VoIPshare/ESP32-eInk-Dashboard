#include "app.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "fetchAllInfo.h"
#include "bambulab.h"
#include "calendar.h"
#include "directFetch.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <Preferences.h>
#include <cstring>
#include <cstdarg>
#include <new>
#include <esp32-hal-rgb-led.h>

static void copyToCfg(char* dest, size_t cap, const String& s) {
  if (!dest || cap == 0) return;
  strncpy(dest, s.c_str(), cap - 1);
  dest[cap - 1] = '\0';
}

#if USE_ZIGBEE
// #warning "ZIGBEE HEADER INCLUDED"
#include "dash_zigbee.h"
#endif

#define AP_SSID "Dashboard-Setup"
#define AP_TIMEOUT_MS (5UL * 60UL * 1000UL)   // leave setup mode after 5 min without activity
static unsigned long apLastActivity = 0;     // millis() of AP start / last setup page request

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

char wifi_ssid[CFG_WIFI_SSID_MAX];
char wifi_pass[CFG_WIFI_PASS_MAX];
char mqtt_pass[CFG_MQTT_PASS_MAX];
char mqtt_sn[CFG_MQTT_SN_MAX];
char mqtt_ip[CFG_MQTT_IP_MAX];
uint16_t mqtt_port;
char device_timezone[CFG_TIMEZONE_MAX];
char weather_location[CFG_WEATHER_LOC_MAX];
char stock_symbols[CFG_STOCKS_MAX];
char ics_urls[ICS_MAX_FEEDS][CFG_ICS_URL_MAX];
char makerworld_uid[CFG_MW_UID_MAX];
char printer_name[CFG_PRINTER_NAME_MAX];
char pkge_api_key[CFG_PKGE_KEY_MAX];
char aerodatabox_key[CFG_ADB_KEY_MAX];

// MakerWorld stats are refreshed at most this often, and only while the idle view is shown.
#define MAKERWORLD_REFRESH_S  (60 * 60)
RTC_DATA_ATTR time_t makerworldLastFetch = 0;

// Preference keys for the calendar feeds (the first keeps the original single-feed key).
static const char* const ICS_PREF_KEYS[ICS_MAX_FEEDS] = { "ics_url", "ics_url2", "ics_url3" };
PinConfig activePins = makePinPreset(DEFAULT_PIN_PRESET);
PinConfig customPins = makePinPreset(PinPreset::Custom);
PinPreset activePinPreset = DEFAULT_PIN_PRESET;

bool forceUpdateStatusBar = false;
bool apModeActive = false;

#if USE_ZIGBEE
bool zigbee_enable = false;
bool zigbee_wait = true;
bool zigbee_linked = false;
bool zigbee_pairing_failed = false;

char zigbee_monitor[CFG_ZIGBEE_STR_MAX];
uint8_t zigbee_monitor_ep = 1;

char zigbee_control[CFG_ZIGBEE_STR_MAX];
uint8_t zigbee_control_ep = 1;
RTC_DATA_ATTR int8_t zigbee_aux_last_command = -1;
#endif

unsigned long previousMillis = 0;

RTC_DATA_ATTR uint16_t bootCount = 0;
RTC_DATA_ATTR bool lastPrint = false;
RTC_DATA_ATTR bool pendingBambuRetry = false;
RTC_DATA_ATTR time_t lastNtpSync = 0;

// Problems listed in the top bar. Each flag is set when that source fails, cleared on its
// next success, and kept across deep sleep.
enum : uint16_t {
  PROBLEM_WIFI         = 1 << 0,
  PROBLEM_TIME         = 1 << 1,
  PROBLEM_WEATHER      = 1 << 2,
  PROBLEM_STOCKS       = 1 << 3,
  PROBLEM_CALENDAR     = 1 << 4,
  PROBLEM_PRINTER      = 1 << 5,
  PROBLEM_PRINTER_CODE = 1 << 6,
  PROBLEM_MAKERWORLD   = 1 << 7,
  PROBLEM_TRACKING     = 1 << 8,
  PROBLEM_FLIGHT       = 1 << 9,
};
static const char* const PROBLEM_LABELS[] = {
  "WiFi", "Time", "Weather", "Stocks", "Calendar", "Printer", "Access code", "MakerWorld", "Tracking", "Flight"
};
RTC_DATA_ATTR uint16_t problems = 0;
RTC_DATA_ATTR uint16_t problemsShown = 0;

static void setProblem(uint16_t flag, bool active) {
  problems = active ? (problems | flag) : (problems & ~flag);
}

#define NTP_RESYNC_S          (60 * 60)   // re-sync internet time at least hourly
#define WAKE_AFTER_MINUTE_MS  500         // wake this long after each minute starts
#define FLIGHT_LIVE_BEFORE_S  (6 * 3600)  // live flight status starts 6 h before departure
#define FLIGHT_LIVE_REFRESH_S (30 * 60)   // and is refreshed every 30 minutes until landing,
#define FLIGHT_LIVE_FAST_S    (10 * 60)   // every 10 minutes within an hour of departure or landing
#define FLIGHT_LIVE_AFTER_MAX_S (3 * 3600) // safety: never follow a flight more than 3 h past its expected landing
#define FLIGHT_LIVE_MAX_REQUESTS 30       // safety: at most this many AeroDataBox requests per flight

bool fullRefresh;

// Battery: the board divides the LiPo voltage by 2 before the ADC pin.
constexpr float BAT_DIVIDER = 2.0f;
constexpr float BAT_CALIBRATION = 1.0f;   // multimeter voltage / displayed voltage, if they differ
constexpr int   BAT_SAMPLES = 16;

// Typical single-cell LiPo resting voltage -> charge, highest first.
struct BatPoint { float v; uint8_t pct; };
constexpr BatPoint BAT_CURVE[] = {
  {4.15f, 100}, {4.10f, 95}, {4.05f, 90}, {3.98f, 80}, {3.92f, 70}, {3.87f, 60},
  {3.83f, 50}, {3.80f, 40}, {3.77f, 30}, {3.73f, 20}, {3.68f, 10}, {3.50f, 5}, {3.30f, 0},
};

const char* pinPresetValue(PinPreset preset) {
  switch (preset) {
    case PinPreset::Esp32Waveshare: return "esp32_waveshare";
    case PinPreset::Esp32Default: return "esp32";
    case PinPreset::Esp32C6Default: return "esp32_c6";
    case PinPreset::Esp32C6SuperMini: return "esp32_c6_supermini";
    case PinPreset::Custom:
    default: return "custom";
  }
}

const char* pinPresetLabel(PinPreset preset) {
  switch (preset) {
    case PinPreset::Esp32Waveshare: return "ESP32 Waveshare";
    case PinPreset::Esp32Default: return "ESP32";
    case PinPreset::Esp32C6Default: return "ESP32 C6";
    case PinPreset::Esp32C6SuperMini: return "ESP32-C6 SuperMini";
    case PinPreset::Custom:
    default: return "Custom";
  }
}

void logPinConfiguration() {
  DBGF("Pin preset: %s", pinPresetLabel(activePinPreset));
  DBGF("Pins CS=%d DC=%d RST=%d BUSY=%d SCK=%d MOSI=%d PWR=%d BAT=%d BTN=%d",
       activePins.epdCs,
       activePins.epdDc,
       activePins.epdRst,
       activePins.epdBusy,
       activePins.epdSck,
       activePins.epdMosi,
       activePins.displayPower,
       activePins.battery,
       activePins.demoButton);
}

PinPreset parsePinPreset(const String& value) {
  if (value == "esp32_waveshare") return PinPreset::Esp32Waveshare;
  if (value == "esp32") return PinPreset::Esp32Default;
  if (value == "esp32_c6") return PinPreset::Esp32C6Default;
  if (value == "esp32_c6_supermini" || value == "esp32_c6_mini") return PinPreset::Esp32C6SuperMini;
  if (value == "custom") return PinPreset::Custom;
  return DEFAULT_PIN_PRESET;
}

void applyBoardSpecificPinSetup();

void applyStoredPinSelection() {
  if (activePinPreset == PinPreset::Custom) {
    setCustomPinConfig(customPins);
  } else {
    applyPinPreset(activePinPreset);
  }
  applyBoardSpecificPinSetup();
}

int16_t readPinField(const String& name) {
  if (!server.hasArg(name)) return PIN_UNASSIGNED;
  String value = server.arg(name);
  value.trim();
  if (value.length() == 0) return PIN_UNASSIGNED;
  return static_cast<int16_t>(value.toInt());
}

void saveCustomPinConfig(Preferences& prefs, const PinConfig& pins) {
  prefs.putInt("pin_epd_cs", pins.epdCs);
  prefs.putInt("pin_epd_dc", pins.epdDc);
  prefs.putInt("pin_epd_rst", pins.epdRst);
  prefs.putInt("pin_epd_busy", pins.epdBusy);
  prefs.putInt("pin_epd_sck", pins.epdSck);
  prefs.putInt("pin_epd_mosi", pins.epdMosi);
  prefs.putInt("pin_disp_power", pins.displayPower);
  prefs.putInt("pin_battery", pins.battery);
  prefs.putInt("pin_demo_btn", pins.demoButton);
}

PinConfig loadCustomPinConfig(Preferences& prefs, const PinConfig& fallback) {
  PinConfig pins = fallback;
  pins.epdCs = static_cast<int16_t>(prefs.getInt("pin_epd_cs", fallback.epdCs));
  pins.epdDc = static_cast<int16_t>(prefs.getInt("pin_epd_dc", fallback.epdDc));
  pins.epdRst = static_cast<int16_t>(prefs.getInt("pin_epd_rst", fallback.epdRst));
  pins.epdBusy = static_cast<int16_t>(prefs.getInt("pin_epd_busy", fallback.epdBusy));
  pins.epdSck = static_cast<int16_t>(prefs.getInt("pin_epd_sck", fallback.epdSck));
  pins.epdMosi = static_cast<int16_t>(prefs.getInt("pin_epd_mosi", fallback.epdMosi));
  pins.displayPower = static_cast<int16_t>(prefs.getInt("pin_disp_power", fallback.displayPower));
  pins.battery = static_cast<int16_t>(prefs.getInt("pin_battery", fallback.battery));
  pins.demoButton = static_cast<int16_t>(prefs.getInt("pin_demo_btn", fallback.demoButton));
  return pins;
}

void pinValueToText(int16_t pin, char* out, size_t outSize) {
  if (outSize == 0) return;
  if (isPinAssigned(pin)) {
    snprintf(out, outSize, "%d", pin);
  } else {
    out[0] = '\0';
  }
}

bool hasRequiredDisplayPins(const PinConfig& pins) {
  return isPinAssigned(pins.epdCs) &&
         isPinAssigned(pins.epdDc) &&
         isPinAssigned(pins.epdRst) &&
         isPinAssigned(pins.epdBusy) &&
         isPinAssigned(pins.epdSck) &&
         isPinAssigned(pins.epdMosi);
}

void forceSuperMiniLedsOff() {
  pinMode(15, OUTPUT);
  digitalWrite(15, LOW);
  rgbLedWrite(8, 0, 0, 0);
  delay(2);
  rgbLedWrite(8, 0, 0, 0);
}

void flashSuperMiniStatusLed(uint8_t red, uint8_t green, uint8_t blue, uint8_t flashes, uint16_t onMs, uint16_t offMs) {
  if (activePinPreset != PinPreset::Esp32C6SuperMini) return;

  for (uint8_t i = 0; i < flashes; ++i) {
    rgbLedWrite(8, red, green, blue);
    delay(onMs);
    forceSuperMiniLedsOff();
    if (i + 1 < flashes) {
      delay(offMs);
    }
  }
}

void setSuperMiniStatusLed(uint8_t red, uint8_t green, uint8_t blue) {
  if (activePinPreset != PinPreset::Esp32C6SuperMini) return;
  rgbLedWrite(8, red, green, blue);
}

void updateSuperMiniHoldFeedback(unsigned long heldMs) {
  if (activePinPreset != PinPreset::Esp32C6SuperMini) return;

  if (heldMs < BUTTON_REBOOT_HOLD_MS) {
    // Green stays on while the button is being held.
    setSuperMiniStatusLed(0, 48, 0);
    return;
  }

  if (heldMs < BUTTON_SETUP_HOLD_MS) {
    // Reboot threshold reached (release now to reboot): flash cyan.
    const unsigned long phase = (heldMs - BUTTON_REBOOT_HOLD_MS) % 320UL;
    setSuperMiniStatusLed(0, 48, phase < 120UL ? 48 : 0);
    return;
  }

  // Setup threshold reached: flash white.
  const unsigned long phase = (heldMs - BUTTON_SETUP_HOLD_MS) % 180UL;
  if (phase < 110UL) {
    setSuperMiniStatusLed(48, 48, 48);
  } else {
    setSuperMiniStatusLed(0, 48, 0);
  }
}

void signalPrintTransition() {
  if (activePinPreset != PinPreset::Esp32C6SuperMini) return;

  if (!previousIsPrinting && isPrinting) {
    // Green pulse when a print starts.
    flashSuperMiniStatusLed(0, 48, 0, 2, 140, 90);
  } else if (previousIsPrinting && !isPrinting) {
    // Blue pulse when a print finishes or leaves the printing state.
    flashSuperMiniStatusLed(0, 0, 48, 3, 120, 80);
  }
}

void applyBoardSpecificPinSetup() {
  if (activePinPreset == PinPreset::Esp32C6SuperMini) {
    // SuperMini board LEDs: GPIO15 simple LED and GPIO8 WS2812 RGB LED.
    forceSuperMiniLedsOff();
  }
}

void appendFormat(String& out, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  va_list argsCopy;
  va_copy(argsCopy, args);
  int needed = vsnprintf(nullptr, 0, fmt, argsCopy);
  va_end(argsCopy);
  if (needed <= 0) {
    va_end(args);
    return;
  }

  char* buffer = new char[needed + 1];
  vsnprintf(buffer, needed + 1, fmt, args);
  va_end(args);
  out += buffer;
  delete[] buffer;
}

// Escape a stored value for use inside a value="..." attribute.
static String htmlAttr(const char* s) {
  String out;
  for (; *s; s++) {
    switch (*s) {
      case '"': out += "&quot;"; break;
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      default:  out += *s; break;
    }
  }
  return out;
}

/* --------------------------------------------------
   HTML PAGE (stored in flash to save RAM)
   -------------------------------------------------- */

String buildPage()
{
  String options;
  options.reserve(3072);
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n && i < 48; i++)
  {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    options += "<option value='";
    options += ssid;
    options += "'>";
    options += ssid;
    options += "</option>";
  }

  String pinPresetOptions;
  pinPresetOptions.reserve(768);
  const PinPreset presets[] = {
    PinPreset::Esp32Waveshare,
    PinPreset::Esp32Default,
    PinPreset::Esp32C6Default,
    PinPreset::Esp32C6SuperMini,
    PinPreset::Custom
  };
  for (PinPreset preset : presets) {
    pinPresetOptions += "<option value='";
    pinPresetOptions += pinPresetValue(preset);
    pinPresetOptions += "'";
    if (preset == activePinPreset) pinPresetOptions += " selected";
    pinPresetOptions += ">";
    pinPresetOptions += pinPresetLabel(preset);
    pinPresetOptions += "</option>";
  }

  String pinPresetJs;
  pinPresetJs.reserve(1400);
  appendFormat(pinPresetJs, R"rawliteral(
const presetPins = {
  "esp32_waveshare": { epdCs:%d, epdDc:%d, epdRst:%d, epdBusy:%d, epdSck:%d, epdMosi:%d, displayPower:%d, battery:%d, demoButton:%d },
  "esp32": { epdCs:%d, epdDc:%d, epdRst:%d, epdBusy:%d, epdSck:%d, epdMosi:%d, displayPower:%d, battery:%d, demoButton:%d },
  "esp32_c6": { epdCs:%d, epdDc:%d, epdRst:%d, epdBusy:%d, epdSck:%d, epdMosi:%d, displayPower:%d, battery:%d, demoButton:%d },
  "esp32_c6_supermini": { epdCs:%d, epdDc:%d, epdRst:%d, epdBusy:%d, epdSck:%d, epdMosi:%d, displayPower:%d, battery:%d, demoButton:%d }
};
let previousPinPreset = "%s";
  )rawliteral",
    makePinPreset(PinPreset::Esp32Waveshare).epdCs,
    makePinPreset(PinPreset::Esp32Waveshare).epdDc,
    makePinPreset(PinPreset::Esp32Waveshare).epdRst,
    makePinPreset(PinPreset::Esp32Waveshare).epdBusy,
    makePinPreset(PinPreset::Esp32Waveshare).epdSck,
    makePinPreset(PinPreset::Esp32Waveshare).epdMosi,
    makePinPreset(PinPreset::Esp32Waveshare).displayPower,
    makePinPreset(PinPreset::Esp32Waveshare).battery,
    makePinPreset(PinPreset::Esp32Waveshare).demoButton,
    makePinPreset(PinPreset::Esp32Default).epdCs,
    makePinPreset(PinPreset::Esp32Default).epdDc,
    makePinPreset(PinPreset::Esp32Default).epdRst,
    makePinPreset(PinPreset::Esp32Default).epdBusy,
    makePinPreset(PinPreset::Esp32Default).epdSck,
    makePinPreset(PinPreset::Esp32Default).epdMosi,
    makePinPreset(PinPreset::Esp32Default).displayPower,
    makePinPreset(PinPreset::Esp32Default).battery,
    makePinPreset(PinPreset::Esp32Default).demoButton,
    makePinPreset(PinPreset::Esp32C6Default).epdCs,
    makePinPreset(PinPreset::Esp32C6Default).epdDc,
    makePinPreset(PinPreset::Esp32C6Default).epdRst,
    makePinPreset(PinPreset::Esp32C6Default).epdBusy,
    makePinPreset(PinPreset::Esp32C6Default).epdSck,
    makePinPreset(PinPreset::Esp32C6Default).epdMosi,
    makePinPreset(PinPreset::Esp32C6Default).displayPower,
    makePinPreset(PinPreset::Esp32C6Default).battery,
    makePinPreset(PinPreset::Esp32C6Default).demoButton,
    makePinPreset(PinPreset::Esp32C6SuperMini).epdCs,
    makePinPreset(PinPreset::Esp32C6SuperMini).epdDc,
    makePinPreset(PinPreset::Esp32C6SuperMini).epdRst,
    makePinPreset(PinPreset::Esp32C6SuperMini).epdBusy,
    makePinPreset(PinPreset::Esp32C6SuperMini).epdSck,
    makePinPreset(PinPreset::Esp32C6SuperMini).epdMosi,
    makePinPreset(PinPreset::Esp32C6SuperMini).displayPower,
    makePinPreset(PinPreset::Esp32C6SuperMini).battery,
    makePinPreset(PinPreset::Esp32C6SuperMini).demoButton,
    pinPresetValue(activePinPreset)
  );

  char displayPowerValue[12];
  char batteryValue[12];
  char demoButtonValue[12];
  pinValueToText(customPins.displayPower, displayPowerValue, sizeof(displayPowerValue));
  pinValueToText(customPins.battery, batteryValue, sizeof(batteryValue));
  pinValueToText(customPins.demoButton, demoButtonValue, sizeof(demoButtonValue));

  String customPinInputs;
  customPinInputs.reserve(2200);
  appendFormat(customPinInputs, R"rawliteral(
    <div id="customPinFields" class="pin-fields" style="display:%s;">
      <div class="note">Custom mode lets you override every display-related pin. Leave optional fields empty to disable them.</div>

      <label>EPD CS</label>
      <input type="number" name="pin_epd_cs" value="%d" placeholder="Chip select">

      <label>EPD DC</label>
      <input type="number" name="pin_epd_dc" value="%d" placeholder="Data/command">

      <label>EPD RST</label>
      <input type="number" name="pin_epd_rst" value="%d" placeholder="Reset">

      <label>EPD BUSY</label>
      <input type="number" name="pin_epd_busy" value="%d" placeholder="Busy">

      <label>EPD SCK</label>
      <input type="number" name="pin_epd_sck" value="%d" placeholder="SPI clock">

      <label>EPD MOSI</label>
      <input type="number" name="pin_epd_mosi" value="%d" placeholder="SPI MOSI">

      <label>Display Power Pin (optional)</label>
      <input type="number" name="pin_display_power" value="%s" placeholder="Leave empty if unused">

      <label>Battery Pin (optional)</label>
      <input type="number" name="pin_battery" value="%s" placeholder="Leave empty if unused">

      <label>Button Pin (optional)</label>
      <input type="number" name="pin_demo_button" value="%s" placeholder="Leave empty if unused">
    </div>
  )rawliteral",
    activePinPreset == PinPreset::Custom ? "block" : "none",
    customPins.epdCs,
    customPins.epdDc,
    customPins.epdRst,
    customPins.epdBusy,
    customPins.epdSck,
    customPins.epdMosi,
    displayPowerValue,
    batteryValue,
    demoButtonValue
  );

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Calendar Setup</title>

<style>
body{
  font-family: Arial, Helvetica, sans-serif;
  background:#f4f6f8;
  margin:0;
}

.container{
  max-width:420px;
  margin:40px auto;
  background:white;
  padding:25px;
  border-radius:12px;
  box-shadow:0 6px 18px rgba(0,0,0,0.1);
}

h2{
  text-align:center;
  margin-bottom:25px;
  color:#333;
}

label{
  font-weight:600;
  font-size:14px;
}

input, select{
  width:100%;
  padding:10px;
  margin-top:6px;
  margin-bottom:18px;
  border:1px solid #ccc;
  border-radius:8px;
  font-size:14px;
}

button{
  width:100%;
  padding:12px;
  border:none;
  border-radius:10px;
  background:#007bff;
  color:white;
  font-size:16px;
  font-weight:bold;
  cursor:pointer;
}

button:hover{
  background:#0056b3;
}

.scanBtn{
  background:#28a745;
  margin-bottom:15px;
}

.footer{
  text-align:center;
  margin-top:15px;
  font-size:12px;
  color:#777;
}

.note{
  margin:14px 0;
  padding:10px 12px;
  border-radius:10px;
  background:#eef6ff;
  color:#335;
  font-size:13px;
  line-height:1.4;
}

.pin-fields{
  margin-top:8px;
  border-top:1px solid #e4e9ee;
  padding-top:14px;
}
</style>

<script>
  )rawliteral";

html += pinPresetJs;

html += R"rawliteral(
function setPinField(name, value){
  const field = document.getElementsByName(name)[0];
  if(!field) return;
  field.value = value >= 0 ? value : "";
}

function applyPresetValues(presetKey){
  const pins = presetPins[presetKey];
  if(!pins) return;
  setPinField("pin_epd_cs", pins.epdCs);
  setPinField("pin_epd_dc", pins.epdDc);
  setPinField("pin_epd_rst", pins.epdRst);
  setPinField("pin_epd_busy", pins.epdBusy);
  setPinField("pin_epd_sck", pins.epdSck);
  setPinField("pin_epd_mosi", pins.epdMosi);
  setPinField("pin_display_power", pins.displayPower);
  setPinField("pin_battery", pins.battery);
  setPinField("pin_demo_button", pins.demoButton);
}

function toggleCustomPins(){
  const preset = document.getElementById("pin_preset").value;
  const customPins = document.getElementById("customPinFields");
  customPins.style.display = preset === "custom" ? "block" : "none";
  if(preset === "custom" && previousPinPreset !== "custom"){
    applyPresetValues(previousPinPreset);
  }
  previousPinPreset = preset;
}

function validateForm(){
  const ssid = document.getElementById("wifi_ssid").value;
  if(ssid.length === 0){
    alert("WiFi SSID cannot be empty");
    return false;
  }
  if(document.getElementById("pin_preset").value === "custom"){
    const required = ["pin_epd_cs","pin_epd_dc","pin_epd_rst","pin_epd_busy","pin_epd_sck","pin_epd_mosi"];
    for(const fieldId of required){
      const field = document.getElementsByName(fieldId)[0];
      if(!field || field.value.trim().length === 0){
        alert("Please fill all required custom display pins.");
        return false;
      }
    }
  }
  return true;
}
</script>
</head>

<body>

<div class="container">
  <h2>ESP32 Calendar Setup</h2>

  <form action="/save" method="POST" onsubmit="return validateForm();">

    <label>Available WiFi Networks</label>
    <select id="wifi_ssid" name="wifi_ssid">
)rawliteral";

html += options;

html += R"rawliteral(
    </select>

    <button class="scanBtn" type="button" onclick="location.reload()">Scan Again</button>

    <label>WiFi Password</label>
    <input  name="wifi_pass" placeholder="Leave empty to keep the saved password">

    <label>Timezone</label>
    <input name="device_timezone" value=")rawliteral";

html += device_timezone;

html += R"rawliteral(" placeholder="EST5EDT,M3.2.0/2,M11.1.0/2">
    <div class="note">Used for the clock when the layout does not provide a timezone in the clock widget Extra1 field.</div>

    <hr>

    <label>Weather Location</label>
    <input name="weather_location" value=")rawliteral";

html += htmlAttr(weather_location);

html += R"rawliteral(" placeholder="London, GB   or   51.51,-0.13">
    <div class="note">City name (optionally followed by a 2-letter country code) or latitude,longitude. Forecast from Open-Meteo.</div>

    <label>Stock Symbols</label>
    <input name="stock_symbols" value=")rawliteral";

html += htmlAttr(stock_symbols);

html += R"rawliteral(" placeholder="AAPL,NVDA,^IXIC">
    <div class="note">Up to 5 Yahoo Finance symbols, comma separated.</div>

    <label>Calendar iCal URLs (up to 3, merged)</label>
)rawliteral";

for (int i = 0; i < ICS_MAX_FEEDS; i++) {
  html += "    <input name=\"";
  html += ICS_PREF_KEYS[i];
  html += "\" value=\"";
  html += htmlAttr(ics_urls[i]);
  html += "\" placeholder=\"https://calendar.google.com/calendar/ical/.../basic.ics\">\n";
}

html += R"rawliteral(
    <div class="note">Google Calendar: Settings &rarr; your calendar &rarr; Integrate calendar &rarr; public or secret address in iCal format.</div>

    <hr>

    <label>Printer Name</label>
    <input name="printer_name" value=")rawliteral";

html += htmlAttr(printer_name);

html += R"rawliteral(" placeholder="Shown as the printer widget title">

    <label>Bambu Printer IP</label>
    <input name="mqtt_ip" value=")rawliteral";

html += htmlAttr(mqtt_ip);

html += R"rawliteral(" placeholder="192.168.1.100">

    <label>Bambu MQTT Port</label>
    <input name="mqtt_port" value=")rawliteral";

html += String(mqtt_port ? mqtt_port : 8883);

html += R"rawliteral(" placeholder="8883">

    <label>Bambu Printer Serial Number</label>
    <input name="mqtt_sn" value=")rawliteral";

html += htmlAttr(mqtt_sn);

html += R"rawliteral(" placeholder="Serial number (printer Settings &rarr; Device)">

    <label>Bambu LAN Access Code</label>
    <input name="mqtt_pass" placeholder="Leave empty to keep the saved code">
    <div class="note">Printer screen: Settings &rarr; WLAN / Network &rarr; Access Code. Leave the IP empty to hide the printer widget.</div>

    <label>MakerWorld User ID</label>
    <input name="makerworld_uid" value=")rawliteral";

html += htmlAttr(makerworld_uid);

html += R"rawliteral(" placeholder="Digits from your MakerWorld profile URL">
    <div class="note">Shown in the printer area when no print is running (and 5 minutes after a print ends). Your profile URL looks like makerworld.com/en/@name &mdash; the numeric ID is in the URL of your published models list.</div>

    <hr>

    <label>PKGE API Key (package tracking)</label>
    <input name="pkge_api_key" placeholder=")rawliteral";

html += pkge_api_key[0] ? "Saved - leave empty to keep, - to remove" : "Paste the API key of your PKGE Business account";

html += R"rawliteral(">
    <div class="note">Add your packages on business.pkge.net; the dashboard shows the ones still on their way (and those delivered in the last day).</div>

    <label>AeroDataBox API Key (live flight status)</label>
    <input name="aerodatabox_key" placeholder=")rawliteral";

html += aerodatabox_key[0] ? "Saved - leave empty to keep, - to remove" : "Your X-RapidAPI-Key (optional)";

html += R"rawliteral(">
    <div class="note">Optional. On the day of a flight in your calendar, shows delays, gate and departed/landed. Free key: rapidapi.com &rarr; AeroDataBox &rarr; Basic plan.</div>

    <hr>

    <label>Pin Preset</label>
    <select id="pin_preset" name="pin_preset" onchange="toggleCustomPins()">
)rawliteral";

html += pinPresetOptions;

html += R"rawliteral(
    </select>
)rawliteral";

html += customPinInputs;

html += R"rawliteral(
)rawliteral";

#if USE_ZIGBEE
  html += R"rawliteral(
    <div class="note">
      Zigbee automation follows the Bambu aux fan: switch ON when the aux fan is ON, switch OFF when the aux fan is OFF.
      Because the printer data is refreshed periodically, the OFF action can happen about 1 to 5 minutes after the fan stops.
    </div>
    <label style="display:flex;align-items:center;gap:10px;margin-bottom:14px;">
      <input type="checkbox" name="zigbee_enable" value="1" style="width:auto;margin:0;"
)rawliteral";
  html += zigbee_enable ? " checked" : "";
  html += R"rawliteral(>
      Enable Zigbee automation
    </label>
)rawliteral";
  if (zigbee_enable) {
    html += R"rawliteral(<div class="note">)rawliteral";
    if (zigbee_linked) {
      html += "Zigbee status: linked. The dashboard will reuse the saved binding.";
    } else if (zigbee_pairing_failed) {
      html += "Zigbee status: not linked. Pairing timed out previously. Wake the device and press the Zigbee pairing button; pairing will stay open for up to 60 seconds.";
    } else {
      html += "Zigbee status: waiting for first link. Wake the device and press the Zigbee pairing button; pairing will stay open for up to 60 seconds.";
    }
    html += R"rawliteral(</div>)rawliteral";
  }
#endif

html += R"rawliteral(
    <button type="submit">Save Configuration</button>

  </form>

  <div class="footer">
    Device will restart after saving
  </div>
</div>

</body>
</html>
)rawliteral";

  return html;
}

namespace {
alignas(DashboardDisplay) uint8_t displayStorage[sizeof(DashboardDisplay)];
DashboardDisplay* displayInstance = nullptr;
}

DashboardDisplay& getDisplay() {
  if (displayInstance == nullptr) {
    displayInstance = new (displayStorage) DashboardDisplay(
      DashboardPanel(activePins.epdCs, activePins.epdDc, activePins.epdRst, activePins.epdBusy));
  }
  return *displayInstance;
}

const GFXglyph* findGlyph(const SparseGFXfont* font, uint32_t code) {
  int low = 0;
  int high = font->glyphCount - 1;

  while (low <= high) {
    int mid = (low + high) / 2;
    uint32_t val = pgm_read_dword(&font->charMap[mid]);

    if (val == code) return &font->glyph[mid];
    if (val < code) low = mid + 1;
    else high = mid - 1;
  }
  return nullptr;
}
// Read next Unicode codepoint from UTF-8 string.
// Returns a codepoint and advances `*str`.
uint32_t utf8_next(const char** str) {
  const unsigned char* s = (const unsigned char*)(*str);
  // if (!s[1]) return 0;

  uint32_t codepoint = 0;

  if (*s < 0x80) {  // 1 byte
    codepoint = *s;
    (*str)++;
  } else if ((*s & 0xE0) == 0xC0) {  // 2 bytes
    codepoint = s[0] & 0x1F;
    codepoint <<= 6;
    codepoint |= s[1] & 0x3F;
    (*str) += 2;
  } else if ((*s & 0xF0) == 0xE0) {  // 3 bytes
    codepoint = s[0] & 0x0F;
    codepoint <<= 6;
    codepoint |= s[1] & 0x3F;
    codepoint <<= 6;
    codepoint |= s[2] & 0x3F;
    (*str) += 3;
  } else if ((*s & 0xF8) == 0xF0) {  // 4 bytes
    codepoint = s[0] & 0x07;
    codepoint <<= 6;
    codepoint |= s[1] & 0x3F;
    codepoint <<= 6;
    codepoint |= s[2] & 0x3F;
    codepoint <<= 6;
    codepoint |= s[3] & 0x3F;
    (*str) += 4;
  }
  return codepoint;
}

// Draw a single character from any SparseGFXfont.
// `x,y` are baseline-ish coordinates (matches Adafruit_GFX glyph conventions).
void drawSparseChar(const SparseGFXfont* font, int16_t x, int16_t y, uint32_t code, uint16_t color) {
  const GFXglyph* glyph = findGlyph(font, code);
  if (!glyph) return;

  uint16_t bo = pgm_read_word(&glyph->bitmapOffset);
  uint8_t w = pgm_read_byte(&glyph->width);
  uint8_t h = pgm_read_byte(&glyph->height);
  int8_t xo = pgm_read_byte(&glyph->xOffset);
  int8_t yo = pgm_read_byte(&glyph->yOffset);

  const uint8_t* bitmap = font->bitmap;
  uint8_t rowBytes = (w + 7) / 8;

  for (uint8_t yy = 0; yy < h; yy++) {
    for (uint8_t xx = 0; xx < w; xx++) {
      if (pgm_read_byte(&bitmap[bo + yy * rowBytes + xx / 8]) & (0x80 >> (xx & 7))) {
        display.drawPixel(x + xo + xx, y + yo + yy, color);
      }
    }
  }
}

void drawSparseCharCenter(const SparseGFXfont* font, int16_t x, int16_t yCenter, uint32_t code, uint16_t color) {
  const GFXglyph* glyph = findGlyph(font, code);
  if (!glyph) return;

  uint16_t bo = pgm_read_word(&glyph->bitmapOffset);
  uint8_t w = pgm_read_byte(&glyph->width);
  uint8_t h = pgm_read_byte(&glyph->height);
  int8_t xo = pgm_read_byte(&glyph->xOffset);
  int8_t yo = pgm_read_byte(&glyph->yOffset);

  const uint8_t* bitmap = font->bitmap;
  uint8_t rowBytes = (w + 7) / 8;

  // Shift baseline so glyph center = yCenter
  int16_t baselineY = yCenter - (yo + h / 2);

  for (uint8_t yy = 0; yy < h; yy++) {
    for (uint8_t xx = 0; xx < w; xx++) {
      if (pgm_read_byte(&bitmap[bo + yy * rowBytes + xx / 8]) & (0x80 >> (xx & 7))) {
        display.drawPixel(x + xo + xx, baselineY + yo + yy, color);
      }
    }
  }
}

int16_t getSparseStringWidth(const SparseGFXfont* font, const char* str) {
  int16_t width = 0;

  const char* p = str;
  while (*p) {
    uint32_t code = utf8_next(&p);
    const GFXglyph* glyph = findGlyph(font, code);

    if (glyph) {
      width += pgm_read_byte(&glyph->xAdvance);
    } else {
      width += font->yAdvance / 2;
    }
  }

  return width;
}

void drawSparseString(const SparseGFXfont* font, int16_t x, int16_t y, const char* str, uint16_t color) {
  int16_t cursorX = x;
  int16_t cursorY = y;

  while (*str) {
    uint32_t code = utf8_next(&str);  // decode next Unicode codepoint
    drawSparseChar(font, cursorX, cursorY, code, color);

    const GFXglyph* glyph = findGlyph(font, code);
    if (glyph) {
      cursorX += pgm_read_byte(&glyph->xAdvance);
    } else {
      cursorX += font->yAdvance / 2;
    }
  }
}

void drawSparseStringCentered(const SparseGFXfont* font,
                              int16_t centerX, int16_t y, const char* str, uint16_t color) {

  int16_t textWidth = getSparseStringWidth(font, str);
  int16_t startX = centerX - (textWidth / 2);

  drawSparseString(font, startX, y, str, color);
}

// NTP sync. Timezone is set per-widget (clock) via TZ strings.
void initTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

#if USE_ZIGBEE
static void saveZigbeeStatus() {
  preferences.begin("config", false);
  preferences.putBool("zigbee_linked", zigbee_linked);
  preferences.putBool("zigbee_pairing_failed", zigbee_pairing_failed);
  preferences.end();
}
#endif


// Clock widget: uses `infoClock->Extra1` as TZ string (e.g. "EST5EDT,...").
void updateClock(LayoutItem* infoClock) {
  if (!infoClock) return;
  const char* tzValue = infoClock->Extra1[0] != '\0'
    ? infoClock->Extra1
    : (device_timezone[0] != '\0' ? device_timezone : "UTC0");
  setenv("TZ", tzValue, 1);

  tzset();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    DBG_ERROR("Failed to obtain time");
    return;
  }

  char timeStr[16];
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);

  display.fillRect(infoClock->PosX, infoClock->PosY, infoClock->Width, infoClock->Height, GxEPD_WHITE);

  int centerX = infoClock->PosX + infoClock->Width / 2;

  drawSparseStringCentered(&HighSpeed_130, centerX, infoClock->PosY + 110, timeStr, GxEPD_BLACK);
}


// =======================
// Battery Read
// =======================
float readBattery() {
  if (!hasBatteryPin()) return 0.0f;
  analogReadResolution(12);
  // analogReadMilliVolts applies the chip's factory ADC calibration; average out the noise.
  uint32_t mv = 0;
  for (int i = 0; i < BAT_SAMPLES; i++) mv += analogReadMilliVolts(activePins.battery);
  return mv / (float)BAT_SAMPLES / 1000.0f * BAT_DIVIDER * BAT_CALIBRATION;
}

// Charge (0-100) from the battery voltage, interpolated on BAT_CURVE.
static int batteryPercent(float v) {
  const int n = sizeof(BAT_CURVE) / sizeof(BAT_CURVE[0]);
  if (v >= BAT_CURVE[0].v) return 100;
  for (int i = 1; i < n; i++) {
    if (v >= BAT_CURVE[i].v) {
      const BatPoint& hi = BAT_CURVE[i - 1];
      const BatPoint& lo = BAT_CURVE[i];
      return lo.pct + (int)((v - lo.v) / (hi.v - lo.v) * (hi.pct - lo.pct) + 0.5f);
    }
  }
  return 0;
}

// Alert icon followed by the names of the failing sources, e.g. "[!] WiFi Weather".
// Names that don't fit before maxX are summarized as "+N".
static void drawProblems(int16_t x, int16_t y, int16_t maxX) {
  problemsShown = problems;
  if (!problems) return;

  drawSparseChar(&MDI_22_Sparse, x, y, MDI_ALERT, ALERT_COLOR);
  x += 24;

  const int count = sizeof(PROBLEM_LABELS) / sizeof(PROBLEM_LABELS[0]);
  int remaining = __builtin_popcount(problems);
  for (int i = 0; i < count; i++) {
    if (!(problems & (1 << i))) continue;
    int16_t w = getSparseStringWidth(&epaperFont, PROBLEM_LABELS[i]);
    char more[8];
    snprintf(more, sizeof(more), "+%d", remaining);
    // Keep room for a "+N" summary unless this is the last name.
    int16_t reserve = remaining > 1 ? getSparseStringWidth(&epaperFont, more) + 6 : 0;
    if (x + w + reserve > maxX) {
      drawSparseString(&epaperFont, x, y, more, ALERT_COLOR);
      return;
    }
    drawSparseString(&epaperFont, x, y, PROBLEM_LABELS[i], ALERT_COLOR);
    x += w + 8;
    remaining--;
  }
}

void drawStatus(LayoutItem* item) {
  DBG(F("Battery Draw"));

  float voltage = 0.0f;
  char buf[20] = "";
  if (hasBatteryPin()) {
    voltage = readBattery();
    snprintf(buf, sizeof(buf), "%.2f V", voltage);
  }

  char versionBuf[32];
  snprintf(versionBuf, sizeof(versionBuf), "v%s", FW_VERSION);

  display.fillRect(item->PosX, item->PosY, item->Width, item->Height, GxEPD_WHITE);

  int16_t leftX = item->PosX;
  if (hasBatteryPin()) {
    drawSparseString(&epaperFont, item->PosX, item->PosY + 16, buf, GxEPD_BLACK);
    leftX += getSparseStringWidth(&epaperFont, buf) + 10;
  }

  // Icons are drawn right-to-left in fixed 22px slots so they never overlap
  // regardless of which subset is active.
  constexpr int16_t ICON_STEP = 22;
  constexpr int16_t RIGHT_PAD = 4;

  int16_t iconReserve = RIGHT_PAD;
  if (hasBatteryPin())                  iconReserve += ICON_STEP;
#if USE_ZIGBEE
  if (zigbee_enable)                    iconReserve += ICON_STEP * 2;
#endif
  if (isPrinting || previousIsPrinting) iconReserve += ICON_STEP;

  int16_t versionWidth = getSparseStringWidth(&epaperFont, versionBuf);
  int16_t versionX = item->PosX + item->Width - iconReserve - versionWidth;
  int16_t minVersionX = item->PosX + (hasBatteryPin() ? getSparseStringWidth(&epaperFont, buf) + 8 : 0);
  if (versionX < minVersionX) versionX = minVersionX;
  drawSparseString(&epaperFont, versionX, item->PosY + 16, versionBuf, GxEPD_BLACK);

  drawProblems(leftX, item->PosY + 16, versionX - 8);

  // Draw icons right to left
  int16_t ix = item->PosX + item->Width - RIGHT_PAD;

  if (hasBatteryPin()) {
    int percentage = batteryPercent(voltage);
    DBGF("[Battery] %.2f V, %d%%", voltage, percentage);
    // MDI battery-10 .. battery-90 are 0xF007A .. 0xF0082, full battery is 0xF0079.
    int tenths = constrain((percentage + 5) / 10, 1, 10);
    uint32_t charCode = tenths == 10 ? 0xF0079 : 0xF007A + (tenths - 1);
    ix -= ICON_STEP;
    drawSparseChar(&MDI_22_Sparse, ix, item->PosY + 16, charCode, GxEPD_BLACK);
  }
#if USE_ZIGBEE
  if (zigbee_enable) {
    uint32_t zigbeeStateIcon = zigbee_linked ? (s_light_state ? MDI_POWER_ON : MDI_POWER_OFF) : MDI_ALERT;
    ix -= ICON_STEP;
    drawSparseChar(&MDI_22_Sparse, ix, item->PosY + 16, zigbeeStateIcon, GxEPD_BLACK);
    ix -= ICON_STEP;
    drawSparseChar(&MDI_22_Sparse, ix, item->PosY + 16, MDI_ZIGBEE, GxEPD_BLACK);
  }
#endif
  if (isPrinting || previousIsPrinting) {
    ix -= ICON_STEP;
    drawSparseChar(&MDI_22_Sparse, ix, item->PosY + 16, MDI_PRINT_ON, GxEPD_BLACK);
  }
}

// Partial refresh for one layout region. Useful to reduce ghosting + update time.
static bool displayReady = false;
static long clockDrawnMinute = -1;

// Minutes since the epoch: changes exactly when the displayed HH:MM changes.
static long currentMinute() {
  return (long)(time(nullptr) / 60);
}

// Powers and initializes the panel once per boot.
static void initDisplay(bool initialRefresh) {
  if (displayReady) return;
  if (hasDisplayPowerPin()) {
    // Not all hats need it; IO4 powers some e-paper boards.
    pinMode(activePins.displayPower, OUTPUT);
    digitalWrite(activePins.displayPower, HIGH);
    delay(50);
  }
  SPI.begin(activePins.epdSck, -1, activePins.epdMosi, activePins.epdCs);
  display.init(115200, initialRefresh, 2, false);
  display.setRotation(0);
  displayReady = true;
}

// Waits for the SNTP client (started by initTime) to set the clock.
static bool waitForNtpSync(unsigned long timeoutMs) {
  // sntp_get_sync_status() resets COMPLETED back to RESET once read, so read it once per loop.
  unsigned long start = millis();
  bool ok = false;
  while (!(ok = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) && millis() - start < timeoutMs)
    delay(50);
  DBGF("NTP %s after %lu ms", ok ? "synced" : "NOT synced", millis() - start);
  return ok;
}

void updatePartial(LayoutItem* item, void (*drawFunc)(LayoutItem*)) {
  if (!item) return;

  display.setPartialWindow(item->PosX, item->PosY, item->Width, item->Height);
  display.firstPage();
  do {
    display.fillRect(item->PosX, item->PosY, item->Width, item->Height, GxEPD_WHITE);
    drawFunc(item);
  } while (display.nextPage());
}

// Fetch gating: "should we hit the network for this widget this boot?"
// This is intentionally separate from draw/update logic.
bool shouldFetchRefresh(LayoutItem* item) {
  if (!item || !item->Active || item->Refresh <= 0) return false;
  return (bootCount % item->Refresh) == 0;
}

void handleConfig() {
  apLastActivity = millis();  // someone is using the page: restart the timeout
  // String page = FPSTR(PAGE_HTML_START);
  // page += FPSTR(PAGE_HTML_ZIGBEE);
  // page += FPSTR(PAGE_HTML_END);

  server.send(200, "text/html", buildPage());
}

void handleSave() {
  copyToCfg(wifi_ssid, sizeof(wifi_ssid), server.arg("wifi_ssid"));
  // Empty password fields keep the saved value, so re-opening setup doesn't require retyping them.
  if (server.arg("wifi_pass").length() > 0)
    copyToCfg(wifi_pass, sizeof(wifi_pass), server.arg("wifi_pass"));
  copyToCfg(device_timezone, sizeof(device_timezone), server.arg("device_timezone"));
  copyToCfg(weather_location, sizeof(weather_location), server.arg("weather_location"));
  copyToCfg(stock_symbols, sizeof(stock_symbols), server.arg("stock_symbols"));
  for (int i = 0; i < ICS_MAX_FEEDS; i++)
    copyToCfg(ics_urls[i], sizeof(ics_urls[i]), server.arg(ICS_PREF_KEYS[i]));
  copyToCfg(makerworld_uid, sizeof(makerworld_uid), server.arg("makerworld_uid"));
  copyToCfg(printer_name, sizeof(printer_name), server.arg("printer_name"));
  // Empty keeps the saved key; a single "-" removes it (hides the tracking widget).
  if (server.arg("pkge_api_key") == "-") pkge_api_key[0] = '\0';
  else if (server.arg("pkge_api_key").length() > 0)
    copyToCfg(pkge_api_key, sizeof(pkge_api_key), server.arg("pkge_api_key"));
  if (server.arg("aerodatabox_key") == "-") aerodatabox_key[0] = '\0';
  else if (server.arg("aerodatabox_key").length() > 0)
    copyToCfg(aerodatabox_key, sizeof(aerodatabox_key), server.arg("aerodatabox_key"));
// Serial.printf("Wifi %s Pass %s", wifi_ssid, wifi_pass);

  if (server.arg("mqtt_pass").length() > 0)
    copyToCfg(mqtt_pass, sizeof(mqtt_pass), server.arg("mqtt_pass"));
  copyToCfg(mqtt_sn, sizeof(mqtt_sn), server.arg("mqtt_sn"));
  copyToCfg(mqtt_ip, sizeof(mqtt_ip), server.arg("mqtt_ip"));
  mqtt_port = server.arg("mqtt_port").toInt();
  if (mqtt_port == 0) mqtt_port = 8883;
  activePinPreset = parsePinPreset(server.arg("pin_preset"));
  customPins.epdCs = readPinField("pin_epd_cs");
  customPins.epdDc = readPinField("pin_epd_dc");
  customPins.epdRst = readPinField("pin_epd_rst");
  customPins.epdBusy = readPinField("pin_epd_busy");
  customPins.epdSck = readPinField("pin_epd_sck");
  customPins.epdMosi = readPinField("pin_epd_mosi");
  customPins.displayPower = readPinField("pin_display_power");
  customPins.battery = readPinField("pin_battery");
  customPins.demoButton = readPinField("pin_demo_button");
  if (activePinPreset == PinPreset::Custom && !hasRequiredDisplayPins(customPins)) {
    server.send(400, "text/plain", "Custom preset requires CS, DC, RST, BUSY, SCK and MOSI pins.");
    return;
  }
  applyStoredPinSelection();

  Serial.printf("MQTT P: %u SN: %s IP %s\n", mqtt_port, mqtt_sn, mqtt_ip);
#if USE_ZIGBEE
  zigbee_enable = server.hasArg("zigbee_enable");
  zigbee_wait = server.hasArg("zigbee_wait");

  if (!zigbee_enable) {
    zigbee_linked = false;
    zigbee_pairing_failed = false;
  }

  copyToCfg(zigbee_monitor, sizeof(zigbee_monitor), server.arg("zigbee_monitor"));
  zigbee_monitor_ep = server.arg("zigbee_monitor_ep").toInt();

  copyToCfg(zigbee_control, sizeof(zigbee_control), server.arg("zigbee_control"));
  zigbee_control_ep = server.arg("zigbee_control_ep").toInt();
#endif

  preferences.begin("config", false);

  preferences.putString("wifi_ssid", wifi_ssid);
  preferences.putString("wifi_pass", wifi_pass);
  preferences.putString("device_timezone", device_timezone);
  preferences.putString("weather_loc", weather_location);
  preferences.putString("stock_symbols", stock_symbols);
  for (int i = 0; i < ICS_MAX_FEEDS; i++)
    preferences.putString(ICS_PREF_KEYS[i], ics_urls[i]);
  preferences.putString("makerworld_uid", makerworld_uid);
  preferences.putString("printer_name", printer_name);
  preferences.putString("pkge_key", pkge_api_key);
  preferences.putString("adb_key", aerodatabox_key);

  preferences.putString("mqtt_pass", mqtt_pass);
  preferences.putString("mqtt_sn", mqtt_sn);
  preferences.putString("mqtt_ip", mqtt_ip);
  preferences.putUInt("mqtt_port", mqtt_port);
  preferences.putString("pin_preset", pinPresetValue(activePinPreset));
  saveCustomPinConfig(preferences, customPins);

#if USE_ZIGBEE
  preferences.putBool("zigbee_enable", zigbee_enable);
  preferences.putBool("zigbee_wait", zigbee_wait);
  preferences.putBool("zigbee_linked", zigbee_linked);
  preferences.putBool("zigbee_pairing_failed", zigbee_pairing_failed);

  preferences.putString("zigbee_monitor", zigbee_monitor);
  preferences.putUInt("zigbee_monitor_ep", zigbee_monitor_ep);

  preferences.putString("zigbee_control", zigbee_control);
  preferences.putUInt("zigbee_control_ep", zigbee_control_ep);
#endif
  preferences.end();

server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Saved</title>

<style>
body{
  margin:0;
  font-family: Arial, Helvetica, sans-serif;
  display:flex;
  justify-content:center;
  align-items:center;
  height:100vh;
  color:black;
}

.card{
  background: rgba(255,255,255,0.1);
  backdrop-filter: blur(10px);
  padding:30px;
  border-radius:16px;
  text-align:center;
  box-shadow:0 8px 25px rgba(0,0,0,0.2);
  max-width:320px;
}

h2{
  margin-bottom:10px;
}

p{
  margin-top:5px;
  font-size:14px;
  opacity:0.9;
}

.spinner{
  margin:20px auto;
  width:40px;
  height:40px;
  border:4px solid rgba(255,255,255,0.3);
  border-top:4px solid white;
  border-radius:50%;
  animation: spin 1s linear infinite;
}

@keyframes spin{
  100% { transform: rotate(360deg); }
}

.countdown{
  margin-top:10px;
  font-size:13px;
  opacity:0.8;
}
</style>

<script>
let seconds = 3;
function updateCountdown(){
  document.getElementById("count").innerText = seconds;
  if(seconds > 0){
    seconds--;
    setTimeout(updateCountdown, 1000);
  }
}
window.onload = updateCountdown;
</script>
</head>

<body>

<div class="card">
  <h2>✅ Configuration Saved</h2>
  <p>Your device will reboot shortly</p>

  <div class="spinner"></div>

  <div class="countdown">
    Rebooting in <span id="count">3</span> seconds...
  </div>
</div>

</body>
</html>
)rawliteral");

delay(3000);
ESP.restart();

}


void startAP() {
  apModeActive = true;
  apLastActivity = millis();

  Serial.println("Start AP");

  bool result = WiFi.softAP(AP_SSID);

  if (result) {
    Serial.println("AP started OK");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("AP failed!");
  }

  Serial.println("AP Started. Connect and go to 192.168.4.1");

  // Captive portal: answer every DNS lookup with our IP so phones/laptops pop up the setup page.
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", handleConfig);
  server.on("/save", HTTP_POST, handleSave);
  // OS connectivity checks (Android /generate_204, Apple /hotspot-detect.html, Windows /connecttest.txt)
  // and any other URL get redirected to the setup page.
  server.onNotFound([]() {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();

  DBG(F("AP and config server started"));

  // display.display(true);  // full refresh
  if (hasDisplayPowerPin()) {
    pinMode(activePins.displayPower, OUTPUT);
    digitalWrite(activePins.displayPower, HIGH);
    delay(50);
  }

  SPI.begin(activePins.epdSck, -1, activePins.epdMosi, activePins.epdCs);
  display.init(115200, true, 2, false);  // Full refresh
  display.setRotation(0);
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);

  display.firstPage();
  do {
    drawSparseStringCentered(&epaperFont, 400, 175, "ESP32 AP Mode", GxEPD_BLACK);
    drawSparseStringCentered(&epaperFont, 400, 275, "Connect to " AP_SSID " and go to", GxEPD_BLACK);
    drawSparseStringCentered(&epaperFont, 400, 325, "http://192.168.4.1", GxEPD_BLACK);
    drawSparseStringCentered(&epaperFont, 400, 375, "For the first configuration!", GxEPD_BLACK);
  } while (display.nextPage());

  DBG(F("Display configured"));
  display.hibernate();
}


constexpr int BUTTON_ACTION_NONE = 0;
constexpr int BUTTON_ACTION_AP = 2;
constexpr int BUTTON_ACTION_REBOOT = 3;

int readWakeButtonAction() {
  if (!hasDemoButtonPin()) {
    return BUTTON_ACTION_NONE;
  }

  pinMode(activePins.demoButton, INPUT_PULLUP);
  DBGF("[Button] GPIO%d is %s at %lu ms", activePins.demoButton,
       digitalRead(activePins.demoButton) == HIGH ? "HIGH (pressed)" : "LOW", millis());
  if (digitalRead(activePins.demoButton) != HIGH) {
    return BUTTON_ACTION_NONE;
  }

  // After a button wake the press began at wake time, so count the boot time as held time.
  unsigned long pressStart = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1 ? 0 : millis();
  while (digitalRead(activePins.demoButton) == HIGH) {
    unsigned long heldMs = millis() - pressStart;
    updateSuperMiniHoldFeedback(heldMs);
    if (heldMs >= BUTTON_SETUP_HOLD_MS) {
      Serial.println("Button held -> AP config mode");
      forceSuperMiniLedsOff();
      flashSuperMiniStatusLed(48, 0, 0, 2, 90, 70);
      return BUTTON_ACTION_AP;
    }
    delay(10);
  }

  forceSuperMiniLedsOff();

  if (millis() - pressStart >= BUTTON_REBOOT_HOLD_MS) {
    Serial.println("Button held -> reboot");
    flashSuperMiniStatusLed(0, 0, 48, 2, 100, 70);
    return BUTTON_ACTION_REBOOT;
  }
  return BUTTON_ACTION_NONE;
}

// A 4 s hold while awake restarts the board into setup mode. RTC_DATA_ATTR is reset by a
// restart, so the request lives in no-init RTC memory, guarded by a magic value.
constexpr uint32_t SETUP_REQUEST_MAGIC = 0x5E7AB0A7;
RTC_NOINIT_ATTR uint32_t setupRequest;

// Watches the button while the dashboard is awake (WiFi, fetching, drawing), so a hold works at
// any time, not only from deep sleep. A short tap is ignored: the dashboard is already refreshing.
static void buttonWatchTask(void*) {
  bool pressed = false;
  unsigned long pressStart = 0;
  int lowPolls = 0;
  for (;;) {
    bool high = digitalRead(activePins.demoButton) == HIGH;
    unsigned long now = millis();
    if (high) {
      lowPolls = 0;
      if (!pressed) {
        pressed = true;
        pressStart = now;
      }
      unsigned long heldMs = now - pressStart;
      updateSuperMiniHoldFeedback(heldMs);
      if (heldMs >= BUTTON_SETUP_HOLD_MS) {
        Serial.println("Button held -> AP config mode");
        forceSuperMiniLedsOff();
        flashSuperMiniStatusLed(48, 0, 0, 2, 90, 70);
        setupRequest = SETUP_REQUEST_MAGIC;
        ESP.restart();
      }
    } else if (pressed && ++lowPolls >= 3) {  // released for 60 ms (debounce)
      pressed = false;
      forceSuperMiniLedsOff();
      if (now - pressStart >= BUTTON_REBOOT_HOLD_MS) {
        Serial.println("Button held -> reboot");
        flashSuperMiniStatusLed(0, 0, 48, 2, 100, 70);
        ESP.restart();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

bool startWiFiReliable(const char* ssid, const char* password)
{
  DBG(F("startWiFiReliable called"));

  for (int attempt = 1; attempt <= 2; attempt++)
  {
    DBGF("\nWiFi attempt %d", attempt);

    // FULL STOP FIRST
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(500);

    // THEN re-init cleanly
    WiFi.mode(WIFI_STA);
    delay(100);

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);   // IMPORTANT: disable during manual connect
    WiFi.setSleep(false);

    delay(500);

    WiFi.begin(ssid, password);

    unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED)
    {
      if (millis() - start > 20000)
        break;

      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      DBG(F("\nWiFi connected!"));
      DBG(WiFi.localIP());

      WiFi.setAutoReconnect(true); // enable AFTER connection
      return true;
    }

    DBG(F("\nFailed. Retrying..."));
  }

  // Final cleanup
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);

  return false;
}

// bool startWiFiReliable(const char* ssid, const char* password)
// {
//   Serial.println("startWiFiReliable called");

//   WiFi.mode(WIFI_OFF);
//   delay(500);
//   WiFi.mode(WIFI_STA);
//   delay(300);

//   // Force WPA2 only, disable WPA3/PMF entirely
//   wifi_config_t wifi_cfg = {};
//   strncpy((char*)wifi_cfg.sta.ssid,     ssid,     sizeof(wifi_cfg.sta.ssid)     - 1);
//   strncpy((char*)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);

//   wifi_cfg.sta.threshold.authmode  = WIFI_AUTH_WPA2_PSK;
//   wifi_cfg.sta.pmf_cfg.capable     = false;
//   wifi_cfg.sta.pmf_cfg.required    = false;
//   wifi_cfg.sta.sae_pwe_h2e         = WPA3_SAE_PWE_UNSPECIFIED; // disable SAE/WPA3

//   // Force 802.11 b/g/n only — skip WiFi 6 (ax) negotiation
//   esp_wifi_set_protocol(WIFI_IF_STA,
//     WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

//   esp_wifi_set_ps(WIFI_PS_NONE);
//   esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);

//   // Static IP — keeps DHCP out of the equation
//   IPAddress local_IP(192, 168, 50, 20);
//   IPAddress gateway(192, 168, 50, 1);
//   IPAddress subnet(255, 255, 255, 0);
//   IPAddress dns(8, 8, 8, 8);
//   WiFi.config(local_IP, gateway, subnet, dns);

//   WiFi.persistent(false);
//   WiFi.begin(ssid, password);

//   unsigned long start = millis();
//   while (WiFi.status() != WL_CONNECTED)
//   {
//     if (millis() - start > 20000) break;
//     delay(500);
//     Serial.printf(" [%d]", WiFi.status());
//   }

//   Serial.printf("\nFinal status: %d\n", WiFi.status());

//   if (WiFi.status() == WL_CONNECTED)
//   {
//     Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
//     return true;
//   }

//   WiFi.mode(WIFI_OFF);
//   return false;
// }

// =======================
// Setup
// =======================
void setup() {
  Serial.begin(115200);
#if DEBUG
  // After a reset / flash (not a timer wake) give the USB serial monitor time to attach.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) delay(3000);
#endif
#if CONFIG_IDF_TARGET_ESP32
  delay(2000);
#endif

  if (activePinPreset == PinPreset::Esp32C6SuperMini) {
    forceSuperMiniLedsOff();
  }

  fullRefresh = (bootCount % (60 * 24) == 0);


  int wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Wakeup caused by external signal using RTC_CNTL"); fullRefresh=true; bootCount=0; break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println("Wakeup caused by ULP program"); break;
    default: Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }

  preferences.begin("config", true);
  memset(wifi_ssid, 0, sizeof(wifi_ssid));
  memset(wifi_pass, 0, sizeof(wifi_pass));
  memset(mqtt_pass, 0, sizeof(mqtt_pass));
  memset(mqtt_sn, 0, sizeof(mqtt_sn));
  memset(mqtt_ip, 0, sizeof(mqtt_ip));
  memset(device_timezone, 0, sizeof(device_timezone));
  preferences.getString("wifi_ssid", wifi_ssid, sizeof(wifi_ssid));
  preferences.getString("wifi_pass", wifi_pass, sizeof(wifi_pass));
  preferences.getString("mqtt_pass", mqtt_pass, sizeof(mqtt_pass));
  preferences.getString("mqtt_sn", mqtt_sn, sizeof(mqtt_sn));
  preferences.getString("mqtt_ip", mqtt_ip, sizeof(mqtt_ip));
  mqtt_port = preferences.getUInt("mqtt_port", 8883);
  preferences.getString("device_timezone", device_timezone, sizeof(device_timezone));
  memset(weather_location, 0, sizeof(weather_location));
  memset(stock_symbols, 0, sizeof(stock_symbols));
  memset(ics_urls, 0, sizeof(ics_urls));
  preferences.getString("weather_loc", weather_location, sizeof(weather_location));
  preferences.getString("stock_symbols", stock_symbols, sizeof(stock_symbols));
  for (int i = 0; i < ICS_MAX_FEEDS; i++)
    preferences.getString(ICS_PREF_KEYS[i], ics_urls[i], sizeof(ics_urls[i]));
  memset(makerworld_uid, 0, sizeof(makerworld_uid));
  preferences.getString("makerworld_uid", makerworld_uid, sizeof(makerworld_uid));
  memset(printer_name, 0, sizeof(printer_name));
  preferences.getString("printer_name", printer_name, sizeof(printer_name));
  memset(pkge_api_key, 0, sizeof(pkge_api_key));
  preferences.getString("pkge_key", pkge_api_key, sizeof(pkge_api_key));
  memset(aerodatabox_key, 0, sizeof(aerodatabox_key));
  preferences.getString("adb_key", aerodatabox_key, sizeof(aerodatabox_key));
  if (device_timezone[0] == '\0') {
    strncpy(device_timezone, "UTC0", sizeof(device_timezone) - 1);
  }
  activePinPreset = parsePinPreset(preferences.getString("pin_preset", pinPresetValue(DEFAULT_PIN_PRESET)));
  customPins = loadCustomPinConfig(preferences, makePinPreset(DEFAULT_PIN_PRESET));
  applyStoredPinSelection();
  logPinConfiguration();
  if (activePinPreset == PinPreset::Esp32C6SuperMini) {
    forceSuperMiniLedsOff();
  }

#if USE_ZIGBEE
  zigbee_enable = preferences.getBool("zigbee_enable", false);
  zigbee_wait = preferences.getBool("zigbee_wait", true);
  zigbee_linked = preferences.getBool("zigbee_linked", false);
  zigbee_pairing_failed = preferences.getBool("zigbee_pairing_failed", false);
  s_zigbee_linked = zigbee_linked;
  s_zigbee_pairing_failed = zigbee_pairing_failed;

  memset(zigbee_monitor, 0, sizeof(zigbee_monitor));
  memset(zigbee_control, 0, sizeof(zigbee_control));
  preferences.getString("zigbee_monitor", zigbee_monitor, sizeof(zigbee_monitor));
  if (zigbee_monitor[0] == '\0') {
    strncpy(zigbee_monitor, "0", sizeof(zigbee_monitor) - 1);
  }
  preferences.getString("zigbee_control", zigbee_control, sizeof(zigbee_control));
  if (zigbee_control[0] == '\0') {
    strncpy(zigbee_control, "0", sizeof(zigbee_control) - 1);
  }
#endif

  preferences.end();

  // Setup mode asked by a 4 s hold while the board was awake.
  if (setupRequest == SETUP_REQUEST_MAGIC) {
    setupRequest = 0;
    startAP();
    return;
  }

  // Check the button as early as possible, so the hold time matches what the user feels.
  switch (readWakeButtonAction()) {
    case BUTTON_ACTION_AP:
      startAP();
      return;
    case BUTTON_ACTION_REBOOT:
      ESP.restart();
      return;
    case BUTTON_ACTION_NONE:
    default:
      break;
  }
  if (hasDemoButtonPin()) {
    xTaskCreate(buttonWatchTask, "button", 3072, nullptr, 2, nullptr);
  }

  delay(200);
// DBGF("Wifi %s Pass %s", wifi_ssid.c_str(), wifi_pass.c_str());

  // Start an AP and server to configure it
  if (wifi_ssid[0] == '\0' || wifi_pass[0] == '\0') {
    startAP();
    return;
  }

  if (activePinPreset == PinPreset::Esp32C6SuperMini) {
    forceSuperMiniLedsOff();
  }

  // Serial.printf("Is Printing %d Prev %d\n", isPrinting, previousIsPrinting);
  DBG(F("============= STARTING ============="));

  loadDefaultLayout();
  LayoutItem* infoClock = getLayout(1);
  LayoutItem* infoEvent = getLayout(2);
  LayoutItem* infoStocks = getLayout(4);
  LayoutItem* infoOpenMeteo = getLayout(8);
  LayoutItem* infoBambu = getLayout(64);
  LayoutItem* infoTracking = getLayout(16);
  LayoutItem* infoBattery = getLayout(128);
  LayoutItem* infoCalendar = getLayout(256);

  bool eventIf = shouldFetchRefresh(infoEvent);
  bool stocksIf = shouldFetchRefresh(infoStocks);
  bool openMeteoIf = shouldFetchRefresh(infoOpenMeteo);
  bool trackingIf = shouldFetchRefresh(infoTracking);
  // A parcel delivered more than a day ago is still on screen: remove it without a download.
  bool trackingRedraw = infoTracking && !trackingIf && trackingDropExpired();

  // A flight departs within 24 hours: refresh its countdown every 10 minutes, using the calendar
  // data kept in RTC memory (no download).
  time_t nowTs = time(nullptr);
  bool flightSoon = cal.nextFlight.start > nowTs && cal.nextFlight.start - nowTs < 86400;

  // Live flight status: follow the next flight from 6 hours before its departure until it lands.
  if (cal.nextFlight.start && cal.nextFlight.start != liveFlight.calStart &&
      cal.nextFlight.start - nowTs < FLIGHT_LIVE_BEFORE_S) {
    liveFlight = {};
    liveFlight.calStart = cal.nextFlight.start;
    liveFlight.calEnd = cal.nextFlight.end;
    snprintf(liveFlight.title, sizeof(liveFlight.title), "%s", cal.nextFlight.title);
    extractFlightNumber(liveFlight.title, liveFlight.number, sizeof(liveFlight.number));
  }
  // Stop 1 h after the actual landing, and in any case at most FLIGHT_LIVE_AFTER_MAX_S after the
  // expected landing (scheduled arrival, else the calendar end), even if "landed" is never reported.
  time_t expectedLanding = liveFlight.arrScheduled ? liveFlight.arrScheduled
                         : (liveFlight.calEnd > liveFlight.calStart ? liveFlight.calEnd : liveFlight.calStart);
  time_t liveLimit = expectedLanding + FLIGHT_LIVE_AFTER_MAX_S;
  if (liveFlight.arrActual && liveFlight.arrActual + 3600 < liveLimit) liveLimit = liveFlight.arrActual + 3600;
  bool liveWindow = liveFlight.calStart &&
                    nowTs >= liveFlight.calStart - FLIGHT_LIVE_BEFORE_S && nowTs < liveLimit;
  bool flightFinished = liveFlight.valid &&
    (!strcmp(liveFlight.status, "Arrived") || !strcmp(liveFlight.status, "Canceled") ||
     !strcmp(liveFlight.status, "Diverted"));
  time_t liveDep = liveFlight.depActual ? liveFlight.depActual
                 : liveFlight.depScheduled ? liveFlight.depScheduled : liveFlight.calStart;
  time_t liveArr = liveFlight.arrActual ? liveFlight.arrActual : liveFlight.arrScheduled;
  bool nearMovement = (nowTs > liveDep - 3600 && nowTs < liveDep + 1800) ||
                      (liveArr && nowTs > liveArr - 3600 && nowTs < liveArr + 1800);
  time_t liveRefresh = nearMovement ? FLIGHT_LIVE_FAST_S : FLIGHT_LIVE_REFRESH_S;
  bool flightStatusDue = liveWindow && !flightFinished && liveFlight.number[0] && aerodatabox_key[0] &&
    liveFlight.requests < FLIGHT_LIVE_MAX_REQUESTS &&
    (liveFlight.lastFetch == 0 || nowTs - liveFlight.lastFetch >= liveRefresh);

  bool eventRedraw = infoEvent && (flightSoon || liveWindow) && !eventIf && (bootCount % 10 == 0);
  bool batteryIf = shouldFetchRefresh(infoBattery);
  bool calendarIf = shouldFetchRefresh(infoCalendar);
  bool bambuIf = (infoBambu && infoBambu->Active && (((isPrinting || previousIsPrinting) && (bootCount % infoBambu->Refresh == 0)) || (bootCount % (infoBambu->Refresh * 2) == 0))) || pendingBambuRetry;
  // The 5-minute "print finished" window just ended: redraw so the MakerWorld view replaces the printer view.
  if (infoBambu && printViewOnScreen && !showPrintView()) bambuIf = true;

  // MakerWorld stats: only needed for the idle view, refreshed at most once per MAKERWORLD_REFRESH_S.
  bool makerworldDue = infoBambu && makerworld_uid[0] != '\0' && !showPrintView() &&
    (makerworldLastFetch == 0 || time(nullptr) - makerworldLastFetch >= MAKERWORLD_REFRESH_S);
  bool makerworldRedraw = false;

  // Internet time is re-synced at least once per NTP_RESYNC_S: the RTC drifts during deep sleep.
  bool ntpDue = lastNtpSync == 0 || time(nullptr) - lastNtpSync >= NTP_RESYNC_S;

  // Draw the clock right away from the RTC, before any (slow) network work, so it changes
  // within a couple of seconds of the minute. Full-refresh boots draw it with everything else.
  if (!fullRefresh && infoClock) {
    initDisplay(false);
    updatePartial(infoClock, updateClock);
    clockDrawnMinute = currentMinute();
    display.powerOff();  // keeps controller RAM; the panel is powered again for the next update
  }

  // Bring Wi-Fi up only when at least one widget needs fresh data.
  bool needWiFi =
    eventIf || stocksIf || openMeteoIf || trackingIf || bambuIf || fullRefresh || flightStatusDue
    || (calendarIf && hasIcsFeeds())
    || makerworldDue
    || ntpDue;

  DBGF("Bambu %d Print %d Prev %d Weather %d Stocks %d Events %d Calendar %d MW %d NTP %d Full %d",
       bambuIf, isPrinting, previousIsPrinting, openMeteoIf, stocksIf, eventIf, calendarIf,
       makerworldDue, ntpDue, fullRefresh);

  if (needWiFi) {
    if (startWiFiReliable(wifi_ssid, wifi_pass)) {
      DBG("WiFi connected.");
      setProblem(PROBLEM_WIFI, false);
      initTime();

      if (bambuIf)
      {
        bool bambuOk = fetchBambu(infoBambu);
        // A rejected access code won't fix itself: don't retry (with Wi-Fi) on every wake.
        pendingBambuRetry = !bambuOk && !bambuAuthRejected;
        setProblem(PROBLEM_PRINTER_CODE, !bambuOk && bambuAuthRejected);
        setProblem(PROBLEM_PRINTER, !bambuOk && !bambuAuthRejected);
        signalPrintTransition();
        forceUpdateStatusBar = true;
      }

      // Weather, stocks and calendar come straight from Open-Meteo, Yahoo Finance and the iCal feeds.
      // On a failed fetch keep the old content on screen instead of drawing "unavailable".
      if (openMeteoIf) {
        bool ok = fetchWeatherDirect(infoOpenMeteo);
        setProblem(PROBLEM_WEATHER, !ok);
        if (!ok && !fullRefresh) openMeteoIf = false;
      }
      if (stocksIf) {
        int failed = 0;
        bool ok = fetchStocksDirect(&failed);
        setProblem(PROBLEM_STOCKS, !ok || failed > 0);
        if (!ok && !fullRefresh) stocksIf = false;
      }
      if ((eventIf || calendarIf) && hasIcsFeeds()) {
        const char* tz = (infoClock && infoClock->Extra1[0]) ? infoClock->Extra1 : device_timezone;
        int failed = 0;
        bool ok = fetchCalendarIcs(tz, &failed);
        setProblem(PROBLEM_CALENDAR, !ok || failed > 0);
        if (!ok && !fullRefresh) eventIf = calendarIf = false;
      }
      if (trackingIf) {
        bool ok = fetchTrackingDirect();
        setProblem(PROBLEM_TRACKING, !ok);
        if (!ok && !fullRefresh) trackingIf = false;
      }
      if (flightStatusDue) {
        const char* tz = (infoClock && infoClock->Extra1[0]) ? infoClock->Extra1 : device_timezone;
        bool ok = fetchFlightStatus(tz);
        liveFlight.lastFetch = time(nullptr);  // also after a failure: retried at the next interval
        liveFlight.requests++;
        setProblem(PROBLEM_FLIGHT, !ok);
        if (ok && infoEvent) eventRedraw = true;  // show the new status right away
      }

      // Re-check after the Bambu fetch: a print may have just started.
      if (makerworldDue && !showPrintView()) {
        makerworldRedraw = fetchMakerWorldDirect();
        makerworldLastFetch = time(nullptr);  // also after a failure, so a bad ID retries hourly, not every wake
        setProblem(PROBLEM_MAKERWORLD, !makerworldRedraw);
      }

      // Usually already done while the fetches ran; wait a bit longer when a re-sync is due.
      if (waitForNtpSync(ntpDue ? 5000 : 1000)) {
        lastNtpSync = time(nullptr);
        setProblem(PROBLEM_TIME, false);
      } else if (ntpDue) {
        setProblem(PROBLEM_TIME, true);
      }

    } else {
      DBG("WiFi FAILED → keeping the previous content");
      setProblem(PROBLEM_WIFI, true);
      if (!fullRefresh) openMeteoIf = stocksIf = eventIf = calendarIf = trackingIf = false;
      if (bambuIf) pendingBambuRetry = true;
    }
    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_OFF);

  }

#if USE_ZIGBEE
  if (zigbee_enable) {
    if (!zigbee_linked) {
      DBGF("[ZB] Zigbee enabled but not linked -> opening pairing for up to 60 seconds");
      ensureZigbeePaired();
      zigbee_linked = s_zigbee_linked;
      zigbee_pairing_failed = s_zigbee_pairing_failed;
      saveZigbeeStatus();
    }

    if (zigbee_linked && bambuIf) {
      bool auxFanOn = isAuxFanOn();
      int8_t desiredCommand = auxFanOn ? 1 : 0;

      DBGF("[ZB] Syncing Zigbee -> %s", auxFanOn ? "ON" : "OFF");
      if (syncZigbeePower(auxFanOn)) {
        zigbee_aux_last_command = desiredCommand;
        forceUpdateStatusBar = true;
      }
      zigbee_linked = s_zigbee_linked;
      zigbee_pairing_failed = s_zigbee_pairing_failed;
      saveZigbeeStatus();
    }
  }
#endif

  DBGF("Boot Count %d", bootCount);

  // Printer status while printing and for 5 minutes after; MakerWorld stats otherwise.
  bool printView = showPrintView();
  if (bambuIf || makerworldRedraw) printViewOnScreen = printView;

  // Start SPI and initialize display after deciding refresh mode (no-op if the early clock did it).
  initDisplay(fullRefresh);

  DBGF("What refresh %d", fullRefresh);
  /* FullREFRESH */
  if (fullRefresh) {
    DBG(F("Do the full refresh"));

    display.setFullWindow();
    display.fillScreen(GxEPD_WHITE);
    display.firstPage();

    do {
      if (bambuIf || makerworldRedraw)
      {
        if (printView) bambuWidget(infoBambu);
        else           makerWorldWidget(infoBambu);
      }

      updateClock(infoClock);

      if (openMeteoIf) weatherWidget(infoOpenMeteo);

      if (infoBattery)
        drawStatus(infoBattery);
      DBG("HERE AFTER BATTERY");

      if (eventIf || eventRedraw) gCalWidget(infoEvent);
      if (stocksIf) stockWidget(infoStocks);
      if (calendarIf) drawCalendar(infoCalendar);
      if (trackingIf || trackingRedraw) trackingWidget(infoTracking);
    } while (display.nextPage());

    DBG(F("Finish full"));
  }
  /* PARTIAL REFRESH */
  else {
    DBG(F("Do Partial refresh"));

    // Already drawn before the network work; redraw only if the minute changed meanwhile.
    if (clockDrawnMinute != currentMinute())
      updatePartial(infoClock, updateClock);

    if (openMeteoIf)
      updatePartial(infoOpenMeteo, weatherWidget);

    if (bambuIf || makerworldRedraw)
      updatePartial(infoBambu, printView ? bambuWidget : makerWorldWidget);

    if (eventIf || eventRedraw)
      updatePartial(infoEvent, gCalWidget);

    if (stocksIf)
      updatePartial(infoStocks, stockWidget);

    if (calendarIf)
      updatePartial(infoCalendar, drawCalendar);

    if (trackingIf || trackingRedraw)
      updatePartial(infoTracking, trackingWidget);

    if (infoBattery && (batteryIf || forceUpdateStatusBar || problems != problemsShown))
      updatePartial(infoBattery, drawStatus);
  }
  bootCount++;
  DBGF("Finished Boot %d\n", bootCount);

  // Wake WAKE_AFTER_MINUTE_MS after the next minute boundary (sub-second precision).
  uint64_t sleep_us = 60ULL * 1000000ULL;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec > 1700000000) {  // RTC holds a real date
    uint64_t intoMinuteUs = (uint64_t)(tv.tv_sec % 60) * 1000000ULL + tv.tv_usec;
    sleep_us = 60ULL * 1000000ULL - intoMinuteUs + WAKE_AFTER_MINUTE_MS * 1000ULL;
  } else {
    DBG(F("Time not set, sleeping 60s"));
  }
  DBGF("Awake %lu ms, sleeping %llu ms", millis(), sleep_us / 1000ULL);

  // hibernate() clears the controller's internal frame buffer used for partial refresh XOR.
  // Only use it when the next boot will be a full refresh (controller RAM is reloaded anyway).
  // For partial-refresh boots, powerOff() keeps the controller RAM intact.
  // bootCount was already incremented, so it is the next boot's value.
  bool nextBootIsFullRefresh = (bootCount % (60 * 24) == 0);
  if (nextBootIsFullRefresh) {
    display.hibernate();
  } else {
    display.powerOff();
  }

  if (hasDisplayPowerPin()) {
    gpio_hold_en((gpio_num_t)activePins.displayPower);
  }

  if (hasDemoButtonPin()) {
    // A hold in progress: let the button task finish it, instead of waking right back up and
    // restarting the count.
    if (digitalRead(activePins.demoButton) == HIGH) {
      while (digitalRead(activePins.demoButton) == HIGH) delay(10);
      delay(500);  // the button task may be restarting the board
    }
    esp_sleep_enable_ext1_wakeup(
      (1ULL << getDemoWakeGpio()),
      ESP_EXT1_WAKEUP_ANY_HIGH  // Wake when pin goes HIGH
    );
  }

  esp_sleep_enable_timer_wakeup(sleep_us);

  esp_deep_sleep_start();
}

void loop() {
  if (apModeActive || wifi_ssid[0] == '\0' || wifi_pass[0] == '\0') {
    dnsServer.processNextRequest();
    server.handleClient();

    // Setup mode opened by mistake (already configured): go back to the dashboard after a while
    // instead of keeping the radio on and draining the battery.
    if (wifi_ssid[0] != '\0' && wifi_pass[0] != '\0' && millis() - apLastActivity > AP_TIMEOUT_MS) {
      Serial.println("Setup mode timed out -> restarting");
      ESP.restart();
    }
  }
}
