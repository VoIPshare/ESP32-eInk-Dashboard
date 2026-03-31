#pragma once
#include "app.h"
#include "fetchAllInfo.h"

#define MAX_AMS             4
#define MAX_TRAYS           4
#define NAME_LEN            64
#define ID_LEN              16
#define WEIGHT_LEN          16

struct Tray {
  char id[ID_LEN];
  int remain;
  char weight[WEIGHT_LEN];
  char tray_type[10];
  int state;
};

struct AMS {
  char id[ID_LEN];
  Tray trays[MAX_TRAYS];
  int trayCount;
  int humidity;
  float temp;
};

struct PrinterStatus {

  // Progress
  int layer_num;
  int total_layer_num;
  int percent;
  unsigned int remainTime;
  char gcode_state[20];

  // Temps
  float bed_temp;
  float nozzle_temp;

  // Fans
  int fan_speed;
  int cooling_fan_speed;
  int big_fan1_speed;
  int big_fan2_speed;
  // Job
  char job_name[NAME_LEN];
  char stage_state[16];
  int amsCount;
  uint8_t error;
  // AMS
  AMS ams[MAX_AMS];
  
};


extern RTC_DATA_ATTR bool isPrinting;
extern RTC_DATA_ATTR bool previousIsPrinting;

void fetchBambu(  LayoutItem*  );
void bambuWidget(LayoutItem* );
