#include "dash_zigbee.h"

bool s_light_state = false;
bool s_zigbee_linked = false;
bool s_zigbee_pairing_failed = false;

#if USE_ZIGBEE

// Zigbee switch endpoint used to control the paired Ikea switch.
static ZigbeeSwitch zbSwitch(SWITCH_ENDPOINT_NUMBER);
static bool s_endpoint_registered = false;

static void onLightStateChange(bool state) {
  s_light_state = state;
  Serial.printf("[ZB] Light state -> %s\n", state ? "ON" : "OFF");
}

static void printBoundDevicesInfo() {
  if (!zbSwitch.bound()) {
    Serial.println("[ZB] No bound devices found.");
    return;
  }

  Serial.println("\n[ZB] Bound devices info:");
  for (const auto &device : zbSwitch.getBoundDevices()) {
    if (!device) continue;
    Serial.printf("[ZB] Bound endpoint=%u short=0x%04x\n", device->endpoint, device->short_addr);
  }
  zbSwitch.getLightState();
}

static bool beginCoordinatorSession(bool allowPairing, uint32_t waitForBindingMs) {
  s_light_state = false;
  Serial.println("\n[ZB] Activating Zigbee coordinator");

  zbSwitch.onLightStateChange(onLightStateChange);
  zbSwitch.allowMultipleBinding(true);
  zbSwitch.setManufacturerAndModel("DIY", "ESP32-C6-Coordinator");

  if (!s_endpoint_registered) {
    Zigbee.addEndpoint(&zbSwitch);
    s_endpoint_registered = true;
  }
  Zigbee.setRebootOpenNetwork(0);

  if (!Zigbee.begin(ZIGBEE_COORDINATOR)) {
    Serial.println("[ZB] Failed to start Zigbee");
    ESP.restart();
  }

  if (allowPairing) {
    Serial.printf("[ZB] Opening pairing window for %lu seconds\n", waitForBindingMs / 1000UL);
    Zigbee.openNetwork(waitForBindingMs / 1000UL);
  }

  uint32_t waitStart = millis();
  while (!zbSwitch.bound() && (millis() - waitStart) < waitForBindingMs) {
    Serial.print(".");
    delay(500);
  }

  s_zigbee_linked = zbSwitch.bound();
  s_zigbee_pairing_failed = allowPairing && !s_zigbee_linked;
  if (!s_zigbee_linked) {
    Serial.println("\n[ZB] No bound device after pairing window.");
    return false;
  }

  vTaskDelay(300 / portTICK_PERIOD_MS);
  return true;
}

static void endCoordinatorSession() {
  printBoundDevicesInfo();
  vTaskDelay(3000 / portTICK_PERIOD_MS);
  Zigbee.setRebootOpenNetwork(0);
  vTaskDelay(100 / portTICK_PERIOD_MS);
  Zigbee.closeNetwork();
}

void activateCoordinatorReadAndClose(bool flipSwitch) {
  if (!beginCoordinatorSession(true, 60000)) {
    endCoordinatorSession();
    return;
  }

  if (flipSwitch) {
    Serial.println("[ZB] Toggle switch");
    zbSwitch.lightToggle();
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }

  Serial.println("[ZB] Reading switch state...");
  zbSwitch.getLightState();
  vTaskDelay(300 / portTICK_PERIOD_MS);

  endCoordinatorSession();
}

void closeZigbee() {
  Serial.println("[ZB] Preparing to close network...");
  Serial.println(s_light_state);
  vTaskDelay(3000 / portTICK_PERIOD_MS);
  Serial.println("[ZB] Disable Zigbee network...");
  Zigbee.setRebootOpenNetwork(0);
  Serial.println(F("[ZB] Closing network"));
  delay(100);
  Zigbee.closeNetwork();
}

void toogleZigbee() {
  zbSwitch.lightToggle();
}

void readZigbee() {
  Serial.println("[ZB] Reading switch state...");
  zbSwitch.getLightState();
  delay(200);
  zbSwitch.lightToggle();
  delay(200);
}

bool syncZigbeePower(bool turnOn) {
  if (!beginCoordinatorSession(!s_zigbee_linked, s_zigbee_linked ? 2000 : 60000)) {
    endCoordinatorSession();
    return false;
  }

  Serial.printf("[ZB] Setting switch %s\n", turnOn ? "ON" : "OFF");
  bool ok = turnOn ? switchOn() : switchOff();
  vTaskDelay(500 / portTICK_PERIOD_MS);

  if (ok) {
    s_light_state = turnOn;
    zbSwitch.getLightState();
    vTaskDelay(300 / portTICK_PERIOD_MS);
  }

  endCoordinatorSession();
  return ok;
}

bool ensureZigbeePaired() {
  if (beginCoordinatorSession(true, 60000)) {
    endCoordinatorSession();
    return true;
  }
  endCoordinatorSession();
  return false;
}

bool switchOn() {
  if (!zbSwitch.bound()) return false;
  zbSwitch.lightOn();
  return true;
}

bool switchOff() {
  if (!zbSwitch.bound()) return false;
  zbSwitch.lightOff();
  return true;
}

#endif
