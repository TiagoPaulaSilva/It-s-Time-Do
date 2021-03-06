#ifndef OLED_H
#define OLED_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

extern Adafruit_SSD1306 display;

// PROTOTYPES
void OLED_Init();
void Display_Check_OTA_Firmware_Update();
void Display_WiFi_Connecting();
void Display_Firebase_Connecting();
void OLED_OTA_Progress(int status);
void OLED_Clear();
void OLED_Print_Calendar(String calendar);
void OLED_Print_Clock(String clock);
void OLED_Print_Schedule(String from_cloud);
void OLED_Build_Home_Screen(String _Schedule_Time, String Firmware_Version);
void OLED_Print_Loading_Screen();
void Clear_Active_Tasks();
void OLED_Print();
void OLED_Build_Working_Screen(String current_duration, String Firmware_Version);
void OLED_Print_Current_Task_Duration(String value);

#endif // OLED_H
