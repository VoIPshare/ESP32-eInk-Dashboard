#include "calendar.h"
#include <time.h>
#include <ArduinoJson.h>

#define COLS 7
#define ROWS 6

const char* daysHeader[7] = {"M","T","W","T","F","S","S"};

const char* months[12] = {
  "January","February","March","April","May","June",
  "July","August","September","October","November","December"
};

int daysInMonth(int year,int month)
{
  if(month==2)
  {
    if((year%4==0 && year%100!=0) || (year%400==0))
      return 29;
    return 28;
  }

  const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
  return days[month-1];
}

int weekdayMonday(struct tm *t)
{
  int w=t->tm_wday;
  if(w==0) return 6;
  return w-1;
}

void drawCalendar(LayoutItem* item)
{
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);

  int year = t->tm_year + 1900;
  int month = t->tm_mon + 1;
  int today = t->tm_mday;

  int headerH = 28;
  int cellW = item->Width / COLS;
  int cellH = (item->Height - headerH) / ROWS;

  // ---- Month Title ----
  char title[24];
  sprintf(title, "%s %d", months[month - 1], year);
  drawSparseStringCentered(&epaperFont, item->PosX + item->Width / 2, item->PosY + textSpace, title, GxEPD_BLACK);

  // ---- Weekday headers ----
  for (int i = 0; i < 7; i++)
  {
    int x = item->PosX + i * cellW + cellW / 2;
    drawSparseStringCentered(&epaperFont, x, item->PosY + 20 + textSpace, daysHeader[i], GxEPD_BLACK);
  }

  // ---- First weekday of month ----
  struct tm firstDay = *t;
  firstDay.tm_mday = 1;
  mktime(&firstDay);
  int start = weekdayMonday(&firstDay);
  int total = daysInMonth(year, month);

  int d = 1;
  for (int r = 0; r < ROWS; r++)
  {
    for (int c = 0; c < COLS; c++)
    {
      int index = r * COLS + c;
      if (index >= start && d <= total)
      {
        int x = item->PosX + c * cellW + cellW / 2;
        int y = item->PosY + headerH + r * cellH + cellH / 2 + 10;

        char buf[4];
        sprintf(buf, "%d", d);

        bool isToday = (d == today);
        bool hasEvent = false;

        // ---- Check if this day has an event ----
        for (int i = 0; i < cal.daysCount; i++)
        {
          if ((int)cal.daysWithEvents[i] == d)
          {
            hasEvent = true;
            break;
          }
        }

        int radius = cellH / 2 -2;

        if (hasEvent)
        {
          // Event day: filled marker.
          int fillRadius = radius;
          if (isToday && radius > 5) {
            fillRadius = radius - 3;
          }

          display.fillCircle(x, y - 5, fillRadius, GxEPD_BLACK);

          drawSparseStringCentered(&epaperFont, x, y, buf, GxEPD_WHITE);

          if (isToday)
          {
            // Today + event: keep the filled event circle, add a white gap,
            // then the double black outline outside.
            display.drawCircle(x, y - 5, radius - 1, GxEPD_WHITE);
            display.drawCircle(x, y - 5, radius, GxEPD_BLACK);
            display.drawCircle(x, y - 5, radius + 1, GxEPD_BLACK);
          }
        }
        else
        {
          drawSparseStringCentered(&epaperFont, x, y, buf, GxEPD_BLACK);

          if (isToday)
          {
            display.drawCircle(x, y - 5, radius, GxEPD_BLACK);
            display.drawCircle(x, y - 5, radius + 1, GxEPD_BLACK);
          }
        }

        d++;
      }
    }
  }
}
