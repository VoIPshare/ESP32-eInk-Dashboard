#include "directFetch.h"
#include "calendar.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <time.h>

// Coordinates resolved from a city name survive deep sleep, so geocoding runs only once.
RTC_DATA_ATTR static float cachedLat = 0.0f;
RTC_DATA_ATTR static float cachedLon = 0.0f;
RTC_DATA_ATTR static char cachedLocation[CFG_WEATHER_LOC_MAX] = "";

// ----------------------- Helpers -----------------------

static void urlEncode(const char* in, char* out, size_t cap) {
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  for (; *in && o + 4 < cap; in++) {
    unsigned char c = (unsigned char)*in;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out[o++] = (char)c;
    } else {
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 0x0F];
    }
  }
  out[o] = '\0';
}

static bool httpGetJson(const char* url, JsonDocument& doc, const JsonDocument* filter = nullptr) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  http.setUserAgent("Mozilla/5.0 (ESP32 eInk Dashboard)");

  int code = http.GET();
  if (code != 200) {
    DBGF("[Direct] HTTP %d for %s", code, url);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  DeserializationError err = filter
    ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
    : deserializeJson(doc, body);
  if (err) {
    DBGF("[Direct] JSON error %s for %s", err.c_str(), url);
    return false;
  }
  return true;
}

// ----------------------- Weather (Open-Meteo) -----------------------

static bool resolveLocation(float& lat, float& lon) {
  if (weather_location[0] == '\0') return false;

  if (sscanf(weather_location, "%f , %f", &lat, &lon) == 2) return true;

  if (strcmp(cachedLocation, weather_location) == 0) {
    lat = cachedLat;
    lon = cachedLon;
    return true;
  }

  // "City" or "City, CC" where CC is a 2-letter country code.
  char city[CFG_WEATHER_LOC_MAX];
  char country[3] = "";
  snprintf(city, sizeof(city), "%s", weather_location);
  char* comma = strchr(city, ',');
  if (comma) {
    *comma = '\0';
    const char* cc = comma + 1;
    while (*cc == ' ') cc++;
    if (strlen(cc) == 2) {
      country[0] = (char)toupper((unsigned char)cc[0]);
      country[1] = (char)toupper((unsigned char)cc[1]);
      country[2] = '\0';
    }
  }
  for (int i = (int)strlen(city) - 1; i >= 0 && city[i] == ' '; i--) city[i] = '\0';

  char encoded[CFG_WEATHER_LOC_MAX * 3];
  urlEncode(city, encoded, sizeof(encoded));

  char url[320];
  snprintf(url, sizeof(url),
           "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&format=json%s%s",
           encoded, country[0] ? "&countryCode=" : "", country);

  JsonDocument doc;
  if (!httpGetJson(url, doc)) return false;

  JsonObject r = doc["results"][0];
  if (r.isNull()) {
    DBGF("[Weather] Location not found: %s", weather_location);
    return false;
  }
  lat = r["latitude"] | 0.0f;
  lon = r["longitude"] | 0.0f;

  cachedLat = lat;
  cachedLon = lon;
  snprintf(cachedLocation, sizeof(cachedLocation), "%s", weather_location);
  DBGF("[Weather] %s -> %.4f,%.4f", weather_location, lat, lon);
  return true;
}

bool fetchWeatherDirect(LayoutItem* item) {
  weather.count = 0;

  float lat, lon;
  if (!resolveLocation(lat, lon)) return false;

  int days = MAXWEATHERDAYS;
  if (item && item->ColWidth > 0) days = constrain(item->Width / item->ColWidth, 1, MAXWEATHERDAYS);

  char url[320];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min"
           "&forecast_days=%d&timezone=auto",
           lat, lon, days);

  JsonDocument doc;
  if (!httpGetJson(url, doc)) return false;

  JsonArray codes = doc["daily"]["weather_code"];
  JsonArray tmax = doc["daily"]["temperature_2m_max"];
  JsonArray tmin = doc["daily"]["temperature_2m_min"];
  if (codes.isNull()) return false;

  for (size_t i = 0; i < codes.size() && weather.count < days; i++) {
    WeatherDay& wd = weather.day[weather.count++];
    wd.sunrise[0] = '\0';
    wd.sunset[0] = '\0';
    wd.weather_code = codes[i] | 0;
    wd.temp_max = tmax[i] | 0.0f;
    wd.temp_min = tmin[i] | 0.0f;
    wd.icon = weatherCodeToMDI(wd.weather_code);
  }
  DBGF("[Weather] %d days", weather.count);
  return weather.count > 0;
}

// ----------------------- Stocks (Yahoo Finance) -----------------------

bool fetchStocksDirect(int* failed) {
  stockCount = 0;
  int failures = 0;
  if (stock_symbols[0] == '\0') return false;

  JsonDocument filter;
  JsonObject meta = filter["chart"]["result"][0]["meta"].to<JsonObject>();
  meta["regularMarketPrice"] = true;
  meta["chartPreviousClose"] = true;
  meta["previousClose"] = true;
  meta["regularMarketDayHigh"] = true;
  meta["regularMarketDayLow"] = true;
  meta["fiftyTwoWeekHigh"] = true;
  meta["fiftyTwoWeekLow"] = true;

  char list[CFG_STOCKS_MAX];
  snprintf(list, sizeof(list), "%s", stock_symbols);

  char* save = nullptr;
  for (char* sym = strtok_r(list, ", ", &save); sym && stockCount < MAX_STOCKS; sym = strtok_r(nullptr, ", ", &save)) {
    char encoded[48];
    urlEncode(sym, encoded, sizeof(encoded));

    char url[160];
    snprintf(url, sizeof(url),
             "https://query1.finance.yahoo.com/v8/finance/chart/%s?range=1d&interval=1d", encoded);

    JsonDocument doc;
    if (!httpGetJson(url, doc, &filter)) { failures++; continue; }

    JsonObject m = doc["chart"]["result"][0]["meta"];
    if (m.isNull() || m["regularMarketPrice"].isNull()) {
      DBGF("[Stocks] No quote for %s", sym);
      failures++;
      continue;
    }

    StockItem& s = stocks[stockCount++];
    // Drop the index prefix for display, e.g. "^IXIC" -> "IXIC".
    snprintf(s.name, sizeof(s.name), "%s", sym[0] == '^' ? sym + 1 : sym);
    s.price = m["regularMarketPrice"] | 0.0f;
    s.prevClose = m["chartPreviousClose"] | (m["previousClose"] | 0.0f);
    s.dayHigh = m["regularMarketDayHigh"] | 0.0f;
    s.dayLow = m["regularMarketDayLow"] | 0.0f;
    s.fiftyTwoWeekHigh = m["fiftyTwoWeekHigh"] | 0.0f;
    s.fiftyTwoWeekLow = m["fiftyTwoWeekLow"] | 0.0f;
    DBGF("[Stocks] %s %.2f (prev %.2f)", s.name, s.price, s.prevClose);
  }
  if (failed) *failed = failures;
  return stockCount > 0;
}

// ----------------------- MakerWorld (Bambu API) -----------------------

// Reader for ArduinoJson that buffers the TLS stream and tolerates pauses in a large download
// (Stream::read() returning -1 mid-transfer would otherwise end the JSON as IncompleteInput).
class PatientStreamReader {
public:
  explicit PatientStreamReader(WiFiClient* s) : stream(s) {}

  int read() {
    if (pos >= len && !fill()) return -1;
    return buf[pos++];
  }

  size_t readBytes(char* out, size_t n) {
    size_t done = 0;
    while (done < n) {
      if (pos >= len && !fill()) break;
      size_t chunk = min(n - done, len - pos);
      memcpy(out + done, buf + pos, chunk);
      pos += chunk;
      done += chunk;
    }
    return done;
  }

  size_t total = 0;

private:
  WiFiClient* stream;
  uint8_t buf[1024];
  size_t len = 0, pos = 0;

  bool fill() {
    unsigned long start = millis();
    while (millis() - start < 10000) {
      int avail = stream->available();
      if (avail > 0) {
        len = stream->read(buf, min((size_t)avail, sizeof(buf)));
        pos = 0;
        if (len > 0) { total += len; return true; }
      } else if (!stream->connected()) {
        return false;
      }
      delay(2);
    }
    return false;
  }
};

bool fetchMakerWorldDirect() {
  if (makerworld_uid[0] == '\0') return false;

  // makerworld.com is behind a browser check; Bambu's API host serves the same data directly.
  char url[160];
  snprintf(url, sizeof(url),
           "https://api.bambulab.com/v1/design-service/published/%s/design?limit=100", makerworld_uid);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the ~1 MB answer can be parsed as a stream
  http.setTimeout(15000);
  if (!http.begin(client, url)) return false;
  http.setUserAgent("Mozilla/5.0 (ESP32 eInk Dashboard)");

  int code = http.GET();
  if (code != 200) {
    DBGF("[MakerWorld] HTTP %d", code);
    http.end();
    return false;
  }

  // Keep only the counters of each design; everything else (covers, titles, ...) is skipped.
  JsonDocument filter;
  JsonObject f = filter["hits"][0].to<JsonObject>();
  f["likeCount"] = true;
  f["downloadCount"] = true;
  f["printCount"] = true;
  f["collectionCount"] = true;
  f["boostCnt"] = true;

  unsigned long t0 = millis();
  JsonDocument doc;
  // The skipped parts of each design are nested deeper than the default limit of 10.
  PatientStreamReader reader(http.getStreamPtr());
  DeserializationError err = deserializeJson(doc, reader,
                                             DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(40));
  http.end();
  if (err) {
    DBGF("[MakerWorld] JSON error %s after %u bytes", err.c_str(), (unsigned)reader.total);
    return false;
  }

  JsonArray hits = doc["hits"];
  if (hits.isNull()) return false;

  makerworld = {};
  for (JsonObject h : hits) {
    makerworld.likes       += h["likeCount"] | 0;
    makerworld.downloads   += h["downloadCount"] | 0;
    makerworld.prints      += h["printCount"] | 0;
    makerworld.collections += h["collectionCount"] | 0;
    makerworld.boosts      += h["boostCnt"] | 0;
  }
  makerworld.lastUpdated = time(nullptr);
  makerworld.available = true;
  saveMakerWorld();

  DBGF("[MakerWorld] %u designs in %lu ms: likes %d downloads %d prints %d boosts %d",
       (unsigned)hits.size(), millis() - t0, makerworld.likes, makerworld.downloads,
       makerworld.prints, makerworld.boosts);
  return true;
}

// ----------------------- Calendar (.ics) -----------------------

#define ICS_MAX_EXDATES  16
#define ICS_MAX_BYDAY     7
#define ICS_MAX_STEPS  1500   // safety limit per recurring event
#define ICS_FLIGHT_LOOKAHEAD_S  (7 * 86400)  // flights pinned up to a week ahead

struct IcsEvent {
  char summary[CFG_CAL_TITLE_MAX];
  time_t start;
  time_t end;
  bool hasEnd;
  bool allDay;
  bool cancelled;

  // RRULE subset: FREQ, INTERVAL, UNTIL, COUNT, BYDAY
  char freq;            // 0 = none, 'D', 'W', 'M', 'Y'
  int interval;
  time_t until;         // 0 = none
  int count;            // 0 = none
  int byDayCount;
  int8_t byDayWd[ICS_MAX_BYDAY];  // 0 = Monday ... 6 = Sunday
  int8_t byDayN[ICS_MAX_BYDAY];   // 0 = every, 2 = second, -1 = last (MONTHLY only)

  time_t exdates[ICS_MAX_EXDATES];
  int exdateCount;
};

struct IcsWindow {
  time_t now;
  time_t from;       // local midnight today
  time_t to;         // max(now + 24h, first day of next month)
  time_t monthEnd;
  int month;         // tm_mon of today
  bool days[32];
};

// Days since 1970-01-01 for a civil date (proleptic Gregorian).
static long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static time_t makeLocal(int y, int mon, int d, int hh, int mm, int ss) {
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = d;
  t.tm_hour = hh;
  t.tm_min = mm;
  t.tm_sec = ss;
  t.tm_isdst = -1;
  return mktime(&t);
}

// Parses "20261010", "20261010T140000" (local / TZID) or "20261010T140000Z" (UTC).
static time_t parseIcsValue(const char* v, bool* allDay) {
  int y = 0, mon = 0, d = 0, hh = 0, mm = 0, ss = 0;
  if (sscanf(v, "%4d%2d%2d", &y, &mon, &d) != 3) return 0;
  if (v[8] != 'T') {
    if (allDay) *allDay = true;
    return makeLocal(y, mon, d, 0, 0, 0);
  }
  if (allDay) *allDay = false;
  sscanf(v + 9, "%2d%2d%2d", &hh, &mm, &ss);
  if (v[15] == 'Z') {
    return (time_t)daysFromCivil(y, mon, d) * 86400 + hh * 3600 + mm * 60 + ss;
  }
  // TZID times are treated as the dashboard's own timezone.
  return makeLocal(y, mon, d, hh, mm, ss);
}

static int weekdayIndex(const char* s) {
  static const char* names[] = { "MO", "TU", "WE", "TH", "FR", "SA", "SU" };
  for (int i = 0; i < 7; i++)
    if (s[0] == names[i][0] && s[1] == names[i][1]) return i;
  return -1;
}

static void parseRrule(const char* value, IcsEvent& ev) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%s", value);
  char* save = nullptr;
  for (char* part = strtok_r(buf, ";", &save); part; part = strtok_r(nullptr, ";", &save)) {
    if (!strncmp(part, "FREQ=", 5)) {
      const char* f = part + 5;
      if (!strcmp(f, "DAILY")) ev.freq = 'D';
      else if (!strcmp(f, "WEEKLY")) ev.freq = 'W';
      else if (!strcmp(f, "MONTHLY")) ev.freq = 'M';
      else if (!strcmp(f, "YEARLY")) ev.freq = 'Y';
    } else if (!strncmp(part, "INTERVAL=", 9)) {
      ev.interval = max(1, atoi(part + 9));
    } else if (!strncmp(part, "UNTIL=", 6)) {
      ev.until = parseIcsValue(part + 6, nullptr);
    } else if (!strncmp(part, "COUNT=", 6)) {
      ev.count = atoi(part + 6);
    } else if (!strncmp(part, "BYDAY=", 6)) {
      char* save2 = nullptr;
      for (char* day = strtok_r(part + 6, ",", &save2); day && ev.byDayCount < ICS_MAX_BYDAY; day = strtok_r(nullptr, ",", &save2)) {
        int n = atoi(day);  // "2TU" -> 2, "-1FR" -> -1, "MO" -> 0
        const char* name = day;
        while (*name == '-' || *name == '+' || isdigit((unsigned char)*name)) name++;
        int wd = weekdayIndex(name);
        if (wd < 0) continue;
        ev.byDayWd[ev.byDayCount] = (int8_t)wd;
        ev.byDayN[ev.byDayCount] = (int8_t)n;
        ev.byDayCount++;
      }
    }
  }
}

// Undo iCalendar TEXT escaping: "\," "\;" "\\" and "\n".
static void unescapeText(char* s) {
  char* out = s;
  for (char* in = s; *in; in++) {
    if (*in == '\\' && in[1]) {
      in++;
      *out++ = (*in == 'n' || *in == 'N') ? ' ' : *in;
    } else {
      *out++ = *in;
    }
  }
  *out = '\0';
}

static void addUpcoming(const IcsEvent& ev, time_t s, time_t e) {
  const int cap = sizeof(cal.next24hEvents) / sizeof(cal.next24hEvents[0]);
  int n = cal.eventCount;
  int pos = n;
  while (pos > 0 && cal.next24hEvents[pos - 1].start > s) pos--;
  if (pos >= cap) return;

  int last = (n < cap) ? n : cap - 1;
  for (int i = last; i > pos; i--) cal.next24hEvents[i] = cal.next24hEvents[i - 1];

  CalEventItem& item = cal.next24hEvents[pos];
  snprintf(item.title, sizeof(item.title), "%s", ev.summary);
  item.start = s;
  item.end = e;
  item.allDay = ev.allDay;
  if (n < cap) cal.eventCount++;
}

static void markDays(IcsWindow& w, time_t s, time_t e) {
  time_t cur = max(s, w.from);
  for (int guard = 0; guard < 32 && cur < w.monthEnd; guard++) {
    struct tm lt;
    localtime_r(&cur, &lt);
    if (lt.tm_mon == w.month) w.days[lt.tm_mday] = true;

    lt.tm_mday += 1;
    lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
    lt.tm_isdst = -1;
    cur = mktime(&lt);
    if (cur >= e) break;
  }
}

// Returns false once occurrences are past the window (stop expanding).
static bool emitOccurrence(const IcsEvent& ev, IcsWindow& w, time_t s) {
  if (s >= w.to) return false;
  time_t dur = ev.end - ev.start;
  time_t e = s + dur;
  if (e <= w.from && !(dur == 0 && s >= w.from)) return true;

  for (int i = 0; i < ev.exdateCount; i++)
    if (ev.exdates[i] == s) return true;

  if (e > w.now && s < w.now + 86400) addUpcoming(ev, s, e);
  if (s < w.monthEnd) markDays(w, s, e > s ? e : s + 1);

  // Remember the earliest flight not yet over within the next week: still to depart (pinned with a
  // countdown) or departed but before its calendar end, so a delayed flight keeps its live status.
  if (e > w.now && s < w.now + ICS_FLIGHT_LOOKAHEAD_S &&
      (cal.nextFlight.start == 0 || s < cal.nextFlight.start) && isFlightTitle(ev.summary)) {
    snprintf(cal.nextFlight.title, sizeof(cal.nextFlight.title), "%s", ev.summary);
    cal.nextFlight.start = s;
    cal.nextFlight.end = e;
    cal.nextFlight.allDay = ev.allDay;
  }
  return true;
}

static time_t nthWeekdayOfMonth(int y, int mon, int wd, int n, const struct tm& base) {
  int dim = daysInMonth(y, mon);
  struct tm first = {};
  first.tm_year = y - 1900; first.tm_mon = mon - 1; first.tm_mday = 1; first.tm_hour = 12; first.tm_isdst = -1;
  mktime(&first);
  int firstWd = (first.tm_wday + 6) % 7;  // Monday = 0
  int day;
  if (n > 0) {
    day = 1 + (wd - firstWd + 7) % 7 + (n - 1) * 7;
  } else {
    int lastWd = (firstWd + dim - 1) % 7;
    day = dim - (lastWd - wd + 7) % 7 + (n + 1) * 7;
  }
  if (day < 1 || day > dim) return 0;
  return makeLocal(y, mon, day, base.tm_hour, base.tm_min, base.tm_sec);
}

static void expandEvent(const IcsEvent& ev, IcsWindow& w) {
  if (ev.cancelled || ev.start == 0) return;

  if (!ev.freq) {
    emitOccurrence(ev, w, ev.start);
    return;
  }

  struct tm base;
  localtime_r(&ev.start, &base);
  const int by = base.tm_year + 1900, bm = base.tm_mon + 1, bd = base.tm_mday;
  const int interval = ev.interval > 0 ? ev.interval : 1;
  const time_t dur = ev.end - ev.start;
  int emitted = 0;

  auto accept = [&](time_t t) -> bool {  // false = stop
    if (t < ev.start) return true;
    if (ev.until && t > ev.until) return false;
    if (ev.count && emitted >= ev.count) return false;
    emitted++;
    return emitOccurrence(ev, w, t);
  };

  // Without COUNT, jump close to the window instead of walking from the first occurrence.
  long skipSeconds = ev.count ? 0 : (long)(w.from - dur - ev.start);

  if (ev.freq == 'D') {
    long k = skipSeconds > 0 ? skipSeconds / (86400L * interval) - 1 : 0;
    for (int step = 0; step < ICS_MAX_STEPS; step++, k++) {
      if (k < 0) continue;
      if (!accept(makeLocal(by, bm, bd + (int)(k * interval), base.tm_hour, base.tm_min, base.tm_sec))) return;
    }
  } else if (ev.freq == 'W') {
    int baseWd = (base.tm_wday + 6) % 7;
    int days[ICS_MAX_BYDAY];
    int nDays = 0;
    for (int d = 0; d < 7; d++) {  // weekdays in order Monday..Sunday
      bool use = ev.byDayCount == 0 ? d == baseWd : false;
      for (int i = 0; i < ev.byDayCount; i++) if (ev.byDayWd[i] == d) use = true;
      if (use) days[nDays++] = d;
    }
    long k = skipSeconds > 0 ? skipSeconds / (7L * 86400L * interval) - 1 : 0;
    for (int step = 0; step < ICS_MAX_STEPS; step++, k++) {
      if (k < 0) continue;
      for (int i = 0; i < nDays; i++) {
        int mday = bd - baseWd + (int)(k * 7 * interval) + days[i];
        if (!accept(makeLocal(by, bm, mday, base.tm_hour, base.tm_min, base.tm_sec))) return;
      }
    }
  } else if (ev.freq == 'M') {
    long k = skipSeconds > 0 ? skipSeconds / (31L * 86400L * interval) - 1 : 0;
    for (int step = 0; step < ICS_MAX_STEPS; step++, k++) {
      if (k < 0) continue;
      int months = bm - 1 + (int)(k * interval);
      int y = by + months / 12, mon = months % 12 + 1;
      if (ev.byDayCount > 0 && ev.byDayN[0] != 0) {
        time_t t = nthWeekdayOfMonth(y, mon, ev.byDayWd[0], ev.byDayN[0], base);
        if (t && !accept(t)) return;
        if (!t && makeLocal(y, mon, 1, 0, 0, 0) >= w.to) return;
      } else if (bd <= daysInMonth(y, mon)) {
        if (!accept(makeLocal(y, mon, bd, base.tm_hour, base.tm_min, base.tm_sec))) return;
      }
    }
  } else if (ev.freq == 'Y') {
    long k = skipSeconds > 0 ? skipSeconds / (366L * 86400L * interval) - 1 : 0;
    for (int step = 0; step < ICS_MAX_STEPS; step++, k++) {
      if (k < 0) continue;
      int y = by + (int)(k * interval);
      if (bd > daysInMonth(y, bm)) continue;  // Feb 29 in non-leap years
      if (!accept(makeLocal(y, bm, bd, base.tm_hour, base.tm_min, base.tm_sec))) return;
    }
  }
}

// Buffered line reader over the HTTP stream, with RFC 5545 line unfolding.
class IcsLineReader {
public:
  explicit IcsLineReader(WiFiClient* s) : stream(s) {}

  // Returns false at end of stream.
  bool next(char* out, size_t cap) {
    if (!havePending && !readPhysical(pending, sizeof(pending))) return false;
    havePending = false;
    snprintf(out, cap, "%s", pending);

    // Continuation lines start with a space or tab.
    while (readPhysical(pending, sizeof(pending))) {
      if (pending[0] == ' ' || pending[0] == '\t') {
        size_t len = strlen(out);
        snprintf(out + len, cap - len, "%s", pending + 1);
      } else {
        havePending = true;
        break;
      }
    }
    return true;
  }

private:
  WiFiClient* stream;
  uint8_t buf[1024];
  size_t bufLen = 0, bufPos = 0;
  char pending[320];
  bool havePending = false;

  int readByte() {
    if (bufPos < bufLen) return buf[bufPos++];
    unsigned long start = millis();
    while (millis() - start < 10000) {
      int avail = stream->available();
      if (avail > 0) {
        bufLen = stream->read(buf, min((size_t)avail, sizeof(buf)));
        bufPos = 0;
        if (bufLen > 0) return buf[bufPos++];
      } else if (!stream->connected()) {
        return -1;
      }
      delay(2);
    }
    return -1;
  }

  bool readPhysical(char* out, size_t cap) {
    size_t n = 0;
    int c;
    bool any = false;
    while ((c = readByte()) >= 0) {
      any = true;
      if (c == '\n') break;
      if (c == '\r') continue;
      if (n + 1 < cap) out[n++] = (char)c;
    }
    out[n] = '\0';
    return any;
  }
};

// Downloads one feed and adds its occurrences to `cal` / `w.days`.
static bool readIcsFeed(const char* url, IcsWindow& w) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the raw stream is the file
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) {
    DBGF("[ICS] HTTP %d for %s", code, url);
    http.end();
    return false;
  }

  IcsLineReader reader(http.getStreamPtr());
  static char line[640];
  static IcsEvent ev;
  bool inEvent = false;
  int eventsSeen = 0;
  unsigned long t0 = millis();

  while (reader.next(line, sizeof(line))) {
    if (!inEvent) {
      if (!strcmp(line, "BEGIN:VEVENT")) {
        memset(&ev, 0, sizeof(ev));
        ev.interval = 1;
        inEvent = true;
      }
      continue;
    }

    if (!strcmp(line, "END:VEVENT")) {
      inEvent = false;
      eventsSeen++;
      if (!ev.hasEnd) ev.end = ev.allDay ? ev.start + 86400 : ev.start;
      expandEvent(ev, w);
      continue;
    }

    char* colon = strchr(line, ':');
    if (!colon) continue;
    *colon = '\0';
    const char* name = line;
    char* value = colon + 1;

    if (!strncmp(name, "DTSTART", 7)) {
      ev.start = parseIcsValue(value, &ev.allDay);
    } else if (!strncmp(name, "DTEND", 5)) {
      ev.end = parseIcsValue(value, nullptr);
      ev.hasEnd = true;
    } else if (!strncmp(name, "SUMMARY", 7)) {
      unescapeText(value);
      snprintf(ev.summary, sizeof(ev.summary), "%s", value);
    } else if (!strcmp(name, "RRULE")) {
      parseRrule(value, ev);
    } else if (!strncmp(name, "EXDATE", 6)) {
      char* save = nullptr;
      for (char* v = strtok_r(value, ",", &save); v && ev.exdateCount < ICS_MAX_EXDATES; v = strtok_r(nullptr, ",", &save))
        ev.exdates[ev.exdateCount++] = parseIcsValue(v, nullptr);
    } else if (!strcmp(name, "STATUS")) {
      ev.cancelled = !strcmp(value, "CANCELLED");
    }
  }
  http.end();

  DBGF("[ICS] %d events parsed in %lu ms", eventsSeen, millis() - t0);
  return true;
}

bool fetchCalendarIcs(const char* tz, int* failed) {
  // Keep the previous data if every feed fails (cal lives in RTC memory and stays on screen).
  static CalEvents previous;
  previous = cal;
  cal.eventCount = 0;
  cal.daysCount = 0;
  cal.nextFlight = {};
  if (!hasIcsFeeds()) return false;

  setenv("TZ", (tz && tz[0]) ? tz : "UTC0", 1);
  tzset();

  struct tm nowTm;
  if (!getLocalTime(&nowTm, 10000)) {
    DBG("[ICS] Time not synced");
    return false;
  }

  IcsWindow w = {};
  w.now = time(nullptr);
  w.from = makeLocal(nowTm.tm_year + 1900, nowTm.tm_mon + 1, nowTm.tm_mday, 0, 0, 0);
  w.monthEnd = makeLocal(nowTm.tm_year + 1900, nowTm.tm_mon + 2, 1, 0, 0, 0);
  w.to = max(w.now + ICS_FLIGHT_LOOKAHEAD_S, w.monthEnd);
  w.month = nowTm.tm_mon;

  // All feeds go into the same window, so upcoming events and marked days are merged.
  bool anyOk = false;
  int failures = 0;
  for (int i = 0; i < ICS_MAX_FEEDS; i++) {
    if (ics_urls[i][0] == '\0') continue;
    if (readIcsFeed(ics_urls[i], w)) anyOk = true;
    else failures++;
  }
  if (failed) *failed = failures;

  if (!anyOk) {
    cal = previous;
    return false;
  }

  for (int d = 1; d <= 31 && cal.daysCount < 31; d++)
    if (w.days[d]) cal.daysWithEvents[cal.daysCount++] = d;
  if (cal.nextFlight.start)
    DBGF("[ICS] Next flight: %s", cal.nextFlight.title);

  DBGF("[ICS] %d upcoming, %d days marked", cal.eventCount, cal.daysCount);
  return anyOk;
}

// ----------------------- Package tracking (PKGE) -----------------------

#define TRACKING_KEEP_DELIVERED_S  86400  // delivered parcels stay visible for 1 day

static bool trackingExpired(const TrackingItem& it, time_t now) {
  return it.status == 5 && now - it.lastDate > TRACKING_KEEP_DELIVERED_S;
}

// "2021-07-06T13:46:25+03:00", "2026-10-15 18:05Z", "...Z" or "2021-07-06" -> UTC epoch seconds
// (0 if invalid). Seconds, fractions and the offset are optional.
static time_t parseIsoUtc(const char* s) {
  int y, mo, d, hh = 0, mm = 0, ss = 0, n = 0;
  if (!s || sscanf(s, "%4d-%2d-%2d", &y, &mo, &d) != 3) return 0;
  time_t t = (time_t)daysFromCivil(y, mo, d) * 86400;
  if (s[10] != 'T' && s[10] != ' ') return t;
  const char* p = s + 11;
  if (sscanf(p, "%2d:%2d%n", &hh, &mm, &n) < 2) return t;
  p += n;
  if (*p == ':') { ss = atoi(p + 1); p += 3; }
  while (*p == '.' || isdigit((unsigned char)*p)) p++;  // fractional seconds
  t += hh * 3600 + mm * 60 + ss;
  if (*p == '+' || *p == '-') {
    int oh = 0, om = 0;
    sscanf(p + 1, "%2d:%2d", &oh, &om);
    t -= (oh * 3600 + om * 60) * (*p == '+' ? 1 : -1);
  }
  return t;
}

// Sort order: parcels still on their way first, then by most recent checkpoint.
static bool trackingBefore(const TrackingItem& a, const TrackingItem& b) {
  bool aDone = a.status == 5, bDone = b.status == 5;
  if (aDone != bDone) return !aDone;
  return a.lastDate > b.lastDate;
}

bool trackingDropExpired() {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;  // clock not set yet
  int kept = 0;
  for (int i = 0; i < trackingCount; i++)
    if (!trackingExpired(tracking[i], now)) tracking[kept++] = tracking[i];
  bool dropped = kept != trackingCount;
  trackingCount = kept;
  return dropped;
}

bool fetchTrackingDirect() {
  if (pkge_api_key[0] == '\0') {
    trackingCount = 0;
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the answer can be parsed as a stream
  http.setTimeout(15000);
  if (!http.begin(client, "https://api.pkge.net/v1/packages/list?per-page=100")) return false;
  http.addHeader("X-Api-Key", pkge_api_key);

  int code = http.GET();
  if (code != 200) {
    DBGF("[Tracking] HTTP %d (401 = wrong API key)", code);
    http.end();
    return false;
  }

  // Keep only what the widget shows; checkpoints and addresses are skipped.
  JsonDocument filter;
  JsonObject f = filter["payload"][0].to<JsonObject>();
  f["track_number"] = true;
  f["status"] = true;
  f["last_status_date"] = true;
  f["est_delivery_date_from"] = true;
  f["est_delivery_date_to"] = true;

  JsonDocument doc;
  PatientStreamReader reader(http.getStreamPtr());
  DeserializationError err = deserializeJson(doc, reader,
                                             DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(40));
  http.end();
  if (err) {
    DBGF("[Tracking] JSON error %s", err.c_str());
    return false;
  }

  JsonArray list = doc["payload"];
  if (list.isNull()) {
    DBG(F("[Tracking] Unexpected answer (no payload list)"));
    return false;
  }

  // Built aside and copied at the end, so a failed fetch keeps the previous list.
  TrackingItem found[MAX_TRACKING];
  int count = 0;
  time_t now = time(nullptr);
  int seen = 0;
  for (JsonObject p : list) {
    seen++;
    TrackingItem it = {};
    snprintf(it.trackNumber, sizeof(it.trackNumber), "%s", p["track_number"] | "");
    it.status = p["status"] | 0;
    it.lastDate = parseIsoUtc(p["last_status_date"] | "");
    it.estDelivery = parseIsoUtc(p["est_delivery_date_to"] | (p["est_delivery_date_from"] | ""));
    if (trackingExpired(it, now)) continue;

    // Insert in sorted position, keeping only the first MAX_TRACKING.
    int pos = count;
    while (pos > 0 && trackingBefore(it, found[pos - 1])) pos--;
    if (pos >= MAX_TRACKING) continue;
    int last = count < MAX_TRACKING ? count : MAX_TRACKING - 1;
    for (int i = last; i > pos; i--) found[i] = found[i - 1];
    found[pos] = it;
    if (count < MAX_TRACKING) count++;
  }

  memcpy(tracking, found, sizeof(TrackingItem) * count);
  trackingCount = count;

  DBGF("[Tracking] %d packages in the account, showing %d", seen, trackingCount);
  return true;
}

// ----------------------- Live flight status (AeroDataBox) -----------------------

// Actual/estimated gate time (what airlines and flight apps show): the revised time, else the
// runway (take-off / touchdown) time; 0 when neither is known.
static time_t actualTime(JsonObject movement) {
  time_t t = parseIsoUtc(movement["revisedTime"]["utc"] | "");
  return t ? t : parseIsoUtc(movement["runwayTime"]["utc"] | "");
}

bool fetchFlightStatus(const char* tz) {
  if (aerodatabox_key[0] == '\0' || liveFlight.number[0] == '\0') return false;

  // The API wants the local departure date; use the dashboard's timezone for the calendar time.
  setenv("TZ", (tz && tz[0]) ? tz : "UTC0", 1);
  tzset();
  struct tm lt;
  localtime_r(&liveFlight.calStart, &lt);
  char date[11];
  strftime(date, sizeof(date), "%Y-%m-%d", &lt);

  char url[200];
  snprintf(url, sizeof(url),
           "https://aerodatabox.p.rapidapi.com/flights/Number/%s/%s"
           "?dateLocalRole=Departure&withAircraftImage=false&withLocation=false",
           liveFlight.number, date);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(client, url)) return false;
  http.addHeader("X-RapidAPI-Key", aerodatabox_key);
  http.addHeader("X-RapidAPI-Host", "aerodatabox.p.rapidapi.com");

  int code = http.GET();
  if (code != 200) {
    DBGF("[Flight] HTTP %d for %s on %s (204/404 = flight not found, 401/403 = key or plan)",
         code, liveFlight.number, date);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument filter;
  JsonObject f = filter[0].to<JsonObject>();
  f["status"] = true;
  for (const char* dir : { "departure", "arrival" }) {
    f[dir]["scheduledTime"]["utc"] = true;
    f[dir]["revisedTime"]["utc"] = true;
    f[dir]["runwayTime"]["utc"] = true;
    f[dir]["gate"] = true;
    f[dir]["terminal"] = true;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    DBGF("[Flight] JSON error %s", err.c_str());
    return false;
  }

  // A flight number can fly more than once a day: take the leg closest to the calendar time.
  JsonObject best;
  time_t bestDiff = 0;
  for (JsonObject fl : doc.as<JsonArray>()) {
    time_t dep = parseIsoUtc(fl["departure"]["scheduledTime"]["utc"] | "");
    time_t diff = dep > liveFlight.calStart ? dep - liveFlight.calStart : liveFlight.calStart - dep;
    if (best.isNull() || diff < bestDiff) { best = fl; bestDiff = diff; }
  }
  if (best.isNull()) {
    DBGF("[Flight] No %s flight found on %s", liveFlight.number, date);
    return false;
  }

  JsonObject dep = best["departure"], arr = best["arrival"];
  snprintf(liveFlight.status, sizeof(liveFlight.status), "%s", best["status"] | "Unknown");
  liveFlight.depScheduled = parseIsoUtc(dep["scheduledTime"]["utc"] | "");
  liveFlight.depActual = actualTime(dep);
  liveFlight.arrScheduled = parseIsoUtc(arr["scheduledTime"]["utc"] | "");
  liveFlight.arrActual = actualTime(arr);
  snprintf(liveFlight.depGate, sizeof(liveFlight.depGate), "%s", dep["gate"] | "");
  snprintf(liveFlight.depTerminal, sizeof(liveFlight.depTerminal), "%s", dep["terminal"] | "");
  liveFlight.valid = true;

  DBGF("[Flight] %s %s, gate %s", liveFlight.number, liveFlight.status,
       liveFlight.depGate[0] ? liveFlight.depGate : "-");
  DBGF("[Flight] UTC dep sched %s rev %s rwy %s | arr sched %s rev %s rwy %s",
       dep["scheduledTime"]["utc"] | "-", dep["revisedTime"]["utc"] | "-", dep["runwayTime"]["utc"] | "-",
       arr["scheduledTime"]["utc"] | "-", arr["revisedTime"]["utc"] | "-", arr["runwayTime"]["utc"] | "-");
  return true;
}
