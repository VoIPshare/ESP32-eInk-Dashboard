#pragma once
#include "configure.h"
#include "display.h"
#include <Arduino.h>

extern String wifi_ssid;
extern String wifi_pass;
extern String mqtt_pass;
extern String mqtt_sn;
extern String mqtt_ip;
extern uint16_t mqtt_port;
extern String api_key;
extern String apiUrl;

inline const char* getApiUrl() {
    static char url[200];
    if (api_key.length() == 0) return nullptr;
    // Check it fits: base URL is 55 chars, leave room for key
    if (api_key.length() > 130) {
        Serial.println("API key too long!");
        return nullptr;
    }
    snprintf(url, sizeof(url),
             "https://script.google.com/macros/s/%s/exec",
             api_key.c_str());
    return url;
}