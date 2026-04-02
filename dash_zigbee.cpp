#include "configure.h"
#include "dash_zigbee.h"


bool s_light_state=false;

// ── Zigbee object ─────────────────────────────────────────────
ZigbeeSwitch zbSwitch = ZigbeeSwitch(SWITCH_ENDPOINT_NUMBER);



// ── Callback: light state changed ────────────────────────────
static void onLightStateChange(bool state) {
  s_light_state = state;
  Serial.printf("[ZB] Light state → %s\n", state ? "ON 💡" : "OFF 🌑");
}


void printBoundDevicesInfo() {
  if (!zbSwitch.bound()) {
    Serial.println("[ZB] ⚠️ No bound devices found.");
    return;
  }

  Serial.println("\n[ZB] Bound devices info:");

  // std::list<zb_device_params_t *> devices = zbSwitch.getBoundDevices();
  // for (const auto &dev : devices) {
  //     Serial.printf("  Endpoint  : %d\n",      dev->endpoint);
  //     Serial.printf("  Short addr: 0x%04X\n",  dev->short_addr);
  //     Serial.printf("  IEEE addr : %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
  //         dev->ieee_addr[7], dev->ieee_addr[6],
  //         dev->ieee_addr[5], dev->ieee_addr[4],
  //         dev->ieee_addr[3], dev->ieee_addr[2],
  //         dev->ieee_addr[1], dev->ieee_addr[0]);

  //     // Read manufacturer + model name from the bound device
  //     char *mfr   = zbSwitch.readManufacturer(dev->endpoint, dev->short_addr, dev->ieee_addr);
  //     char *model = zbSwitch.readModel(dev->endpoint, dev->short_addr, dev->ieee_addr);
  //     if (mfr)   Serial.printf("  Manufacturer: %s\n", mfr);
  //     if (model) Serial.printf("  Model       : %s\n", model);
  // }

  // Poll the light state once for all bound devices
  zbSwitch.getLightState();  // triggers onLightStateChange callback
}

void activateCoordinatorReadAndClose(bool flipSwitch) {
  s_light_state = false;
  Serial.println("\n[ZB] === Activating Zigbee Coordinator ===");

  // Register callback
  zbSwitch.onLightStateChange(onLightStateChange);

  // Allow multiple devices if needed
  zbSwitch.allowMultipleBinding(true);

  // Device identity (shown in Zigbee2MQTT / HA)
  zbSwitch.setManufacturerAndModel("DIY", "ESP32-C6-Coordinator");

  // Add endpoint
  Zigbee.addEndpoint(&zbSwitch);

  // Open network for 60 seconds
  Zigbee.setRebootOpenNetwork(60);

  // Start Zigbee
  if (!Zigbee.begin(ZIGBEE_COORDINATOR)) {
    Serial.println("[ZB] ❌ Failed to start Zigbee!");
    ESP.restart();
  }

  // Serial.println("[ZB] ✅ Coordinator started");
  // Serial.println("[ZB] Waiting for device to bind...");

  // Wait for at least one bound device
  while (!zbSwitch.bound()) {
    Serial.print(".");
    delay(500);
  }
  // Serial.println("\n[ZB] ✅ Device bound!");

  // Serial.println("[ZB] Waiting for Zigbee stack to stabilize...");
  // delay(2500);  // IMPORTANT: let stack stabilize
   vTaskDelay(300 / portTICK_PERIOD_MS);




  Serial.println("[ZB] Toogle switch");
  // Optionally toggle the light once to check
  zbSwitch.lightToggle();
  // delay(200);  // give time for response
  vTaskDelay(500 / portTICK_PERIOD_MS);



  // Read switch/light state once
  Serial.println("[ZB] Reading switch state...");
  zbSwitch.getLightState();  // triggers onLightStateChange callback
  // delay(200);
   vTaskDelay(300 / portTICK_PERIOD_MS);

  // Print bound device info
  printBoundDevicesInfo();

 


  // Serial.println("[ZB] Preparing to close network...");
  // Serial.println(s_light_state);
  // wait so Zigbee stack finishes sending packets
  vTaskDelay(3000 / portTICK_PERIOD_MS);

  // Close network (no more pairing)
  // Serial.println("[ZB] Disable Zigbee network...");
  Zigbee.setRebootOpenNetwork(0);  // disables join after reboot
  // Serial.println(F("[ZB] Closing Network"));
  vTaskDelay(100 / portTICK_PERIOD_MS);
  Zigbee.closeNetwork();           // closes join immediately

  // Serial.println("[ZB] === Done ===");
}

void closeZigbee()
{
  Serial.println("[ZB] Preparing to close network...");
  Serial.println(s_light_state);
  // wait so Zigbee stack finishes sending packets
  vTaskDelay(3000 / portTICK_PERIOD_MS);

  // Close network (no more pairing)
  Serial.println("[ZB] Disable Zigbee network...");
  Zigbee.setRebootOpenNetwork(0);  // disables join after reboot
  Serial.println(F("[ZB] Closing Network"));
  delay(100);
  Zigbee.closeNetwork();           // closes join immediately
}

void toogleZigbee() {
  zbSwitch.lightToggle();
}

void readZigbee() {

  // Read switch/light state once
  Serial.println("[ZB] Reading switch state...");
  zbSwitch.getLightState();  // This triggers onLightStateChange()
  delay(200);
  zbSwitch.lightToggle();  //lightOn();
  delay(200);              // give time for response
}


bool switchOn() {
  if (!zbSwitch.bound()) return false;
  zbSwitch.lightOn(SWITCH_ENDPOINT_NUMBER);  // send ON command to bound device
  return true;
}

bool switchOff() {
  if (!zbSwitch.bound()) return false;
  zbSwitch.lightOff(SWITCH_ENDPOINT_NUMBER);  // send OFF command
  return true;
}

