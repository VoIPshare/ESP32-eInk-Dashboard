#include "app.h"
#include <WiFi.h>
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


#if USE_ZIGBEE
// #warning "ZIGBEE HEADER INCLUDED"
#include "dash_zigbee.h"
#endif

WebServer server(80);
Preferences preferences;

String wifi_ssid;
String wifi_pass;
String mqtt_pass;
String mqtt_sn;
String mqtt_ip;
uint16_t mqtt_port;
String api_key;

bool forceUpdateStatusBar = false;
bool forceRefreshAfterDemo = false;

#if USE_ZIGBEE
bool zigbee_enable = false;
bool zigbee_wait = true;

String zigbee_monitor;
uint8_t zigbee_monitor_ep = 1;

String zigbee_control;
uint8_t zigbee_control_ep = 1;
#endif

unsigned long previousMillis = 0;

RTC_DATA_ATTR uint16_t bootCount = 0;
RTC_DATA_ATTR bool lastPrint = false;

bool fullRefresh;

constexpr float BAT_MIN_V = 3.0f;
constexpr float BAT_MAX_V = 3.9f;

/* --------------------------------------------------
   HTML PAGE (stored in flash to save RAM)
   -------------------------------------------------- */
const char PAGE_HTML_START[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Configuration</title>

<style>
*{box-sizing:border-box;margin:0;padding:0}

:root{
--bg:#0d1117;
--card:#161b22;
--border:#30363d;
--accent:#58a6ff;
--accent2:#3fb950;
--muted:#8b949e;
--text:#e6edf3
}

body{
background:var(--bg);
color:var(--text);
font-family:Courier New,monospace;
min-height:100vh;
display:flex;
align-items:center;
justify-content:center;
padding:16px
}

.card{
width:100%;
max-width:420px;
background:var(--card);
border:1px solid var(--border);
border-radius:10px;
overflow:hidden;
box-shadow:0 8px 32px rgba(0,0,0,.5)
}

.header{
padding:14px 18px;
border-bottom:1px solid var(--border);
display:flex;
align-items:center;
gap:10px
}

h1{
font-size:13px;
text-transform:uppercase
}

.section{
padding:14px 18px;
border-top:1px solid var(--border)
}

.section h3{
font-size:12px;
margin-bottom:10px;
color:var(--accent)
}

label{
font-size:10px;
color:var(--muted);
display:block;
margin-top:10px
}

input{
width:100%;
background:var(--bg);
border:1px solid var(--border);
color:var(--text);
padding:7px;
border-radius:5px;
margin-top:4px;
font-size:12px
}

input:focus{
outline:none;
border-color:var(--accent)
}

.row{
display:flex;
gap:10px
}

.row input{
flex:1
}

.checkbox{
display:flex;
align-items:center;
gap:8px;
margin-top:10px
}

.checkbox input{
width:auto
}

button{
width:100%;
background:var(--accent);
color:#0d1117;
border:none;
border-radius:6px;
font-weight:700;
padding:10px;
cursor:pointer;
margin-top:18px;
font-size:12px
}

button:hover{
background:#79c0ff
}

.footer{
text-align:center;
font-size:9px;
color:var(--muted);
padding:10px
}
</style>
</head>

<body>

<div class="card">

<div class="header">
<h1>ESP32 Configuration</h1>
</div>

<form method="POST" action="/save">

<div class="section">
<h3>WiFi</h3>

<label>WiFi SSID</label>
<input name="wifi_ssid" placeholder="MyWiFi">

<label>WiFi Password</label>
<input name="wifi_pass" >
</div>

<div class="section">
<h3>Google Script</h3>

<label>API Key</label>
<input name="api_key" placeholder="Enter your Google Script key">
</div>

<div class="section">
<h3>Printer (MQTT)</h3>

<label>MQTT IP</label>
<input name="mqtt_ip" placeholder="192.168.1.10">

<div class="row">
<input name="mqtt_port" placeholder="Port">
<input name="mqtt_sn" placeholder="Serial number">
</div>

<label>MQTT Password</label>
<input name="mqtt_pass">
</div>


)rawliteral";



#if USE_ZIGBEE
const char PAGE_HTML_ZIGBEE[] PROGMEM = R"rawliteral(
<div class="section">
<h3>Zigbee</h3>

<div class="checkbox">
<input type="checkbox" name="zigbee_enable">
<label>Enable Zigbee</label>
</div>

</div>
)rawliteral";
#else
const char PAGE_HTML_ZIGBEE[] PROGMEM = "";
#endif

const char PAGE_HTML_END[] PROGMEM = R"rawliteral(
<div class="section">
<button type="submit">SAVE CONFIGURATION</button>
</div>

<div class="footer">
ESP32 Dashboard Setup
</div>

</form>
</div>

</body>
</html>
)rawliteral";


#if USE_COLORDISPLAY
// 7.5" 4-color panel (GDEM075F52)
GxEPD2_4C< GxEPD2_750c_GDEM075F52, GxEPD2_750c_GDEM075F52::HEIGHT / 2> display(
  GxEPD2_750c_GDEM075F52(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
#else
GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> display(
  GxEPD2_750_T7(
    EPD_CS,   // CS
    EPD_DC,   // DC
    EPD_RST,  // RST
    EPD_BUSY  // BUSY
    ));

#endif

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
#ifdef BAT_PIN
float readBattery() {
  float calibrationFactor = 1.05;  // tune with multimeter
  analogReadResolution(12);
  uint16_t raw = analogRead(BAT_PIN);
  float voltage = raw * 3.3 / 4095.0 * 2.0 * calibrationFactor;
  return voltage;
}
#endif

void drawStatus(LayoutItem* item) {
  #ifdef  BAT_PIN
  DBG(F("Battery"));

  float voltage = readBattery();
  // Serial.println(voltage);
  char buf[20];
  snprintf(buf, sizeof(buf), "%.2f V", voltage);

  display.fillRect(item->PosX, item->PosY, item->Width, item->Height, GxEPD_WHITE);

  drawSparseString(&epaperFont, item->PosX, item->PosY + 16, buf, GxEPD_BLACK);

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
#endif
#if USE_ZIGBEE
  if (s_light_state)
    drawSparseChar(&MDI_22_Sparse, item->PosX + item->Width - 36, item->PosY + 16, 0xF032C, GxEPD_BLACK);
  else
    drawSparseChar(&MDI_22_Sparse, item->PosX + item->Width - 36, item->PosY + 16, 0xF032E, GxEPD_BLACK);
#endif
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

void handleRoot() {
  String page = FPSTR(PAGE_HTML_START);
  page += FPSTR(PAGE_HTML_ZIGBEE);
  page += FPSTR(PAGE_HTML_END);

  server.send(200, "text/html", page);
}

void handleSave() {
  wifi_ssid = server.arg("wifi_ssid");
  wifi_pass = server.arg("wifi_pass");
  api_key = server.arg("api_key");


  mqtt_pass = server.arg("mqtt_pass");
  mqtt_sn = server.arg("mqtt_sn");
  mqtt_ip = server.arg("mqtt_ip");
  mqtt_port = server.arg("mqtt_port").toInt();

#if USE_ZIGBEE
  zigbee_enable = server.hasArg("zigbee_enable");
  zigbee_wait = server.hasArg("zigbee_wait");

  zigbee_monitor = server.arg("zigbee_monitor");
  zigbee_monitor_ep = server.arg("zigbee_monitor_ep").toInt();

  zigbee_control = server.arg("zigbee_control");
  zigbee_control_ep = server.arg("zigbee_control_ep").toInt();
#endif

  preferences.begin("config", false);

  preferences.putString("wifi_ssid", wifi_ssid);
  preferences.putString("wifi_pass", wifi_pass);
  preferences.putString("api_key", api_key);

  preferences.putString("mqtt_pass", mqtt_pass);
  preferences.putString("mqtt_sn", mqtt_sn);
  preferences.putString("mqtt_ip", mqtt_ip);
  preferences.putUInt("mqtt_port", mqtt_port);

#if USE_ZIGBEE
  preferences.putBool("zigbee_enable", zigbee_enable);
  preferences.putBool("zigbee_wait", zigbee_wait);

  preferences.putString("zigbee_monitor", zigbee_monitor);
  preferences.putUInt("zigbee_monitor_ep", zigbee_monitor_ep);

  preferences.putString("zigbee_control", zigbee_control);
  preferences.putUInt("zigbee_control_ep", zigbee_control_ep);
#endif
  preferences.end();

  server.send(200, "text/html", "<h2>Saved. Rebooting...</h2>");
  delay(1500);
  ESP.restart();
}


void startAP() {

  Serial.println("Start AP");

  // display.display(true);  // full refresh

#ifdef PIN_DISPLAYPOWER
  pinMode(PIN_DISPLAYPOWER, OUTPUT);
  digitalWrite(PIN_DISPLAYPOWER, HIGH);
  delay(50);
#endif

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  // Display a message on the e-paper
  display.init(115200, true, 2, false);  // Full refresh
  display.setRotation(0);
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);



  display.firstPage();
  do {
    // Draw centered text
    drawSparseStringCentered(&epaperFont, 400, 175, "ESP32 AP Mode", GxEPD_BLACK);
    drawSparseStringCentered(&epaperFont, 400, 275, "Connect to Dashboard-Setup and go to", GxEPD_BLACK);
    drawSparseStringCentered(&epaperFont, 400, 325, "http://192.168.4.1", GxEPD_BLACK);
    drawSparseStringCentered(&epaperFont, 400, 375, "For the first configuration!", GxEPD_BLACK);

  } while (display.nextPage());

  DBG(F("Display configured"));

  display.powerOff();

  DBG(F("Start the AP for the first configuration"));

  bool result = WiFi.softAP("Dashbboard-Setup");

  if (result) {
    Serial.println("AP started OK");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("AP failed!");
  }

  Serial.println("AP Started. Connect and go to 192.168.4.1");

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
}


// void ScanWiFi() {
//   Serial.println("Scan start");
//   // WiFi.scanNetworks will return the number of networks found.
//   int n = WiFi.scanNetworks();
//   Serial.println("Scan done");
//   if (n == 0) {
//     Serial.println("no networks found");
//   } else {
//     Serial.print(n);
//     Serial.println(" networks found");
//     Serial.println("Nr | SSID                             | RSSI | CH | Encryption");
//     for (int i = 0; i < n; ++i) {
//       // Print SSID and RSSI for each network found
//       Serial.printf("%2d", i + 1);
//       Serial.print(" | ");
//       Serial.printf("%-32.32s", WiFi.SSID(i).c_str());
//       Serial.print(" | ");
//       Serial.printf("%4ld", WiFi.RSSI(i));
//       Serial.print(" | ");
//       Serial.printf("%2ld", WiFi.channel(i));
//       Serial.print(" | ");
//       switch (WiFi.encryptionType(i)) {
//         case WIFI_AUTH_OPEN:            Serial.print("open"); break;
//         case WIFI_AUTH_WEP:             Serial.print("WEP"); break;
//         case WIFI_AUTH_WPA_PSK:         Serial.print("WPA"); break;
//         case WIFI_AUTH_WPA2_PSK:        Serial.print("WPA2"); break;
//         case WIFI_AUTH_WPA_WPA2_PSK:    Serial.print("WPA+WPA2"); break;
//         case WIFI_AUTH_WPA2_ENTERPRISE: Serial.print("WPA2-EAP"); break;
//         case WIFI_AUTH_WPA3_PSK:        Serial.print("WPA3"); break;
//         case WIFI_AUTH_WPA2_WPA3_PSK:   Serial.print("WPA2+WPA3"); break;
//         case WIFI_AUTH_WAPI_PSK:        Serial.print("WAPI"); break;
//         default:                        Serial.print("unknown");
//       }
//       Serial.println();
//       delay(10);
//     }
//   }

//   // Delete the scan result to free memory for code below.
//   WiFi.scanDelete();
//   Serial.println("-------------------------------------");
// }


// Should serparate it in order to force also reading the layout
void drawDemoScreen() {
#ifdef PIN_DISPLAYPOWER
  pinMode(PIN_DISPLAYPOWER, OUTPUT);
  digitalWrite(PIN_DISPLAYPOWER, HIGH);
  delay(50);
#endif
  Serial.println("Entering Demo Mode");

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
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
  } while (display.nextPage());

  forceRefreshAfterDemo = true;

  // Serial.println("Demo Mode drawn. Holding for 3 seconds...");
  delay(3000);  // keep the demo visible
}

bool startWiFiReliable(const char* ssid, const char* password)
{
  DBG(F("startWiFiReliable called"));
  for (int attempt = 1; attempt <= 2; attempt++)
  {
    DBGF("\nWiFi attempt %d", attempt);

    // Stop everything cleanly
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(800); 

    // Restart WiFi
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    delay(200);  

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
      DBG(F("\nWiFi connected!") );
      DBG(WiFi.localIP());
      return true;
    }

    DBG(F("\nFailed. Retrying..."));
  }

  return false;
}

// =======================
// Setup
// =======================
void setup() {
  Serial.begin(115200);
#if CONFIG_IDF_TARGET_ESP32
  delay(2000);
#endif

  fullRefresh = (bootCount % (60 * 12) == 0);


// preferences.putString("api_key", "A");
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
  wifi_ssid = preferences.getString("wifi_ssid", "");
  wifi_pass = preferences.getString("wifi_pass", "");
  mqtt_pass = preferences.getString("mqtt_pass", "");
  mqtt_sn = preferences.getString("mqtt_sn", "");
  mqtt_ip = preferences.getString("mqtt_ip", "");
  mqtt_port = preferences.getUInt("mqtt_port", 8883);
  api_key = preferences.getString("api_key", "");

#if USE_ZIGBEE
  zigbee_enable = preferences.getBool("zigbee_enable", false);
  zigbee_wait = preferences.getBool("zigbee_wait", true);

  zigbee_monitor = preferences.getString("zigbee_monitor", "0");
  zigbee_control = preferences.getString("zigbee_control", "0");
#endif

  preferences.end();
  delay(200);

  // Start an AP and server to configure it
  if (wifi_ssid == "" || wifi_pass == "") {
    startAP();
    return;
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

#ifdef DEMO_BUTTON
#warning Here DEMOBUTTON
  pinMode(DEMO_BUTTON, INPUT_PULLUP);
  unsigned long pressStart = 0;

  if (digitalRead(DEMO_BUTTON) == HIGH) {
    pressStart = millis();
    while (digitalRead(DEMO_BUTTON) == HIGH) {
      if (millis() - pressStart >= BUTTON_HOLD_MS) {
        Serial.println("Button held → Demo mode");
        drawDemoScreen();
        fullRefresh = true;
        break;
      }
      delay(10);
    }
  }
#endif

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
    if (startWiFiReliable(wifi_ssid.c_str(), wifi_pass.c_str())) {
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
        fetchBambu(infoBambu);

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
  // This is to test, will be moved in the bambulab
  activateCoordinatorReadAndClose(true);
  forceUpdateStatusBar = true;
#endif

  DBGF("Boot Count %d", bootCount);

#ifdef PIN_DISPLAYPOWER
  // Not all hats need it; IO4 powers some e-paper boards.
  pinMode(PIN_DISPLAYPOWER, OUTPUT);
  digitalWrite(PIN_DISPLAYPOWER, HIGH);
  delay(50);
#endif

  // Start SPI and initialize display after deciding refresh mode.
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
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

      #ifdef BAT_PIN
      drawStatus(infoBattery);
      #endif

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

#ifdef BAT_PIN
    if (batteryIf || forceUpdateStatusBar)
      updatePartial(infoBattery, drawStatus);
#endif
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

#ifdef PIN_DISPLAYPOWER
  gpio_hold_en((gpio_num_t)PIN_DISPLAYPOWER);
#endif

  // Align wakeups to roughly the next minute boundary.
  // int seconds_to_sleep = 60 - timeinfo.tm_sec + 1;
  esp_sleep_enable_ext1_wakeup(
    (1ULL << GPIO_NUM_2),     // Pin mask
    ESP_EXT1_WAKEUP_ANY_HIGH  // Wake when pin goes HIGH
  );

  esp_sleep_enable_timer_wakeup((uint64_t)seconds_to_sleep * 1000000ULL);  // 60 secs

  esp_deep_sleep_start();
}

void loop() {
  if (wifi_ssid == "" || wifi_pass == "")
    server.handleClient();
}
