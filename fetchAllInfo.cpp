#include "app.h"
#include <ctype.h>
#include <string.h>
#include "configure.h"
#include "fetchAllInfo.h"
#include "bambulab.h"
#include <Preferences.h>

// Arrays to store the data
LayoutItem layout[MAXLAYOUTS];       // Adjust size as needed
StockItem stocks[5];
RTC_DATA_ATTR LiveFlight liveFlight;
RTC_DATA_ATTR CalEvents cal;  // survives deep sleep: lets the flight countdown redraw without a download
RTC_DATA_ATTR TrackingItem tracking[MAX_TRACKING];  // kept in deep sleep, so delivered parcels can expire between fetches
RTC_DATA_ATTR int trackingCount = 0;

WeatherData weather;

MakerWorld makerworld;

// Counts
int layoutCount = 0;
int stockCount = 0;
// int calEventCount=0;
Preferences prefs;

static void drawUnavailableMessage(LayoutItem* item) {
    if (!item) return;

    int centerX = item->PosX + item->Width / 2;
    int centerY = item->PosY + item->Height / 2;

    drawSparseStringCentered(&epaperFont, centerX, centerY - 8,
                             "Not available", GxEPD_BLACK);
    drawSparseStringCentered(&epaperFont, centerX, centerY + 10,
                             "at the moment", GxEPD_BLACK);
}

// Fixed dashboard layout (800x480). A widget is only added when its setting is filled in.
void loadDefaultLayout() {
    struct DefaultItem { int id; const char* desc; int x, y, w, h, rowH, colW, refresh; const char* extra1; bool enabled; };
    const DefaultItem defaults[] = {
        // Clock Extra1 left empty: updateClock() then uses the Timezone from the setup page.
        {   1, "Clock",      10,   0, 430, 155, 10,  10,   1, "", true },
        { 128, "Battery",   440,   0, 360,  28,  0,   0,  60, "", true },
        { 256, "Calendar",  440,  25, 360, 230, 28, 210,  60, "", true },
        {   2, "Cal Events",440, 255, 360, 145, 22,  10,  60, "", hasIcsFeeds() },
        {   4, "Stocks",     10, 155, 430, 130, 22,  10,  60, "", stock_symbols[0] != '\0' },
        {   8, "OpenMeteo",  10, 285, 430, 105,  0, 100, 360, "", weather_location[0] != '\0' },
        {  64, "Bambu",      10, 390, 430,  90, 22,  70,   4, printer_name[0] ? printer_name : "Bambu", mqtt_ip[0] != '\0' },
        {  16, "Tracking",  440, 400, 360,  80, 22,   0, 120, "", pkge_api_key[0] != '\0' },
    };

    layoutCount = 0;
    for (const DefaultItem& d : defaults) {
        if (!d.enabled) continue;
        LayoutItem& l = layout[layoutCount++];
        memset(&l, 0, sizeof(l));
        l.ID = d.id;
        l.PosX = d.x;
        l.PosY = d.y;
        l.Width = d.w;
        l.Height = d.h;
        l.RowHeight = d.rowH;
        l.ColWidth = d.colW;
        l.Refresh = d.refresh;
        l.Active = true;
        snprintf(l.Description, sizeof(l.Description), "%s", d.desc);
        snprintf(l.Extra1, sizeof(l.Extra1), "%s", d.extra1);
    }
}

#if DEBUG
void debugPrintLayout() {
    DBG(F("---- Layout Debug ----"));
    DBGF("Layout Count: %d", layoutCount);

    for (int i = 0; i < layoutCount; i++) {
        LayoutItem &l = layout[i];

        Serial.printf("Layout[%d]\n", i);
        Serial.printf(" ID: %d\n", l.ID);
        Serial.printf(" PosX: %d\n", l.PosX);
        Serial.printf(" PosY: %d\n", l.PosY);
        Serial.printf(" Width: %d\n", l.Width);
        Serial.printf(" Height: %d\n", l.Height);
        Serial.printf(" RowHeight: %d\n", l.RowHeight);
        Serial.printf(" ColWidth: %d\n", l.ColWidth);
        Serial.printf(" Group: %d\n", l.Group);
        Serial.printf(" Active: %d\n", l.Active);
        Serial.printf(" Refresh: %d\n", l.Refresh);

        Serial.printf(" Description: %s\n", l.Description);
        Serial.printf(" Extra1: %s\n", l.Extra1);
        Serial.printf(" Extra2: %s\n", l.Extra2);
        Serial.printf(" Extra3: %s\n", l.Extra3);
        Serial.printf(" Extra4: %s\n", l.Extra4);
        Serial.printf(" Extra5: %s\n", l.Extra5);

        Serial.println("------------------");
    }
}
#endif

void formatTimeRange(time_t start, time_t end, char* buf, size_t bufLen)
{
    if (!buf || bufLen == 0) return;
    struct tm ts;
    struct tm te;

    localtime_r(&start, &ts);
    localtime_r(&end, &te);

    snprintf(buf, bufLen, "%02d:%02d-%02d:%02d",
             ts.tm_hour, ts.tm_min,
             te.tm_hour, te.tm_min);
}

int findLayoutIndexByID(int id) {
  for (int i = 0; i < layoutCount; i++) {
    if (layout[i].ID == id) {
      return i;
    }
  }
  return -1;
}

// ----------------------- Flights in the calendar -----------------------

static bool isUpperWordAt(const char* p, int len) {
  for (int i = 0; i < len; i++)
    if (!isupper((unsigned char)p[i])) return false;
  return !isalnum((unsigned char)p[len]);
}

// "TS399", "AC 871": 2-character airline code (letters/digits, not both digits), 1-4 digit number.
// When `out` is given, the number is copied there without the space.
static bool findFlightNumber(const char* t, char* out, size_t outSize) {
  for (const char* p = t; *p; p++) {
    if (p != t && isalnum((unsigned char)p[-1])) continue;  // must start a word
    unsigned char a = p[0], b = a ? p[1] : 0;
    if (!(isupper(a) || isdigit(a)) || !(isupper(b) || isdigit(b))) continue;
    if (isdigit(a) && isdigit(b)) continue;
    const char* q = p + 2;
    if (*q == ' ') q++;
    int digits = 0;
    while (isdigit((unsigned char)q[digits])) digits++;
    if (digits >= 1 && digits <= 4 && !isalnum((unsigned char)q[digits])) {
      if (out && outSize > 0) snprintf(out, outSize, "%c%c%.*s", a, b, digits, q);
      return true;
    }
  }
  return false;
}

static bool hasFlightNumber(const char* t) { return findFlightNumber(t, nullptr, 0); }

bool extractFlightNumber(const char* title, char* out, size_t outSize) {
  return title && findFlightNumber(title, out, outSize);
}

// "PUJ->YUL", "YUL - YYZ", "YUL→YYZ", "YUL to YYZ", "YUL/YYZ": two airport codes with a separator.
static bool hasRoute(const char* t) {
  for (const char* p = t; *p; p++) {
    if (p != t && isalnum((unsigned char)p[-1])) continue;
    if (!isUpperWordAt(p, 3)) continue;
    const char* q = p + 3;
    bool separator = false;
    for (;;) {
      if (*q == ' ' || *q == '-' || *q == '>' || *q == '/') { q++; separator = true; }
      else if ((unsigned char)q[0] == 0xE2 && (unsigned char)q[1] == 0x86 && (unsigned char)q[2] == 0x92) { q += 3; separator = true; }  // →
      else if (q[0] == 't' && q[1] == 'o' && q[2] == ' ') { q += 3; separator = true; }
      else break;
    }
    if (separator && isUpperWordAt(q, 3)) return true;
  }
  return false;
}

bool isFlightTitle(const char* t) {
  if (!t || !*t) return false;
  if (strcasestr(t, "flight") || strstr(t, "\xE2\x9C\x88")) return true;  // "flight" or ✈
  return hasFlightNumber(t) && hasRoute(t);
}

// Small airplane (about 18x14 px) pointing right, vertically centered on a text line at baseline y.
static void drawPlaneIcon(int16_t x, int16_t y) {
  int16_t cy = y - 6;
  display.fillRect(x + 1, cy - 1, 15, 3, GxEPD_BLACK);                            // fuselage
  display.fillTriangle(x + 16, cy - 1, x + 16, cy + 1, x + 18, cy, GxEPD_BLACK);   // nose
  display.fillTriangle(x + 6, cy, x + 11, cy, x + 5, cy - 7, GxEPD_BLACK);         // upper wing
  display.fillTriangle(x + 6, cy, x + 11, cy, x + 5, cy + 7, GxEPD_BLACK);         // lower wing
  display.fillTriangle(x + 1, cy, x + 4, cy, x + 1, cy - 4, GxEPD_BLACK);          // tail
}

// "in 45m", "in 3h05m", "in 2d 4h".
static void formatCountdown(time_t secs, char* buf, size_t n) {
  if (secs < 3600)       snprintf(buf, n, "in %ldm", (long)(secs / 60));
  else if (secs < 86400) snprintf(buf, n, "in %ldh%02ldm", (long)(secs / 3600), (long)((secs % 3600) / 60));
  else                   snprintf(buf, n, "in %ldd %ldh", (long)(secs / 86400), (long)((secs % 86400) / 3600));
}

static void formatClock(time_t t, char* buf, size_t n) {
  struct tm lt;
  localtime_r(&t, &lt);
  strftime(buf, n, "%H:%M", &lt);
}

// One line summarizing the live status, e.g. "Delayed +45m, gate B7, Terminal B" or "Landed 19:12".
// The real departure/arrival times are shown on the flight's first line instead.
static void formatLiveFlight(char* buf, size_t n) {
  const LiveFlight& f = liveFlight;
  char arr[8];
  formatClock(f.arrActual ? f.arrActual : f.arrScheduled, arr, sizeof(arr));
  long delayMin = (f.depActual && f.depScheduled) ? (long)(f.depActual - f.depScheduled) / 60 : 0;

  if (!strcmp(f.status, "Canceled"))          { snprintf(buf, n, "CANCELED"); return; }
  if (!strcmp(f.status, "CanceledUncertain")) { snprintf(buf, n, "Possibly canceled"); return; }
  if (!strcmp(f.status, "Diverted"))          { snprintf(buf, n, "Diverted"); return; }
  if (!strcmp(f.status, "Arrived"))           { snprintf(buf, n, "Landed %s", arr); return; }
  if (!strcmp(f.status, "Departed") || !strcmp(f.status, "EnRoute") || !strcmp(f.status, "Approaching")) {
    snprintf(buf, n, "In the air, lands %s", arr);
    return;
  }

  const char* label = "Scheduled";
  if (!strcmp(f.status, "Expected"))   label = "On time";
  if (!strcmp(f.status, "CheckIn"))    label = "Check-in";
  if (!strcmp(f.status, "Boarding"))   label = "Boarding";
  if (!strcmp(f.status, "GateClosed")) label = "Gate closed";
  if (!strcmp(f.status, "Delayed") || delayMin >= 5) label = "Delayed";

  int len = snprintf(buf, n, "%s", label);
  if (delayMin >= 5 && len < (int)n) len += snprintf(buf + len, n - len, " +%ldm", delayMin);
  if (f.depGate[0] && len < (int)n) len += snprintf(buf + len, n - len, ", gate %s", f.depGate);
  if (f.depTerminal[0] && len < (int)n) {
    bool numeric = true;
    for (const char* c = f.depTerminal; *c; c++) if (!isdigit((unsigned char)*c)) numeric = false;
    snprintf(buf + len, n - len, numeric ? ", T%s" : ", Terminal %s", f.depTerminal);
  }
}

void gCalWidget( LayoutItem* item )
{
  int lastDay = -1;
  time_t now = time(nullptr);

  int y = item->PosY + 16;  // baseline of the first line, at the top of the widget
  bool firstLine = true;

  // A flight later than the next 24 hours is pinned on top with a countdown.
  const CalEventItem& flight = cal.nextFlight;
  if (flight.start > now + 86400) {
    char countdown[24], line[CFG_CAL_TITLE_MAX + 40];
    formatCountdown(flight.start - now, countdown, sizeof(countdown));
    snprintf(line, sizeof(line), "%s  %s", flight.title, countdown);
    drawPlaneIcon(item->PosX, y);
    drawSparseString(&epaperFont, item->PosX + 24, y, line, GxEPD_BLACK);
    y += item->RowHeight;
    firstLine = false;
  }

  for (size_t i = 0; i < cal.eventCount; i++)
  {
    struct tm t;
    localtime_r(&cal.next24hEvents[i].start, &t);

    // If new day → print day header
    if (t.tm_mday != lastDay)
    {
      if (!firstLine) y += 5;  // small gap between days
      firstLine = false;
      lastDay = t.tm_mday;

      char dayBuf[32];
      strftime(dayBuf, sizeof(dayBuf), "%A %d, %B", &t);

      drawSparseString(&epaperFont, item->PosX, y, dayBuf, GxEPD_BLACK);

      y += item->RowHeight+5;
    }

    // Stop before spilling into the widget below.
    if (y > item->PosY + item->Height) break;

    const CalEventItem& ev = cal.next24hEvents[i];
    bool live = liveFlight.valid && ev.start == liveFlight.calStart && isFlightTitle(ev.title);
    char timeRange[32];
    if (live && liveFlight.depScheduled && liveFlight.arrScheduled)
      // Real departure/arrival (delays included) instead of the calendar's times.
      formatTimeRange(liveFlight.depActual ? liveFlight.depActual : liveFlight.depScheduled,
                      liveFlight.arrActual ? liveFlight.arrActual : liveFlight.arrScheduled,
                      timeRange, sizeof(timeRange));
    else if (ev.allDay)
      snprintf(timeRange, sizeof(timeRange), "All day");
    else
      formatTimeRange(ev.start, ev.end, timeRange, sizeof(timeRange));
    char line[CFG_CAL_TITLE_MAX + 64];
    int n = snprintf(line, sizeof(line), "%s %s", timeRange, ev.title);

    int16_t x = item->PosX;
    if (isFlightTitle(ev.title)) {
      // Flights get a plane icon and a countdown to departure.
      drawPlaneIcon(x, y);
      x += 24;
      // Count down to the real departure (delays included) when live data is available.
      time_t departs = ev.start;
      if (liveFlight.valid && ev.start == liveFlight.calStart)
        departs = liveFlight.depActual ? liveFlight.depActual : liveFlight.depScheduled;
      if (departs > now && n > 0 && n < (int)sizeof(line)) {
        char countdown[24];
        formatCountdown(departs - now, countdown, sizeof(countdown));
        snprintf(line + n, sizeof(line) - n, "  %s", countdown);
      }
    }
    drawSparseString(&epaperFont, x, y, line, GxEPD_BLACK);

    // Live status (AeroDataBox) on a second line under the flight.
    if (x != item->PosX && liveFlight.valid && ev.start == liveFlight.calStart &&
        y + item->RowHeight <= item->PosY + item->Height) {
      y += item->RowHeight;
      char live[96];
      formatLiveFlight(live, sizeof(live));
      drawSparseString(&epaperFont, x, y, live, GxEPD_BLACK);
    }

    y += item->RowHeight;
  }
}

// --- Draw stocks on e-ink using Material icons + text ---
void stockWidget( LayoutItem* item ) 
{
  if (!item) return;
  if (stockCount <= 0) {
    drawUnavailableMessage(item);
    return;
  }

  int y = item->PosY + item->RowHeight; // starting Y position

    for (size_t i = 0; i < stockCount; i++) {
        StockItem& s = stocks[i];

        // Determine arrow icon
        uint32_t icon = 0xF01FC;
        if (s.price > s.prevClose) icon = 0xF0737;      // Material arrow_upward
        else if (s.price < s.prevClose) icon = 0xF072E; // Material arrow_downward

        // Draw arrow first
        if (icon != 0) {
            drawSparseChar(&MDI_22_Sparse, item->PosX, y, icon, GxEPD_BLACK);
        }

        // Draw text next to icon
        char buf[128];

        snprintf(buf, sizeof(buf), "%-9s", s.name);
        drawSparseString(&epaperFont, item->PosX + 30, y, buf, GxEPD_BLACK);

        snprintf(buf, sizeof(buf), "%7.2f",s.price );
        
        drawSparseString(&epaperFont, item->PosX + 130, y, buf, GxEPD_BLACK);

        snprintf(buf, sizeof(buf), "%7.2f / ",s.dayHigh );
        drawSparseString(&epaperFont, item->PosX + 240, y, buf, GxEPD_BLACK);

        snprintf(buf, sizeof(buf), "%7.2f",s.dayLow );

        drawSparseString(&epaperFont, item->PosX + 330, y, buf, GxEPD_BLACK);

        y += item->RowHeight; // spacing between rows
    }
}

LayoutItem* getLayout(uint16_t id) {
  int idx = findLayoutIndexByID(id);
  return (idx != -1) ? &layout[idx] : nullptr;
}

// ------------------ Weather widget ------------------
static const char* trackingStatusLabel(int status) {
    switch (status) {
        case -1: case 0: case 1: return "Checking";
        case 2: return "No info yet";
        case 3: return "In transit";
        case 4: return "At pickup point";
        case 5: return "Delivered";
        case 6: return "Not delivered";
        case 7: return "Delivery error";
        case 8: return "Preparing";
        case 9: return "Route ended";
        default: return "Unknown";
    }
}

// One line per parcel: icon, end of the tracking number, status and estimated delivery.
void trackingWidget(LayoutItem* item) {
    if (!item) return;
    int y = item->PosY + 18;
    if (trackingCount == 0) {
        drawSparseString(&epaperFont, item->PosX, y, "No packages being tracked", GxEPD_BLACK);
        return;
    }

    for (int i = 0; i < trackingCount; i++) {
        if (y > item->PosY + item->Height) break;
        const TrackingItem& t = tracking[i];

        uint32_t icon = 0xF053E;                                   // package
        if (t.status == 3 || t.status == 8) icon = 0xF0788;        // truck
        else if (t.status == 5) icon = 0xF1B52;                    // package delivered
        else if (t.status == 6 || t.status == 7) icon = MDI_ALERT;
        drawSparseChar(&MDI_22_Sparse, item->PosX, y + 2, icon, GxEPD_BLACK);

        // The last 8 characters are enough to recognize a parcel.
        size_t len = strlen(t.trackNumber);
        const char* tail = len > 8 ? t.trackNumber + len - 8 : t.trackNumber;
        char line[64];
        int n = snprintf(line, sizeof(line), "%s  %s", tail, trackingStatusLabel(t.status));
        if (t.status != 5 && t.estDelivery > 0 && n > 0 && n < (int)sizeof(line)) {
            struct tm et;
            localtime_r(&t.estDelivery, &et);
            strftime(line + n, sizeof(line) - n, "  ETA %b %d", &et);
        }
        drawSparseString(&epaperFont, item->PosX + 28, y, line, GxEPD_BLACK);
        y += item->RowHeight;
    }
}

void weatherWidget(LayoutItem* item) {
    if (!item) return;
    if (weather.count <= 0) {
        drawUnavailableMessage(item);
        return;
    }

    int x = item->PosX;
    int y = item->PosY+63;

    char buf[32];

    for (int i = 0; i < weather.count; i++) {
        WeatherDay &wd = weather.day[i];

        // Draw icon
        drawSparseChar(&MDI_80_Sparse, x + i*item->ColWidth, y, wd.icon, GxEPD_BLACK);

        // Draw temperatures
        snprintf(buf, sizeof(buf), "%.0f/%.0f°C", wd.temp_min, wd.temp_max);
        drawSparseStringCentered(&epaperFont, x + 40 + i*item->ColWidth, y+30, buf, GxEPD_BLACK);

        // // Draw sunrise/sunset
        // snprintf(buf, sizeof(buf), "%s/%s", wd.sunrise, wd.sunset);
        // drawSparseStringCentered(&epaperFont, x + i*item->Width, y+60, buf, GxEPD_BLACK);

    }
}

// --- Convert Open-Meteo code to MDI icon ---
uint32_t weatherCodeToMDI(int code) {
    switch (code) {
        case 0:           return 0xF0599; // sunny
        case 1: case 2:   return 0xF0595; // partly cloudy
        case 3:           return 0xF0590; // cloudy
        case 45: case 48: return 0xF0591; // fog
        case 51: case 53: case 55: return 0xF0597; // drizzle
        case 61: case 63: case 65: return 0xF0596; // rain
        case 71: case 73: case 75: return 0xF0598; // snow
        case 80: case 81: case 82: return 0xF0596; // rain showers
        case 85: case 86: return 0xF0598;           // snow showers
        case 95: case 96: case 99: return 0xF067E;  // thunderstorm
        default: return 0xF0590;
    }
}

void makerWorldWidget(LayoutItem* item) {
    if (!item) return;
    loadMakerWorld();
    if (!makerworld.available) {
        // No MakerWorld stats yet (no user ID, or never fetched): show the idle printer status instead.
        bambuWidget(item);
        return;
    }

    int x = item->PosX;
    int y = item->PosY + 19;

    char buf[64];

    drawSparseString(&epaperFont, x, y, "Likes :", GxEPD_BLACK);

    snprintf(buf, sizeof(buf), "%7d", makerworld.likes);
    drawSparseString(&epaperFont, x+100, y, buf, GxEPD_BLACK);
    // y += item->RowHeight;

    drawSparseString(&epaperFont, x+200, y, "Downloads :", GxEPD_BLACK);
    snprintf(buf, sizeof(buf), "%7d", makerworld.downloads);
    drawSparseString(&epaperFont, x+300, y, buf, GxEPD_BLACK);
    y += item->RowHeight;

    drawSparseString(&epaperFont, x, y, "Prints :", GxEPD_BLACK);
    snprintf(buf, sizeof(buf), "%7d", makerworld.prints);
    drawSparseString(&epaperFont, x+100, y, buf, GxEPD_BLACK);
    // y += item->RowHeight;

    drawSparseString(&epaperFont, x+200, y, "Collections :", GxEPD_BLACK);

    snprintf(buf, sizeof(buf), "%7d", makerworld.collections);
    drawSparseString(&epaperFont, x+300, y, buf, GxEPD_BLACK);
    y += item->RowHeight;

    drawSparseString(&epaperFont, x, y, "Boosts :", GxEPD_BLACK);
    snprintf(buf, sizeof(buf), "%7d", makerworld.boosts);
    drawSparseString(&epaperFont, x+100, y, buf, GxEPD_BLACK);
}


void saveMakerWorld() {
    prefs.begin("ePaper", false);

    prefs.putInt("mw_likes",       makerworld.likes);
    prefs.putInt("mw_downloads",   makerworld.downloads);
    prefs.putInt("mw_prints",      makerworld.prints);
    prefs.putInt("mw_collections", makerworld.collections);
    prefs.putInt("mw_boosts",      makerworld.boosts);
    prefs.putBool("mw_available",  makerworld.available);

    prefs.end();

    DBG(F("MakerWorld saved to NVS"));
}

void loadMakerWorld() {
    prefs.begin("ePaper", true);

    makerworld.likes       = prefs.getInt("mw_likes", 0);
    makerworld.downloads   = prefs.getInt("mw_downloads", 0);
    makerworld.prints      = prefs.getInt("mw_prints", 0);
    makerworld.collections = prefs.getInt("mw_collections", 0);
    makerworld.boosts      = prefs.getInt("mw_boosts", 0);
    makerworld.available   = prefs.getBool("mw_available", false);

    prefs.end();

    Serial.printf("Loaded MW -> Likes %d Downloads %d\n",
                  makerworld.likes, makerworld.downloads);
}
