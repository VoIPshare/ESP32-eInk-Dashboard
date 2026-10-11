#pragma once
#include "configure.h"
#include "display.h"
#include <Arduino.h>
#include <cstring>

// Fixed buffers avoid Arduino String heap churn on long-running / deep-sleep cycles.
#define CFG_WIFI_SSID_MAX     33u   // 32 + NUL (802.11 SSID)
#define CFG_WIFI_PASS_MAX      65u   // WPA2-PSK max + NUL
#define CFG_MQTT_PASS_MAX      65u
#define CFG_MQTT_SN_MAX        65u
#define CFG_MQTT_IP_MAX        64u   // IPv4 literal or short hostname
#define CFG_ZIGBEE_STR_MAX     33u
#define CFG_CAL_TITLE_MAX     128u
#define CFG_TIMEZONE_MAX       65u
#define CFG_WEATHER_LOC_MAX    65u   // "City, CC" or "lat,lon"
#define CFG_STOCKS_MAX         81u   // comma separated symbols
#define CFG_ICS_URL_MAX       257u   // iCal feed URL
#define CFG_MW_UID_MAX         24u   // MakerWorld user id (digits)
#define CFG_PRINTER_NAME_MAX   24u   // printer widget title
#define CFG_PKGE_KEY_MAX       96u   // PKGE Business API key
#define CFG_ADB_KEY_MAX        96u   // RapidAPI key for AeroDataBox

extern char wifi_ssid[CFG_WIFI_SSID_MAX];
extern char wifi_pass[CFG_WIFI_PASS_MAX];
extern char mqtt_pass[CFG_MQTT_PASS_MAX];
extern char mqtt_sn[CFG_MQTT_SN_MAX];
extern char mqtt_ip[CFG_MQTT_IP_MAX];
extern uint16_t mqtt_port;
extern char device_timezone[CFG_TIMEZONE_MAX];
extern char weather_location[CFG_WEATHER_LOC_MAX];
extern char stock_symbols[CFG_STOCKS_MAX];
extern char makerworld_uid[CFG_MW_UID_MAX];
extern char printer_name[CFG_PRINTER_NAME_MAX];
extern char pkge_api_key[CFG_PKGE_KEY_MAX];
extern char aerodatabox_key[CFG_ADB_KEY_MAX];

#define ICS_MAX_FEEDS 3
extern char ics_urls[ICS_MAX_FEEDS][CFG_ICS_URL_MAX];

inline bool hasIcsFeeds() {
    for (int i = 0; i < ICS_MAX_FEEDS; i++)
        if (ics_urls[i][0] != '\0') return true;
    return false;
}

