#define FIRMWARE_VERSION "1.3"
/*
v1.0 - Initial release.
v1.1 - Bug fix in task duration calcs.
v1.2 - Update library 'Firebase-ESP32' from  v3.1.5 to v3.2.0.
v1.3 - Reading and writing data on the server changed to a single JSON packet.
*/

/* Native libraries */
#include <Arduino.h>

/* External libraries */
#include <Ticker.h>

/* Own libraries */
#include "Board_Pins.h"
#include "Cloud.h"
#include "DS3231.h"
#include "Firebase_Secrets.h"
#include "My_Persistent_Data.h"
#include "Network.h"
#include "OLED.h"
#include "WiFi_Secrets.h"

struct Washing_Machine_Parameters {
    const String WORKING = "WORKING...";
    const String FREE = "FREE";

    bool starting = false;
    bool current_power_state = false;
    bool last_power_state = false;

    String washing_mode = "?";

    bool task_finished = false;

    String task_initial_time;
    String task_initial_date;

    String task_finished_time;
    String task_finished_date;

    String task_duration;

} Washing_Machine;

uint32_t Task_Initial_Timestamp = 0;
String Next_Task = "FREE";

FirebaseJson JSON;                 // or constructor with contents e.g. FirebaseJson JSON("{\"a\":true}");
FirebaseJsonArray arr;             // or constructor with contents e.g. FirebaseJsonArray arr("[1,2,true,\"test\"]");
FirebaseJsonData JSON_Field_Value; // object that keeps the deserializing JSON_Field_Value

void Its_Time_Do();
void Wait_Task_Finish_and_Calc_Duration();

void Check_and_Fix_Fields_in_RTDB() {

    Firebase.RTDB.getJSON(&fbdo, "/");
    fbdo.to<FirebaseJson>().get(JSON_Field_Value, "/START");

    if (!JSON_Field_Value.success) {
        JSON.add("START", Washing_Machine.FREE);
        Set_Firebase_JSON_at("/", JSON);
        JSON.clear();
    }
}

bool Get_Washing_Machine_Power_State(int pin) {

    const uint16_t SAMPLES = 255;
    uint32_t mean = 0;

    for (uint16_t i = 0; i < SAMPLES; i++)
        mean += analogRead(pin);

    mean /= SAMPLES;

    return (mean >= 220) ? true : false; // 220 in AD value  is ~0.7V (minimum drop voltage in a diode/LED)
}

void ISR_Display_Update() {
    OLED_Clear();
    OLED_Build_Home_Screen(Next_Task != "" ? Next_Task : "FREE", FIRMWARE_VERSION);
    OLED_Print();
}

void ISR_Server_Update() {

    JSON.clear();

    Firebase.RTDB.getJSON(&fbdo, "/", &JSON);

    fbdo.to<FirebaseJson>().get(JSON_Field_Value, "/START");
    Next_Task = isValid_Time(JSON_Field_Value.to<String>()) != "-1" ? JSON_Field_Value.to<String>() : "FREE";

    JSON.set("/IoT_Device/Calendar", Current_Date(FULL));
    JSON.set("/IoT_Device/Clock", Current_Clock(WITHOUT_SECONDS));
    JSON.set("/IoT_Device/Schedule", (Washing_Machine.starting || Washing_Machine.current_power_state) ? Washing_Machine.WORKING : Next_Task);

    JSON.set("/Washing_Machine/State", Washing_Machine.current_power_state ? "ON" : "OFF");

    JSON.set("/START", (Washing_Machine.starting || Washing_Machine.current_power_state) ? Washing_Machine.WORKING : Washing_Machine.FREE);

    if (Washing_Machine.task_finished) {
        static char path[100] = {0};
        Washing_Machine.task_finished = false;

        siprintf(path, "/Washing_Machine/Last_Task/%s/%s/Finish", Washing_Machine.task_initial_date, Washing_Machine.task_initial_time.c_str());
        JSON.set(path, Washing_Machine.task_finished_time);

        siprintf(path, "/Washing_Machine/Last_Task/%s/%s/Mode", Washing_Machine.task_initial_date, Washing_Machine.task_initial_time.c_str());
        JSON.set(path, Washing_Machine.washing_mode);

        siprintf(path, "/Washing_Machine/Last_Task/%s/%s/Duration", Washing_Machine.task_initial_date, Washing_Machine.task_initial_time.c_str());
        JSON.set(path, Washing_Machine.task_duration);
    }

    Set_Firebase_JSON_at("/", JSON);
}

void ISR_Hardware_Inputs_Monitor() {
    Washing_Machine.current_power_state = Get_Washing_Machine_Power_State(WASHING_MACHINE_POWER_LED);
}

Ticker ISR_Display_Update_Controller(ISR_Display_Update, 100, 0, MILLIS);
Ticker ISR_Cloud_Communication(ISR_Server_Update, 5000, 0, MILLIS);
Ticker ISR_Hardware_Inputs_Monitor_Controller(ISR_Hardware_Inputs_Monitor, 1000, 0, MILLIS);

void setup() {

    Board_Pins_Init();

    Flash_Memory_Init();

    Flash_Memory_Read_Variables();

    RTC_Init();

    OLED_Init();

    WiFi_Init();

    Firebase_Init();
    Check_and_Fix_Fields_in_RTDB();

    //  Checks_OTA_Firmware_Update();

    OLED_Clear();

    OLED_Print_Loading_Screen();

    ISR_Display_Update_Controller.start();
    ISR_Cloud_Communication.start();
    ISR_Hardware_Inputs_Monitor_Controller.start();
}

void loop() {

    ISR_Display_Update_Controller.update();
    ISR_Cloud_Communication.update();
    ISR_Hardware_Inputs_Monitor_Controller.update();

    Its_Time_Do();

    Wait_Task_Finish_and_Calc_Duration();
}

void Its_Time_Do() {

    if (isValid_Time(Next_Task)) {

        int hour = Next_Task.substring(0, Next_Task.indexOf(":")).toInt();
        int min = Next_Task.substring(Next_Task.indexOf(":") + 1).toInt();
        int sec = 0;

        int day = Current_Date(JUST_DAY).toInt();
        int month = Current_Date(JUST_MONTH).toInt();
        int year = Current_Date(JUST_YEAR).toInt();

        uint32_t schedule_timestamp = 0, current_timestamp = 0;

        schedule_timestamp = unix_time_in_seconds(hour, min, sec, day, month, year);
        current_timestamp = unix_time_in_seconds(Current_Clock(JUST_HOUR).toInt(), Current_Clock(JUST_MIN).toInt(), sec, day, month, year);

        if (((current_timestamp >= schedule_timestamp) && (current_timestamp <= (schedule_timestamp + 120))) && !Washing_Machine.last_power_state) {

            Washing_Machine.task_initial_time = Current_Clock(WITHOUT_SECONDS);
            Washing_Machine.task_initial_date = Current_Date(FULL);

            digitalWrite(RELAY, HIGH);
            delay(300);
            digitalWrite(RELAY, LOW);

            while (!Get_Washing_Machine_Power_State(WASHING_MACHINE_POWER_LED)) {
                Washing_Machine.starting = true;
                ISR_Display_Update_Controller.update();
                ISR_Cloud_Communication.update();
                ISR_Hardware_Inputs_Monitor_Controller.update();
                Serial.println("Waiting LED power on...");
                delay(500);
            }
            Washing_Machine.starting = false;
            Serial.print("task_initial_time: ");
            Serial.println(Washing_Machine.task_initial_time);

            Washing_Machine.last_power_state = true;
            Task_Initial_Timestamp = current_timestamp;
        }
    }
}

void Wait_Task_Finish_and_Calc_Duration() {

    if (Washing_Machine.last_power_state && !Get_Washing_Machine_Power_State(WASHING_MACHINE_POWER_LED)) {

        Serial.println("F!");
        Washing_Machine.task_finished_time = Current_Clock(WITHOUT_SECONDS);
        Washing_Machine.task_finished_date = Current_Date(FULL);

        Serial.print("task_finished_time: ");
        Serial.println(Washing_Machine.task_finished_time);

        uint32_t Task_Delta_Timestamp = 0, Task_Finished_Timestamp = 0;

        Task_Finished_Timestamp = unix_time_in_seconds(Current_Clock(JUST_HOUR).toInt(), Current_Clock(JUST_MIN).toInt(), 0, Current_Date(JUST_DAY).toInt(), Current_Date(JUST_MONTH).toInt(), Current_Date(JUST_YEAR).toInt());

        Task_Delta_Timestamp = Task_Finished_Timestamp - Task_Initial_Timestamp; // get delta in secs.
        Task_Delta_Timestamp /= 60;                                              // converts delta from secs to mins.
        int h = Task_Delta_Timestamp / 60;                                       // converts delta from mins to hours and store only hours number and discarts (truncates) minutes.
        int m = ((Task_Delta_Timestamp / 60.0) - h) * 60;                        // get only minutes from delta (in hours format) and convert this to minutes format.

        char Task_Duration[10];

        sprintf(Task_Duration, "%02dh%02dmin", h, m);

        Washing_Machine.task_duration = Task_Duration;

        Washing_Machine.last_power_state = false;

        Washing_Machine.task_finished = true;
    }
}