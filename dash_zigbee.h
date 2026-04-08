#pragma once

#include "configure.h"

extern bool s_light_state;
extern bool s_zigbee_linked;
extern bool s_zigbee_pairing_failed;

#if USE_ZIGBEE
#include <Preferences.h>
#include <Zigbee.h>

#define SWITCH_ENDPOINT_NUMBER 5

void setUpZigbee();
void activateCoordinatorReadAndClose(bool flipSwitch);
void closeZigbee();

void activateCoordinator();
void readZigbee();
bool syncZigbeePower(bool turnOn);
bool ensureZigbeePaired();

void triggerZigbeeSwitch();

bool switchOn();
bool switchOff();
void toogleZigbee();
#else
inline void setUpZigbee() {}
inline void activateCoordinatorReadAndClose(bool) {}
inline void closeZigbee() {}
inline void activateCoordinator() {}
inline void readZigbee() {}
inline bool syncZigbeePower(bool) { return false; }
inline void triggerZigbeeSwitch() {}
inline bool switchOn() { return false; }
inline bool switchOff() { return false; }
inline void toogleZigbee() {}
#endif
