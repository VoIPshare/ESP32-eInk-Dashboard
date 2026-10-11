#pragma once
#include "app.h"
#include <ArduinoJson.h>
#include <time.h>

#define MAXLAYOUTS      15
#define MAXWEATHERDAYS  7  // adjust as needed
#define MAX_STOCKS      5
#define MAX_TRACKING    5

// Structures for layout and widget data
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

// One parcel from the PKGE account (see fetchTrackingDirect).
struct TrackingItem {
  char trackNumber[40];
  int status;            // PKGE status code: 3 in transit, 4 at pickup point, 5 delivered, ...
  time_t lastDate;       // date of the latest checkpoint
  time_t estDelivery;    // estimated delivery date, 0 if unknown
};

// Live status of the next flight from the calendar (AeroDataBox), kept in RTC memory.
struct LiveFlight {
  char number[12];       // "TS399", taken from the calendar title
  char title[CFG_CAL_TITLE_MAX];
  time_t calStart;       // departure time in the calendar (identifies the flight)
  time_t calEnd;         // end time of the calendar event (expected landing if no live data)
  time_t lastFetch;
  uint8_t requests;      // AeroDataBox requests made for this flight (capped)
  bool valid;            // live data received
  char status[20];       // AeroDataBox status: Expected, CheckIn, Boarding, Departed, Delayed, Arrived, ...
  time_t depScheduled, depActual;   // UTC; "actual" = revised/runway time when known, else 0
  time_t arrScheduled, arrActual;
  char depGate[8];
  char depTerminal[8];
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
  CalEventItem nextFlight;   // earliest flight not yet over (calendar end in the future) within 7 days; start == 0: none
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


extern CalEvents cal;

extern TrackingItem tracking[MAX_TRACKING];
extern int trackingCount;

// New weather globals
extern WeatherData weather;

extern uint16_t bootCount;

void gCalWidget(LayoutItem*);
void stockWidget(LayoutItem*);
void weatherWidget(LayoutItem*); // new widget
void trackingWidget(LayoutItem*);

// True for calendar titles that look like a flight: "TS399 PUJ->YUL", "AC 871 YUL to YYZ",
// "Flight to Toronto (AC 871)".
bool isFlightTitle(const char* title);
// Copies the flight number ("TS399", spaces removed) from a title; false if there is none.
bool extractFlightNumber(const char* title, char* out, size_t outSize);

extern LiveFlight liveFlight;

int findLayoutIndexByID(int);

#if DEBUG
void debugPrintLayout();
#endif

void loadDefaultLayout();
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
