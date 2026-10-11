#pragma once
#include "fetchAllInfo.h"

// Widget data fetched directly by the ESP32. Each fetch fills the globals the widgets draw
// from (weather, stocks, cal, makerworld).

// Open-Meteo forecast. Location comes from `weather_location`: "City", "City, CC" or "lat,lon".
bool fetchWeatherDirect(LayoutItem* item);

// Yahoo Finance quotes for the comma separated symbols in `stock_symbols`.
// Returns true if at least one quote was read; `failed` (optional) gets the number of symbols that failed.
bool fetchStocksDirect(int* failed = nullptr);

// Parcels in the PKGE account of `pkge_api_key` (packages are added on the PKGE side).
// Keeps the active ones and those delivered in the last day, newest first.
bool fetchTrackingDirect();
// Removes parcels delivered more than a day ago from the shown list (no download).
// Returns true if any was removed, so the widget needs a redraw.
bool trackingDropExpired();

// Live status (delay, gate, departed/landed) of `liveFlight` from AeroDataBox via RapidAPI,
// using `aerodatabox_key`. `tz` is used to compute the local departure date.
bool fetchFlightStatus(const char* tz);

// MakerWorld totals (likes, downloads, prints, collections, boosts) for `makerworld_uid`.
// On success the totals are stored in NVS (saveMakerWorld) for makerWorldWidget.
bool fetchMakerWorldDirect();

// iCalendar (.ics) feeds from `ics_urls` (up to ICS_MAX_FEEDS, merged),
// e.g. Google Calendar's public or secret address in iCal format.
// `tz` is the POSIX TZ string used to interpret floating / TZID times and day boundaries.
// Returns true if at least one feed was read; `failed` (optional) gets the number of feeds that failed.
bool fetchCalendarIcs(const char* tz, int* failed = nullptr);
