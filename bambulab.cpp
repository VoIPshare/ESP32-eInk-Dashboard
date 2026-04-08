#include "bambulab.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <math.h>

PrinterStatus printer;

// int received=0;
RTC_DATA_ATTR bool isPrinting=false;
RTC_DATA_ATTR bool previousIsPrinting=false;
// --- MQTT ---
// This module keeps only a short-lived MQTT connection per wake cycle:
// connect → subscribe → process a few seconds of messages → disconnect.
WiFiClientSecure wifiClient;
PubSubClient client(wifiClient);


void callback(char* topic, byte* payload, unsigned int length) {

  StaticJsonDocument<4096> doc;
  // doc clear();

  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    DBG( F("[Bambu] JSON parse failed") );
    return;
  }

  JsonObject print = doc["print"];
  if (!print) return;

  // -------- Progress --------
  printer.layer_num = print["3D"]["layer_num"] | 0;
  printer.total_layer_num = print["3D"]["total_layer_num"] | 0;
  printer.percent = print["percent"].as<int>() | 0;
  printer.remainTime = print["mc_remaining_time"].as<int>() | 0;

  printer.error = print["err"].as<int>() | 0;
  
  copyJsonString(printer.gcode_state, sizeof(printer.gcode_state), print["gcode_state"]);

  // Serial.printf("Perc %d isPrinting %d Prev %d  Status %s\n", printer.percent, isPrinting, previousIsPrinting, printer.gcode_state);
  // Persist printing state across deep sleep so we can refresh more frequently mid-print.
  Serial.println( printer.gcode_state );
  if( ( printer.gcode_state && (strcmp(printer.gcode_state,"RUNNING")==0||  strcmp(printer.gcode_state,"PREPARE")==0 ) || strcmp(printer.gcode_state,"PAUSE")   == 0 ) || ( printer.percent <100 && printer.percent >0 ) )
    isPrinting=true;
  else
    isPrinting=false;

  // -------- Temperatures --------
  printer.bed_temp = print["bed_temper"] | 0.0;
  printer.nozzle_temp = print["nozzle_temper"] | 0.0;

  // -------- Fans --------
  printer.fan_speed = print["fan"].as<int>();
  printer.cooling_fan_speed = print["cooling_fan_speed"].as<int>();

  printer.big_fan1_speed = print["big_fan1_speed"].as<int>();
  printer.big_fan2_speed = print["big_fan2_speed"].as<int>();
  
  // -------- Job --------
  const char* name = print["subtask_name"] | "";
  strncpy(printer.job_name, name, NAME_LEN);
  printer.job_name[NAME_LEN - 1] = '\0';

  const char* state = print["job"]["cur_stage"]["state"] | "";
  strncpy(printer.stage_state, state, sizeof(printer.stage_state));
  printer.stage_state[sizeof(printer.stage_state) - 1] = '\0';

  // -------- AMS --------
  printer.amsCount = 0;

  JsonArray amsArray = print["ams"]["ams"];

  if (!amsArray.isNull()) {

    for (JsonObject amsObj : amsArray) {
      
      if (printer.amsCount >= MAX_AMS) break;

      AMS &a = printer.ams[printer.amsCount++];
      a.trayCount = 0;

      const char* amsId = amsObj["id"] | "";
      strncpy(a.id, amsId, ID_LEN);
      a.id[ID_LEN - 1] = '\0';


      a.temp=amsObj["temp"].as<float>();
      a.humidity=amsObj["humidity_raw"].as<int>();


      JsonArray trayArray = amsObj["tray"];

      if (!trayArray.isNull()) {

        for (JsonObject trayObj : trayArray) {

          if (a.trayCount >= MAX_TRAYS) break;

          Tray &t = a.trays[a.trayCount++];

          const char* trayId = trayObj["id"] | "";
          strncpy(t.id, trayId, ID_LEN);
          t.id[ID_LEN - 1] = '\0';

          t.remain = trayObj["remain"] | 0;

          const char* weight = trayObj["weight"] | "";
          strncpy(t.weight, weight, WEIGHT_LEN);
          t.weight[WEIGHT_LEN - 1] = '\0';

          copyJsonString(t.tray_type, sizeof(t.tray_type), trayObj["tray_type"]);

          t.state=trayObj["state"].as<int>();

        }
      }
    }
  }
  DBG(F("[Bambu] Retrived the informations"));
}

static inline int fanPercent(int raw) {
  return (int)(ceil(raw * 10.0f / 15.0f) * 10);
}

void drawFanStat(uint32_t icon, int x, int y, int rawSpeed) {
  char buf[8];
  sprintf(buf, "%d%%", fanPercent(rawSpeed));
  drawSparseChar(&MDI_22_Sparse, x, y+2, icon, GxEPD_BLACK);
  drawSparseString(&epaperFont, x+25, y, buf, GxEPD_BLACK);
}

void bambuWidget(LayoutItem* item)
{
  DBG( F("[Bambu] UPDATING......") );
  char buf[40];
  int X_POS;
  int margin_x=5;
  int baseX = item->PosX + margin_x;
  int baseY = item->PosY;
  int y = baseY + item->RowHeight/2 + 5;

  drawSparseString(&epaperFont, baseX, y, item->Extra1, GxEPD_BLACK );

  // Layers
  X_POS += 65;
  drawSparseChar(&MDI_22_Sparse, X_POS, y+2, MDI_LAYERS, GxEPD_BLACK);
  sprintf(buf, "%d/%d", printer.layer_num , printer.total_layer_num );  

  X_POS+=25;
  drawSparseString(&epaperFont, X_POS, y, buf,GxEPD_BLACK );
  
  // Completed
  X_POS+=90;
  sprintf(buf, "C: %d%%", printer.percent );  
  drawSparseString(&epaperFont, X_POS, y, buf,GxEPD_BLACK );

  // Bed Temperature
  X_POS+=75;

  sprintf(buf, "%.0f°", printer.bed_temp );  
  drawSparseString(&epaperFont, X_POS+25, y, buf,GxEPD_BLACK );
  drawSparseChar(&MDI_22_Sparse, X_POS, y+2, MDI_BED, GxEPD_BLACK);

  // Noozle
   X_POS+=75;
  sprintf(buf, "%.0f°", printer.nozzle_temp );  
  drawSparseString(&epaperFont, X_POS+25, y, buf,GxEPD_BLACK );
  drawSparseChar(&MDI_22_Sparse, X_POS, y+2, MDI_NOZZLE, GxEPD_BLACK);

  y+=item->RowHeight;
  X_POS=baseX;
  drawFanStat(MDI_FAN_COOLING, X_POS,      y, printer.cooling_fan_speed);

  //Exhaust Fan
  X_POS+=70;
  drawFanStat(MDI_FAN_EXHAUST, X_POS,   y, printer.big_fan2_speed);

  //Right Aux
  X_POS+=70;
  drawFanStat(MDI_FAN_AUX, X_POS,  y, printer.big_fan1_speed);

  // Print info 1st AMS if present
  if( printer.ams[0].trayCount>0 )
  {
    DBG( F("Tray") );
    X_POS+=70;
    // AMS Humidity
    sprintf(buf, "%d%%", printer.ams[0].humidity ); 
    drawSparseString(&epaperFont, X_POS+25, y, buf, GxEPD_BLACK);
    drawSparseChar(&MDI_22_Sparse, X_POS, y+2, MDI_HUMIDITY, GxEPD_BLACK);

    // AMS Temperature
    X_POS+=70;
    sprintf(buf, "%.1f°", printer.ams[0].temp ); 
    drawSparseString(&epaperFont, X_POS+25, y, buf, GxEPD_BLACK );
    drawSparseChar(&MDI_22_Sparse, X_POS, y+2, MDI_THERMOMETER, GxEPD_BLACK);

  }

  y+=item->RowHeight;
  //Clock
  drawSparseChar(&MDI_22_Sparse,item->PosX, y+2, MDI_CLOCK, GxEPD_BLACK);
  sprintf(buf, "%02d:%02d", printer.remainTime/60, printer.remainTime%60 ); 
  drawSparseString(&epaperFont, item->PosX+30, y, buf,GxEPD_BLACK );

  drawSparseString(&epaperFont, item->PosX, y+20, printer.gcode_state ,GxEPD_BLACK );

  for (int i = 0; i < printer.amsCount; i++) {

    if( printer.ams[i].trayCount>0 )
    {
      drawSparseString(&epaperFont, item->PosX+90, item->PosY+59, "AMS",GxEPD_BLACK );

      int j = 0;
      for (; j < printer.ams[i].trayCount; j++) {

        drawSparseChar(&MDI_22_Sparse, item->PosX+item->ColWidth*j+150, y+4, MDI_SPOOL, GxEPD_BLACK);
        sprintf(buf, "%d", j+1);  
        drawSparseStringCentered(&epaperFont, item->PosX+item->ColWidth*j+160, y-1, buf,GxEPD_BLACK );
     
        if( printer.ams[i].trays[j].state != 11)
            drawSparseStringCentered(&epaperFont, item->PosX+item->ColWidth*j+160, y+item->RowHeight, "EMPTY",GxEPD_BLACK );
        else
          if( printer.ams[i].trays[j].remain!=-1 )
          {
            sprintf(buf, "%d%%", printer.ams[i].trays[j].remain);  
            drawSparseStringCentered(&epaperFont, item->PosX+item->ColWidth*j+160, y+item->RowHeight, buf,GxEPD_BLACK );
          }
          else {
            drawSparseStringCentered(&epaperFont, item->PosX+item->ColWidth*j+160, y+item->RowHeight, printer.ams[i].trays[j].tray_type,GxEPD_BLACK );
          }
      }
    }
  }

}
// File-local helper: establish MQTT connection + subscribe to the report topic.
static void connectMqtt( LayoutItem * infoBambu) {
  uint8_t retries = 0;
  while (!client.connected() && retries < 5 ) {
      DBG( F("[Bambu]: Connecting to MQTT...") );
      if (client.connect("ESP32_Bambu", "bblp", mqtt_pass)) {
          DBG( F("[Bambu]: connected") );
          char reportTopic[96];
          snprintf(reportTopic, sizeof(reportTopic), "device/%s/report", mqtt_sn);
          client.subscribe(reportTopic);
      } else {
        DBGF("[Bambu]: MQTT fail rc=%d retry %d/5\n", client.state(), retries+1);
        retries++;
        delay(2000);
      }
  }
  if (!client.connected())
    DBG(F("[Bambu] MQTT gave up"));

}

void fetchBambu(  LayoutItem* infoBambu  )
{
  DBG( F("[Bambu] Fetching"));
    DBGF("[Bambu] MQTT Pass %s MQTT_SN %s MQTT_IP %s MQTT_PORT %d", mqtt_pass, mqtt_sn, mqtt_ip, mqtt_port );

  // This will create a widget strange, I need to return something
  if (mqtt_pass[0] == '\0' || mqtt_sn[0] == '\0' || mqtt_ip[0] == '\0' || mqtt_port == 0)
  {
    DBG("[Bambu]: Missing some parametes such pass, sn, ip or port");
    return;
  }

  previousIsPrinting=isPrinting;
  // NOTE: setInsecure() disables certificate validation (MITM risk).
  wifiClient.setInsecure();
  client.setBufferSize(20000);
  DBGF("[Bambu]: IP %s Port %d\n", mqtt_ip, mqtt_port );
  client.setServer(mqtt_ip, mqtt_port);
  client.setCallback(callback);
  connectMqtt( infoBambu );
  unsigned long start = millis();
  while ( millis() - start < 3000) {  // wait up to 4 seconds
    client.loop();
    delay(10);
  }

  client.disconnect();   // <-- CLOSE MQTT connection
  wifiClient.stop();
}

bool isAuxFanOn() {
  DBG("Test fan");
  return printer.big_fan1_speed > 0;
}
