#include "app.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "fetchAllInfo.h"
#include "bambulab.h"
#include "proxmox.h"
#include "calendar.h"
#include <WebServer.h>
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

WebServer server(80);
Preferences preferences;

char wifi_ssid[CFG_WIFI_SSID_MAX];
char wifi_pass[CFG_WIFI_PASS_MAX];
char mqtt_pass[CFG_MQTT_PASS_MAX];
char mqtt_sn[CFG_MQTT_SN_MAX];
char mqtt_ip[CFG_MQTT_IP_MAX];
uint16_t mqtt_port;
char googleapi[CFG_GOOGLEAPI_MAX];
PinConfig activePins = makePinPreset(DEFAULT_PIN_PRESET);
PinConfig customPins = makePinPreset(PinPreset::Custom);
PinPreset activePinPreset = DEFAULT_PIN_PRESET;

bool forceUpdateStatusBar = false;
bool forceRefreshAfterDemo = false;
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

bool fullRefresh;

constexpr float BAT_MIN_V = 3.0f;
constexpr float BAT_MAX_V = 3.9f;

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

      <label>Demo Button Pin (optional)</label>
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
    <input  name="wifi_pass" placeholder="Enter WiFi password">

    <label>Google Script ID</label>
    <input name="googleapi" placeholder="Paste Google Apps Script ID">

    <hr>

    <label>MQTT Server IP</label>
    <input name="mqtt_ip" placeholder="192.168.1.100">

    <label>MQTT Port</label>
    <input name="mqtt_port" placeholder="1883">

    <label>MQTT Username</label>
    <input name="mqtt_sn" placeholder="Username">

    <label>MQTT Password</label>
    <input name="mqtt_pass" placeholder="Password">

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
  // DBG(F("Update Clock fun"));

  // setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
  setenv("TZ", infoClock->Extra1, 1);

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
  float calibrationFactor = 1.05;  // tune with multimeter
  analogReadResolution(12);
  uint16_t raw = analogRead(activePins.battery);
  float voltage = raw * 3.3 / 4095.0 * 2.0 * calibrationFactor;
  return voltage;
}

void drawStatus(LayoutItem* item) {
  if (!hasBatteryPin()) return;
  DBG(F("Battery Draw"));

  float voltage = readBattery();
  Serial.println(voltage);

  char buf[20];
  snprintf(buf, sizeof(buf), "%.2f V", voltage);

  char versionBuf[32];
  snprintf(versionBuf, sizeof(versionBuf), "v%s", FW_VERSION);
  
  display.fillRect(item->PosX, item->PosY, item->Width, item->Height, GxEPD_WHITE);

  drawSparseString(&epaperFont, item->PosX, item->PosY + 16, buf, GxEPD_BLACK);

  int16_t versionWidth = getSparseStringWidth(&epaperFont, versionBuf);
  int16_t iconReserve = 24;
#if USE_ZIGBEE
  iconReserve += 40;
#endif
  if (isPrinting || previousIsPrinting) {
    iconReserve += 20;
  }
  int16_t versionX = item->PosX + item->Width - iconReserve - versionWidth;
  int16_t minVersionX = item->PosX + getSparseStringWidth(&epaperFont, buf) + 8;
  if (versionX < minVersionX) {
    versionX = minVersionX;
  }
  drawSparseString(&epaperFont, versionX, item->PosY + 16, versionBuf, GxEPD_BLACK);

  float percentage = constrain((voltage - BAT_MIN_V) / (BAT_MAX_V - BAT_MIN_V) * 100.0f, 0, 100);

  // Serial.println(percentage);

  unsigned int startChar = 983162;  // empty
  unsigned int endChar = 983170;    // full

  unsigned int numSteps = endChar - startChar;  // 11 steps
                                                // Clamp percentage 0–100
  if (percentage > 100.0f) percentage = 100.0f;
  if (percentage < 0.0f) percentage = 0.0f;

  unsigned int charCode;

  if (percentage < 10.0f) {
    charCode = startChar;  // <10%
  } else if (percentage > 90.0f) {
    charCode = 983161;  // <10%
  } else {

    // Map 10–100% linearly to remaining steps
    charCode = startChar + ((percentage - 10.0f) * numSteps / 90.0f) + 1;
  }

  drawSparseChar(&MDI_22_Sparse, item->PosX + item->Width - 18, item->PosY + 16, charCode, GxEPD_BLACK);
#if USE_ZIGBEE
  if (zigbee_enable) {
    drawSparseChar(&MDI_22_Sparse, item->PosX + item->Width - 54, item->PosY + 16,
                   MDI_ZIGBEE, GxEPD_BLACK);
    uint32_t zigbeeIcon = zigbee_linked ? (s_light_state ? MDI_POWER_ON : MDI_POWER_OFF) : MDI_ALERT;
    drawSparseChar(&MDI_22_Sparse, item->PosX + item->Width - 36, item->PosY + 16,
                   zigbeeIcon, GxEPD_BLACK);
  }
#endif
  if (isPrinting || previousIsPrinting) {
    drawSparseChar(&MDI_22_Sparse, item->PosX + item->Width - 72, item->PosY + 16,
                   MDI_PRINT_ON, GxEPD_BLACK);
  }
}

// Partial refresh for one layout region. Useful to reduce ghosting + update time.
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
  return ( (bootCount % item->Refresh) == 0 || forceRefreshAfterDemo );
}

void handleConfig() {
  // String page = FPSTR(PAGE_HTML_START);
  // page += FPSTR(PAGE_HTML_ZIGBEE);
  // page += FPSTR(PAGE_HTML_END);

  server.send(200, "text/html", buildPage());
}

void handleSave() {
  copyToCfg(wifi_ssid, sizeof(wifi_ssid), server.arg("wifi_ssid"));
  copyToCfg(wifi_pass, sizeof(wifi_pass), server.arg("wifi_pass"));
  copyToCfg(googleapi, sizeof(googleapi), server.arg("googleapi"));
// Serial.printf("Wifi %s Pass %s", wifi_ssid, wifi_pass);

  copyToCfg(mqtt_pass, sizeof(mqtt_pass), server.arg("mqtt_pass"));
  copyToCfg(mqtt_sn, sizeof(mqtt_sn), server.arg("mqtt_sn"));
  copyToCfg(mqtt_ip, sizeof(mqtt_ip), server.arg("mqtt_ip"));
  mqtt_port = server.arg("mqtt_port").toInt();
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
  preferences.putString("googleapi", googleapi);

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

  Serial.println("Start AP");

  bool result = WiFi.softAP("Dashbboard-Setup");

  if (result) {
    Serial.println("AP started OK");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("AP failed!");
  }

  Serial.println("AP Started. Connect and go to 192.168.4.1");

  server.on("/", handleConfig);
  server.on("/save", HTTP_POST, handleSave);
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
    drawSparseStringCentered(&epaperFont, 400, 275, "Connect to Dashboard-Setup and go to", GxEPD_BLACK);
    drawSparseStringCentered(&epaperFont, 400, 325, "http://192.168.4.1", GxEPD_BLACK);
    drawSparseStringCentered(&epaperFont, 400, 375, "For the first configuration!", GxEPD_BLACK);
  } while (display.nextPage());

  DBG(F("Display configured"));
  display.powerOff();
}

// Should serparate it in order to force also reading the layout
void drawDemoScreen() {
  if (hasDisplayPowerPin()) {
    pinMode(activePins.displayPower, OUTPUT);
    digitalWrite(activePins.displayPower, HIGH);
    delay(50);
  }
  Serial.println("Entering Demo Mode");

  SPI.begin(activePins.epdSck, -1, activePins.epdMosi, activePins.epdCs);
  display.init(115200, true, 2, false);  // full refresh
  display.setRotation(0);
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);

  // Get all layout items
  LayoutItem* infoClock = getLayout(1);
  LayoutItem* infoEvent = getLayout(2);
  LayoutItem* infoStocks = getLayout(4);
  LayoutItem* infoOpenMeteo = getLayout(8);
  LayoutItem* infoTracking = getLayout(16);
  LayoutItem* infoProxMox = getLayout(32);
  LayoutItem* infoBambu = getLayout(64);
  LayoutItem* infoBattery = getLayout(128);
  LayoutItem* infoCalendar = getLayout(256);
  
  display.firstPage();
  do {
    LayoutItem* items[] = { infoClock, infoEvent, infoStocks, infoOpenMeteo,
                            infoTracking, infoProxMox, infoBambu, infoBattery, infoCalendar };

    const char* labels[] = { "Clock", "Event", "Stocks", "Weather",
                             "Tracking", "ProxMox", "Bambu", "Battery", "Calendar" };

    for (int i = 0; i < 9; i++) {
      LayoutItem* item = items[i];
      if (item && item->Active) {

        // Draw rectangle
        display.drawRect(item->PosX, item->PosY, item->Width, item->Height, GxEPD_BLACK);
        display.drawRect(item->PosX + 2, item->PosY + 2, item->Width - 4, item->Height - 4, GxEPD_WHITE);

        // Draw label in the center using smaller epaperFont
        int16_t centerX = item->PosX + item->Width / 2;
        int16_t centerY = item->PosY + item->Height / 2;
        drawSparseStringCentered(&epaperFont, centerX, centerY, labels[i], GxEPD_BLACK);
      }
    }
    drawSparseStringCentered(&epaperFont, 400, 470, "Hold 4s on wake for AP config", GxEPD_BLACK);
  } while (display.nextPage());

  forceRefreshAfterDemo = true;

  // Serial.println("Demo Mode drawn. Holding for 3 seconds...");
  delay(3000);  // keep the demo visible
}

constexpr int BUTTON_ACTION_NONE = 0;
constexpr int BUTTON_ACTION_DEMO = 1;
constexpr int BUTTON_ACTION_AP = 2;

int readWakeButtonAction() {
  if (!hasDemoButtonPin()) {
    return BUTTON_ACTION_NONE;
  }

  pinMode(activePins.demoButton, INPUT_PULLUP);
  if (digitalRead(activePins.demoButton) != HIGH) {
    return BUTTON_ACTION_NONE;
  }

  unsigned long pressStart = millis();
  while (digitalRead(activePins.demoButton) == HIGH) {
    unsigned long heldMs = millis() - pressStart;
    if (heldMs >= BUTTON_LONG_HOLD_MS) {
      Serial.println("Button held -> AP config mode");
      return BUTTON_ACTION_AP;
    }
    delay(10);
  }

  if (millis() - pressStart >= BUTTON_HOLD_MS) {
    Serial.println("Button held -> Demo mode");
    return BUTTON_ACTION_DEMO;
  }
  return BUTTON_ACTION_NONE;
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
#if CONFIG_IDF_TARGET_ESP32
  delay(2000);
#endif

  if (activePinPreset == PinPreset::Esp32C6SuperMini) {
    forceSuperMiniLedsOff();
  }

  fullRefresh = (bootCount % (60 * 24) == 0);


// preferences.putString("googleapi", "A");
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
  memset(googleapi, 0, sizeof(googleapi));
  preferences.getString("wifi_ssid", wifi_ssid, sizeof(wifi_ssid));
  preferences.getString("wifi_pass", wifi_pass, sizeof(wifi_pass));
  preferences.getString("mqtt_pass", mqtt_pass, sizeof(mqtt_pass));
  preferences.getString("mqtt_sn", mqtt_sn, sizeof(mqtt_sn));
  preferences.getString("mqtt_ip", mqtt_ip, sizeof(mqtt_ip));
  mqtt_port = preferences.getUInt("mqtt_port", 8883);
  preferences.getString("googleapi", googleapi, sizeof(googleapi));
  activePinPreset = parsePinPreset(preferences.getString("pin_preset", pinPresetValue(DEFAULT_PIN_PRESET)));
  if (activePinPreset == PinPreset::Esp32C6Default) {
    activePinPreset = PinPreset::Esp32C6SuperMini;
  }
  customPins = loadCustomPinConfig(preferences, makePinPreset(DEFAULT_PIN_PRESET));
  applyStoredPinSelection();
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

  bool hasStoredData = loadLayout();
  LayoutItem* infoClock = getLayout(1);
  LayoutItem* infoEvent = getLayout(2);
  LayoutItem* infoStocks = getLayout(4);
  LayoutItem* infoOpenMeteo = getLayout(8);
  LayoutItem* infoTracking = getLayout(16);
  LayoutItem* infoProxMox = getLayout(32);
  LayoutItem* infoBambu = getLayout(64);
  LayoutItem* infoBattery = getLayout(128);
  LayoutItem* infoCalendar = getLayout(256);

  switch (readWakeButtonAction()) {
    case BUTTON_ACTION_AP:
      startAP();
      return;
    case BUTTON_ACTION_DEMO:
      drawDemoScreen();
      fullRefresh = true;
      break;
    case BUTTON_ACTION_NONE:
    default:
      break;
  }

  if (!hasStoredData) {
    DBG("No stored data → forcing full refresh");
    fullRefresh = true;
  }

  bool clockIf = true;  // always update
  bool eventIf = shouldFetchRefresh(infoEvent);
  bool stocksIf = shouldFetchRefresh(infoStocks);
  bool openMeteoIf = shouldFetchRefresh(infoOpenMeteo);
  bool trackingIf = shouldFetchRefresh(infoTracking);
  bool proxmoxIf = shouldFetchRefresh(infoProxMox);
  bool batteryIf = shouldFetchRefresh(infoBattery);
  bool calendarIf = shouldFetchRefresh(infoCalendar);
  bool bambuIf = infoBambu && infoBambu->Active && (((isPrinting || previousIsPrinting) && (bootCount % infoBambu->Refresh == 0)) || (bootCount % (infoBambu->Refresh * 2) == 0));

  // Bring Wi-Fi up only when at least one widget needs fresh data.
  bool needWiFi =
    !hasStoredData || eventIf || stocksIf || openMeteoIf || trackingIf || proxmoxIf || bambuIf || fullRefresh;

  // needWiFi=true;
  DBGF(
    "Bambu %d Print %d Prev %d openMeteo %d ProxMox %d Stocks %d 30min %d FUll %d",
    bambuIf, isPrinting, previousIsPrinting, openMeteoIf, proxmoxIf, stocksIf, clockIf, fullRefresh);

  if (needWiFi) {
    if (startWiFiReliable(wifi_ssid, wifi_pass)) {
      DBG("WiFi connected.");
      initTime();
      if (!hasStoredData || stocksIf || trackingIf) {
        fetchData();

        DBG("GetObjects");
        infoClock = getLayout(1);
        infoEvent = getLayout(2);
        infoStocks = getLayout(4);
        infoOpenMeteo = getLayout(8);
        infoTracking = getLayout(16);
        infoProxMox = getLayout(32);
        infoBambu = getLayout(64);
        infoBattery = getLayout(128);
        infoCalendar = getLayout(256);

        eventIf = shouldFetchRefresh(infoEvent);
        stocksIf = shouldFetchRefresh(infoStocks);
        openMeteoIf = shouldFetchRefresh(infoOpenMeteo);
        trackingIf = shouldFetchRefresh(infoTracking);
        proxmoxIf = shouldFetchRefresh(infoProxMox);
        bambuIf = infoBambu && infoBambu->Active &&
          ((isPrinting || previousIsPrinting) && bootCount % infoBambu->Refresh == 0
           || bootCount % (infoBambu->Refresh * 2) == 0
           || forceRefreshAfterDemo);

        calendarIf = shouldFetchRefresh(infoCalendar);
      }

      if (bambuIf) 
      {
        fetchBambu(infoBambu);
        signalPrintTransition();
      }

      if (proxmoxIf) 
        fetchProxmoxStates(infoProxMox, 3);

    } else {
      DBG("WiFi FAILED → using cached data");
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

  if (hasDisplayPowerPin()) {
    // Not all hats need it; IO4 powers some e-paper boards.
    pinMode(activePins.displayPower, OUTPUT);
    digitalWrite(activePins.displayPower, HIGH);
    delay(50);
  }

  // Start SPI and initialize display after deciding refresh mode.
  SPI.begin(activePins.epdSck, -1, activePins.epdMosi, activePins.epdCs);
  display.init(115200, fullRefresh, 2, false);
  display.setRotation(0);

  DBGF("What refresh %d", fullRefresh);
  /* FullREFRESH */
  if (fullRefresh) {
    DBG(F("Do the full refresh"));

    display.setFullWindow();
    display.fillScreen(GxEPD_WHITE);
    display.firstPage();

    Serial.printf("ProxMoxIF %d BambuIF %d openMeteoIF %d EventIF %d StocksIF %d TrackingIF %d CalendarIF %d\n", proxmoxIf, bambuIf, openMeteoIf, eventIf, stocksIf, trackingIf, calendarIf );
    do {
      if (proxmoxIf) proxmoxWidget(infoProxMox);

      if (bambuIf)
      {
        if( !isPrinting && !previousIsPrinting )
        {
          makerWorldWidget(infoBambu);
        }
        else 
        {
          bambuWidget(infoBambu);
        }
      }

      updateClock(infoClock);

      if (openMeteoIf) weatherWidget(infoOpenMeteo);

      if (hasBatteryPin() && infoBattery)
        drawStatus(infoBattery);
      DBG("HERE AFTER BATTERY");

      if (eventIf) gCalWidget(infoEvent);
      if (stocksIf) stockWidget(infoStocks);
      if (trackingIf) trackingWidget(infoTracking);
      if (calendarIf) drawCalendar(infoCalendar);
    } while (display.nextPage());

    DBG(F("Finish full"));
  }
  /* PARTIAL REFRESH */
  else {
    DBG(F("Do Partial refresh"));

    updatePartial(infoClock, updateClock);

    if (openMeteoIf)
      updatePartial(infoOpenMeteo, weatherWidget);

    if (bambuIf)
    {
      if( !isPrinting && !previousIsPrinting )
        updatePartial(infoBambu, makerWorldWidget);
      else if (bambuIf)
        updatePartial(infoBambu, bambuWidget);
    }

    if (proxmoxIf)
      updatePartial(infoProxMox, proxmoxWidget);

    if (eventIf)
      updatePartial(infoEvent, gCalWidget);

    if (stocksIf)
      updatePartial(infoStocks, stockWidget);

    if (trackingIf)
      updatePartial(infoTracking, trackingWidget);

    if (calendarIf)
      updatePartial(infoCalendar, drawCalendar);

    if (hasBatteryPin() && (batteryIf || forceUpdateStatusBar))
      updatePartial(infoBattery, drawStatus);
  }
  bootCount++;
  DBGF("Finished Boot %d\n", bootCount);

int seconds_to_sleep = 60;
struct tm timeinfo = {};
if (getLocalTime(&timeinfo))
    seconds_to_sleep = 61 - timeinfo.tm_sec;
else
  DBG(F("NTP not synced, sleeping 60s"));

  // display.hibernate();

  if (hasDisplayPowerPin()) {
    gpio_hold_en((gpio_num_t)activePins.displayPower);
  }

  // Align wakeups to roughly the next minute boundary.
  // int seconds_to_sleep = 60 - timeinfo.tm_sec + 1;
  if (hasDemoButtonPin()) {
    esp_sleep_enable_ext1_wakeup(
      (1ULL << getDemoWakeGpio()),
      ESP_EXT1_WAKEUP_ANY_HIGH  // Wake when pin goes HIGH
    );
  }

  esp_sleep_enable_timer_wakeup((uint64_t)seconds_to_sleep * 1000000ULL);  // 60 secs

  esp_deep_sleep_start();
}

void loop() {
  if (apModeActive || wifi_ssid[0] == '\0' || wifi_pass[0] == '\0')
    server.handleClient();
}
