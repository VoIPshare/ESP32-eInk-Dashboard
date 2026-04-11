#pragma once
#include "app.h"
#include <ArduinoJson.h>
#include <time.h>

#define MAXLAYOUTS      15
#define MAXWEATHERDAYS  7  // adjust as needed
#define MAX_TRACKING    5
#define MAX_STOCKS      5

// Structures for layout, stocks, and tracking (existing)
struct LayoutItem {
  char Description[64];
  int ID;
  int PosY;
  int PosX;
  int Width;
  int Height;
  int RowHeight;
  int ColWidth;
  int Refresh;
  int Group;
  char Extra1[64];
  char Extra2[64];
  char Extra3[64];
  char Extra4[64]; 
  char Extra5[64]; 

  bool Active;
};

struct StockItem {
  char name[16];
  float price;
  float dayLow;
  float dayHigh;
  float prevClose;
  float fiftyTwoWeekHigh;
  float fiftyTwoWeekLow;
};

struct TrackingItem {
  char tracking[40];
  char status[32];
  char deliveryStatus[32];
  char lastChecked[32];
  bool cached;
};

struct CalEventItem {
  char title[CFG_CAL_TITLE_MAX];
  time_t  start;
  time_t  end;
  bool allDay;
};

struct CalEvents {
  int daysWithEvents[31];
  int daysCount;
  CalEventItem next24hEvents[10];
  int eventCount;
};

// ------------------ New: Weather ------------------
struct WeatherDay {
  char sunrise[16];
  char sunset[16];
  float temp_max;
  float temp_min;
  int weather_code;
  uint32_t icon;  // e.g., Material Design Icons code
};

struct WeatherData {
  WeatherDay day[MAXWEATHERDAYS];
  int count;
};


// ------------------ MakerWorld ------------------
struct MakerWorld {
    int likes;
    int downloads;
    int prints;
    int collections;
    int boosts;
    time_t lastUpdated;   // optional: store last update timestamp
    bool available;
};

extern MakerWorld makerworld;



// --- Declare global variables ---
extern StockItem stocks[5];
extern int stockCount;

extern LayoutItem layout[MAXLAYOUTS];
extern int layoutCount;

extern TrackingItem tracking[5];
extern int trackingCount;

extern CalEvents cal;

// New weather globals
extern WeatherData weather;

extern uint16_t bootCount;

bool fetchData();
void gCalWidget(LayoutItem*);
void stockWidget(LayoutItem*);
void trackingWidget(LayoutItem*);
void weatherWidget(LayoutItem*); // new widget

int findLayoutIndexByID(int);

#if DEBUG
void debugPrintLayout();
#endif

void saveLayout();
bool loadLayout();
time_t parseISO8601(const char*);
void formatTimeRange(time_t start, time_t end, char* buf, size_t bufLen);
LayoutItem* getLayout(uint16_t);

// ------------------ Weather helpers ------------------
uint32_t weatherCodeToMDI(int code);


void makerWorldWidget(LayoutItem*);

void saveMakerWorld();
void loadMakerWorld();

inline void copyJsonString(char *dest, size_t size, const JsonVariantConst& src)
{
    strncpy(dest, src | "", size - 1);
    dest[size - 1] = '\0';
}
