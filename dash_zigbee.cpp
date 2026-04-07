#include "dash_zigbee.h"

bool s_light_state = false;

#if USE_ZIGBEE

// Zigbee switch endpoint used to control the paired Ikea switch.
static ZigbeeSwitch zbSwitch(SWITCH_ENDPOINT_NUMBER);

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
  zbSwitch.getLightState();
}

void activateCoordinatorReadAndClose(bool flipSwitch) {
  s_light_state = false;
  Serial.println("\n[ZB] Activating Zigbee coordinator");

  zbSwitch.onLightStateChange(onLightStateChange);
  zbSwitch.allowMultipleBinding(true);
  zbSwitch.setManufacturerAndModel("DIY", "ESP32-C6-Coordinator");

  Zigbee.addEndpoint(&zbSwitch);
  Zigbee.setRebootOpenNetwork(60);

  if (!Zigbee.begin(ZIGBEE_COORDINATOR)) {
    Serial.println("[ZB] Failed to start Zigbee");
    ESP.restart();
  }

  while (!zbSwitch.bound()) {
    Serial.print(".");
    delay(500);
  }

  vTaskDelay(300 / portTICK_PERIOD_MS);

  if (flipSwitch) {
    Serial.println("[ZB] Toggle switch");
    zbSwitch.lightToggle();
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }

  Serial.println("[ZB] Reading switch state...");
  zbSwitch.getLightState();
  vTaskDelay(300 / portTICK_PERIOD_MS);

  printBoundDevicesInfo();

  vTaskDelay(3000 / portTICK_PERIOD_MS);
  Zigbee.setRebootOpenNetwork(0);
  vTaskDelay(100 / portTICK_PERIOD_MS);
  Zigbee.closeNetwork();
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

bool switchOn() {
  if (!zbSwitch.bound()) return false;
  zbSwitch.lightOn(SWITCH_ENDPOINT_NUMBER);
  return true;
}

bool switchOff() {
  if (!zbSwitch.bound()) return false;
  zbSwitch.lightOff(SWITCH_ENDPOINT_NUMBER);
  return true;
}

#endif
