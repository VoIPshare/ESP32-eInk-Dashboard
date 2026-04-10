#pragma once
#include "GxEPD2.h"
#include "configure.h"
#include "fonts/SparseFontBase.h"
#include "fonts/MDI_80_Sparse.h"
#include "fonts/MDI_22_Sparse.h"
#include "fonts/epaperFont.h"
#include "fonts/HighSpeed_130.h"

void drawSparseCharCenter(const SparseGFXfont* , int16_t , int16_t , uint32_t , uint16_t );
void drawSparseChar(const SparseGFXfont* , int16_t , int16_t , uint32_t , uint16_t );
int16_t getSparseStringWidth(const SparseGFXfont* , const char* );
void drawSparseString(const SparseGFXfont* , int16_t , int16_t , const char* , uint16_t );
void drawSparseStringCentered(const SparseGFXfont* , int16_t , int16_t ,  const char* , uint16_t );

#if USE_COLORDISPLAY
  #include <GxEPD2_4C.h>
  #include <epd4c/GxEPD2_750c_GDEM075F52.h>
  using DashboardPanel = GxEPD2_750c_GDEM075F52;
  using DashboardDisplay = GxEPD2_4C<DashboardPanel, DashboardPanel::HEIGHT / 2>;
#else
  #include <GxEPD2_BW.h>
  #include <epd/GxEPD2_750_T7.h>
  using DashboardPanel = GxEPD2_750_T7;
  using DashboardDisplay = GxEPD2_BW<DashboardPanel, DashboardPanel::HEIGHT>;
#endif
#include <Arduino.h>
#include <Adafruit_GFX.h>

DashboardDisplay& getDisplay();

#define display getDisplay()

#if DISPLAY_HAS_COLOR
  #define ALERT_COLOR GxEPD_RED
#else
  #define ALERT_COLOR GxEPD_BLACK
#endif
