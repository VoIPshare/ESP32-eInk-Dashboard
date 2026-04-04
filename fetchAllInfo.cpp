#include "app.h"
#include "configure.h"
#include "fetchAllInfo.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>

// Arrays to store the data
LayoutItem layout[MAXLAYOUTS];       // Adjust size as needed
StockItem stocks[5];
TrackingItem tracking[5];
CalEvents cal;

WeatherData weather;

MakerWorld makerworld;

// Counts
int layoutCount = 0;
int stockCount = 0;
int trackingCount = 0;
// int calEventCount=0;
Preferences prefs;


void saveLayout() {
    prefs.begin("ePaper", false);
    char key[12];
    for (int i = 0; i < layoutCount; i++) {
        StaticJsonDocument<1024> doc;

        LayoutItem &item = layout[i];
        doc["ID"] = item.ID;
        doc["PosX"] = item.PosX;
        doc["PosY"] = item.PosY;
        doc["Width"] = item.Width;
        doc["Height"] = item.Height;
        doc["RowHeight"] = item.RowHeight;
        doc["ColWidth"] = item.ColWidth;
        doc["Refresh"] = item.Refresh;
        doc["Group"] = item.Group;
        doc["Active"] = item.Active;
        doc["Description"] = item.Description;
        doc["Extra1"] = item.Extra1;
        doc["Extra2"] = item.Extra2;
        doc["Extra3"] = item.Extra3;
        doc["Extra4"] = item.Extra4;
        doc["Extra5"] = item.Extra5;

        char buffer[1024];
        size_t n = serializeJson(doc, buffer);
        // prefs.putBytes(("layout" + String(i)).c_str(), buffer, n);
        snprintf(key, sizeof(key), "layout%d", i);
        prefs.putBytes(key, buffer, n);

    }

    prefs.putInt("layoutCount", layoutCount);
    prefs.end();
}

bool loadLayout() {
    prefs.begin("ePaper", true);

    layoutCount = prefs.getInt("layoutCount", 0);
    if (layoutCount == 0) {
      prefs.end();
      return false;  // Nothing loaded
    }

    for (int i = 0; i < layoutCount; i++) {
        char buffer[1024];
        char key[12];
        snprintf(key, sizeof(key), "layout%d", i);
        size_t size = prefs.getBytesLength(key);
        prefs.getBytes(key, buffer, size);
        // size_t size = prefs.getBytesLength(("layout" + String(i)).c_str());
        if (size == 0) continue;

        
        // prefs.getBytes(("layout" + String(i)).c_str(), buffer, size);

        StaticJsonDocument<1024> doc;
        deserializeJson(doc, buffer);

        LayoutItem &item = layout[i];
        item.ID = doc["ID"] | 0;
        item.PosX = doc["PosX"] | 0;
        item.PosY = doc["PosY"] | 0;
        item.Width = doc["Width"] | 0;
        item.Height = doc["Height"] | 0;
        item.RowHeight = doc["RowHeight"] | 0;
        item.ColWidth = doc["ColWidth"] | 0;
        item.Refresh = doc["Refresh"] | 0;
        item.Group = doc["Group"] | 0;
        item.Active = doc["Active"] | false;
        copyJsonString(item.Description, sizeof(item.Description), doc["Description"]);
        copyJsonString(item.Extra1, sizeof(item.Extra1), doc["Extra1"]);
        copyJsonString(item.Extra2, sizeof(item.Extra2), doc["Extra2"]);
        copyJsonString(item.Extra3, sizeof(item.Extra3), doc["Extra3"]);
        copyJsonString(item.Extra4, sizeof(item.Extra4), doc["Extra4"]);
        copyJsonString(item.Extra5, sizeof(item.Extra5), doc["Extra5"]);
    }

    prefs.end();
    return true;
}

// ----------------------- Fetch data from Google Script -----------------------
// This endpoint is the "single source of truth" for layout + multi-widget data.
// Layout is cached in Preferences so we can render something even when Wi-Fi fails.
void fetchData() {
    stockCount = layoutCount = trackingCount = cal.eventCount = weather.count = 0;

    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(20000);
    Serial.println( getApiUrl() );
    http.begin( getApiUrl() );

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("HTTP GET failed, code %d\n", httpCode);
        http.end();
        return;
    }

    String payload = http.getString();
    const size_t capacity = 60 * 1024;
    DynamicJsonDocument doc(capacity);

    if (deserializeJson(doc, payload)) {
        Serial.println("JSON parse failed!");
        http.end();
        return;
    }

    http.end();

    // ----------------------- Parse Layout -----------------------
    // Layout drives widget placement, refresh cadence, and per-widget parameters (Extra1..Extra5).
    if (doc["layout"].is<JsonArray>()  && doc["layout"].size() > 0 ) {
        layoutCount = 0;
        for (JsonObject item : doc["layout"].as<JsonArray>()) {
            if (layoutCount >= MAXLAYOUTS ) break;
            LayoutItem &l = layout[layoutCount];
            l.ID = item["ID"] | 0;
            l.PosX = item["PosX"] | 0;
            l.PosY = item["PosY"] | 0;
            l.Width = item["Width"] | 0;
            l.Height = item["Height"] | 0;
            l.RowHeight = item["Row_Height"] | 0;
            l.ColWidth = item["Col_Width"] | 0;
            l.Group = item["Group"] | 0;
            l.Active = item["Active"] | false;
            l.Refresh = item["Refresh"] | 0;

            snprintf(l.Description, sizeof(l.Description), "%s", item["Description"] | "");
            snprintf(l.Extra1, sizeof(l.Extra1), "%s", item["Extra1"] | "");
            snprintf(l.Extra2, sizeof(l.Extra2), "%s", item["Extra2"] | "");
            snprintf(l.Extra3, sizeof(l.Extra3), "%s", item["Extra3"] | "");
            snprintf(l.Extra4, sizeof(l.Extra4), "%s", item["Extra4"] | "");
            snprintf(l.Extra5, sizeof(l.Extra5), "%s", item["Extra5"] | "");

            layoutCount++;
        }
        DBGF( "Layout Count %d\n",layoutCount);
    }

    // ----------------------- Parse Stocks -----------------------
    if (doc["stocks"].is<JsonArray>()) {
        stockCount = 0;
        for (JsonObject item : doc["stocks"].as<JsonArray>()) {
            if ( stockCount >= MAX_STOCKS ) break;
            StockItem &s = stocks[stockCount];
            // s.name = item["name"] | "";
            snprintf(s.name, sizeof(s.name), "%s", item["name"] | "");
            s.price = item["price"] | 0.0f;
            s.dayLow = item["dayLow"] | 0.0f;
            s.dayHigh = item["dayHigh"] | 0.0f;
            s.prevClose = item["prevClose"] | 0.0f;
            s.fiftyTwoWeekHigh = item["fiftyTwoWeekHigh"] | 0.0f;
            s.fiftyTwoWeekLow = item["fiftyTwoWeekLow"] | 0.0f;
            stockCount++;
        }
    }

    // ----------------------- Parse Tracking -----------------------
    if (doc["tracking"].is<JsonArray>()) {
        trackingCount = 0;
        for (JsonObject item : doc["tracking"].as<JsonArray>()) {
            if ( trackingCount >= MAX_TRACKING ) break;
            TrackingItem &t = tracking[trackingCount];
            snprintf(t.tracking,       sizeof(t.tracking),       "%s", item["tracking"]       | "");
            snprintf(t.status,         sizeof(t.status),          "%s", item["status"]         | "");
            snprintf(t.deliveryStatus, sizeof(t.deliveryStatus),  "%s", item["deliveryStatus"] | "");
            snprintf(t.lastChecked,    sizeof(t.lastChecked),     "%s", item["lastChecked"]    | "");
            t.cached = item["cached"] | false;
            trackingCount++;
        }
    }

    // ----------------------- Parse Calendar Events -----------------------
    // Calendar timestamps are parsed to epoch seconds; rendering uses current TZ.
    if (doc["calEvents"]["next24hEvents"].is<JsonArray>()) {
        cal.eventCount = 0;
        for (int i = 0; i < doc["calEvents"]["next24hEvents"].size() && i < 10; i++) {
            JsonObject ev = doc["calEvents"]["next24hEvents"][i].as<JsonObject>();
            copyJsonString(cal.next24hEvents[i].title,
                           sizeof(cal.next24hEvents[i].title),
                           ev["title"]);
            cal.next24hEvents[i].start = parseISO8601(ev["start"] | "");
            cal.next24hEvents[i].end = parseISO8601(ev["end"] | "");
            cal.next24hEvents[i].allDay = ev["allDay"] | false;
            cal.eventCount++;
        }
    }
    // DBG(F("Days With Events"));
    if (doc["calEvents"]["daysWithEvents"].is<JsonArray>()) {
        int k = 0;
        int maxDays = sizeof(cal.daysWithEvents) / sizeof(cal.daysWithEvents[0]); // 31
        for (int i = 0; i < doc["calEvents"]["daysWithEvents"].size() && k < maxDays; i++) {
            cal.daysWithEvents[k++] = doc["calEvents"]["daysWithEvents"][i].as<int>();
        }
        cal.daysCount = k; // optional: store how many days were actually read
    }

    // ------------------ Weather parsing ------------------
    if (doc["weather"].is<JsonArray>()) {
        weather.count = 0;
        for (JsonObject w : doc["weather"].as<JsonArray>()) {
            if (weather.count >= MAXWEATHERDAYS) break;
            WeatherDay &wd = weather.day[weather.count];

            snprintf(wd.sunrise, sizeof(wd.sunrise), "%s", w["sunrise"] | "");
            snprintf(wd.sunset,  sizeof(wd.sunset),  "%s", w["sunset"]  | "");
            wd.temp_max = w["temp_max"] | 0.0f;
            wd.temp_min = w["temp_min"] | 0.0f;
            wd.weather_code = w["weather_code"] | 0;
            wd.icon = weatherCodeToMDI(wd.weather_code);

            weather.count++;
        }
        Serial.printf("Weather count %d\n", weather.count);
    }

    // ------------------ fetchData() update ------------------
    // After parsing weather, add:

    if (doc["makerworld"].is<JsonObject>()) {
        JsonObject mw = doc["makerworld"].as<JsonObject>();
        makerworld.likes       = mw["total"]["likes"]       | 0;
        makerworld.downloads   = mw["total"]["downloads"]   | 0;
        makerworld.prints      = mw["total"]["prints"]      | 0;
        makerworld.collections = mw["total"]["collections"] | 0;
        makerworld.boosts      = mw["total"]["boosts"]      | 0;
        // Optional: parse lastUpdated
        const char* ts = mw["lastUpdated"] | "";
        makerworld.lastUpdated = parseISO8601(ts);
        saveMakerWorld(); 
    }
    saveLayout();
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

// ----------------------- Time parsing -----------------------
// Parse a basic "YYYY-MM-DDTHH:MM:SS" timestamp as UTC and return epoch seconds.
// Note: this function mutates the global TZ temporarily.
time_t parseISO8601(const char* str) {
    // Save and restore the *actual* current TZ instead:
    const char* savedTZ = getenv("TZ");

    struct tm t = {0};
    if (sscanf(str, "%d-%d-%dT%d:%d:%d",
               &t.tm_year, &t.tm_mon, &t.tm_mday,
               &t.tm_hour, &t.tm_min, &t.tm_sec) != 6) return 0;

    t.tm_year -= 1900;
    t.tm_mon  -= 1;

    // Treat the incoming timestamp as UTC.
    setenv("TZ", "UTC0", 1); 
    tzset();
    time_t epoch = mktime(&t);

    // Restore a default TZ (clock widget can still override TZ later).
    if (savedTZ) setenv("TZ", savedTZ, 1);
    else unsetenv("TZ");

    // setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();

    return epoch;
}

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

void gCalWidget( LayoutItem* item )
{
  int lastDay = -1;

  int y = item->PosY+30;

  for (size_t i = 0; i < cal.eventCount; i++)
  {
    struct tm t;
    localtime_r(&cal.next24hEvents[i].start, &t);

    // If new day → print day header
    if (t.tm_mday != lastDay)
    {
      y+=5;
      lastDay = t.tm_mday;

      char dayBuf[32];
      strftime(dayBuf, sizeof(dayBuf), "%A %d, %B", &t);

      drawSparseString(&epaperFont, item->PosX, y, dayBuf, GxEPD_BLACK);

      y += item->RowHeight+5;
    }

    char timeRange[32];
    formatTimeRange(cal.next24hEvents[i].start, cal.next24hEvents[i].end,
                    timeRange, sizeof(timeRange));
    char line[CFG_CAL_TITLE_MAX + 40];
    snprintf(line, sizeof(line), "%s %s", timeRange, cal.next24hEvents[i].title);

    drawSparseString(&epaperFont, item->PosX, y, line, GxEPD_BLACK);

    y += item->RowHeight;
  }
}

// --- Draw stocks on e-ink using Material icons + text ---
void stockWidget( LayoutItem* item ) 
{
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

void trackingWidget( LayoutItem* item ) 
{
    DBG(F("Tracking: Widget"));
    int y = item->PosY+10; // starting Y position

    for (size_t i = 0; i < trackingCount; i++) {
        TrackingItem& s = tracking[i];

        uint32_t icon = 0xF053E;

        if (strcmp(s.status, "delivered") == 0)
            icon = 0xF1B52;   // package delivered icon
        else if (strcmp(s.deliveryStatus, "in_transit") == 0)
          icon = 0xF0788; // Trucking moving
        drawSparseChar(&MDI_22_Sparse, item->PosX, y,icon,GxEPD_BLACK );

        drawSparseString(&epaperFont, item->PosX + 30, y, s.tracking, GxEPD_BLACK);

        drawSparseString(&epaperFont, item->PosX + 130, y, s.deliveryStatus, GxEPD_BLACK);
        y+=item->RowHeight;
        drawSparseString(&epaperFont, item->PosX,y, s.status, GxEPD_BLACK);

        y+=item->RowHeight;
    }

}

LayoutItem* getLayout(uint16_t id) {
  int idx = findLayoutIndexByID(id);
  return (idx != -1) ? &layout[idx] : nullptr;
}

// ------------------ Weather widget ------------------
void weatherWidget(LayoutItem* item) {
    if (!item) return;

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

    prefs.end();

    Serial.printf("Loaded MW -> Likes %d Downloads %d\n",
                  makerworld.likes, makerworld.downloads);
}
