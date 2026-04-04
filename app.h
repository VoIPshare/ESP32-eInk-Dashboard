#pragma once
#include "configure.h"
#include "display.h"
#include <Arduino.h>
#include <cstring>

// Fixed buffers avoid Arduino String heap churn on long-running / deep-sleep cycles.
#define CFG_WIFI_SSID_MAX     33u   // 32 + NUL (802.11 SSID)
#define CFG_WIFI_PASS_MAX      65u   // WPA2-PSK max + NUL
#define CFG_GOOGLEAPI_MAX     131u   // deployment id + NUL (matches URL builder)
#define CFG_MQTT_PASS_MAX      65u
#define CFG_MQTT_SN_MAX        65u
#define CFG_MQTT_IP_MAX        64u   // IPv4 literal or short hostname
#define CFG_ZIGBEE_STR_MAX     33u
#define CFG_CAL_TITLE_MAX     128u
#define CFG_PROXMOX_NAME_MAX   96u

extern char wifi_ssid[CFG_WIFI_SSID_MAX];
extern char wifi_pass[CFG_WIFI_PASS_MAX];
extern char mqtt_pass[CFG_MQTT_PASS_MAX];
extern char mqtt_sn[CFG_MQTT_SN_MAX];
extern char mqtt_ip[CFG_MQTT_IP_MAX];
extern uint16_t mqtt_port;
extern char googleapi[CFG_GOOGLEAPI_MAX];

inline const char* getApiUrl() {
    static char url[200];
    if (googleapi[0] == '\0') return nullptr;
    size_t len = std::strlen(googleapi);
    if (len > 130) {
        Serial.println("API key too long!");
        return nullptr;
    }
    snprintf(url, sizeof(url),
             "https://script.google.com/macros/s/%s/exec",
             googleapi);
    return url;
}