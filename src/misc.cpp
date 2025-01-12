#include <Arduino.h>


void get_reset_reason(int reason, char *strReason) {
  switch (reason) {
    case 1: strcpy(strReason, "POWERON_RESET"); break;           /**<1,  Vbat power on reset*/
    case 3: strcpy(strReason, "SW_RESET"); break;                /**<3,  Software reset digital core*/
    case 4: strcpy(strReason, "OWDT_RESET"); break;              /**<4,  Legacy watch dog reset digital core*/
    case 5: strcpy(strReason, "DEEPSLEEP_RESET"); break;         /**<5,  Deep Sleep reset digital core*/
    case 6: strcpy(strReason, "SDIO_RESET"); break;              /**<6,  Reset by SLC module, reset digital core*/
    case 7: strcpy(strReason, "TG0WDT_SYS_RESET"); break;        /**<7,  Timer Group0 Watch dog reset digital core*/
    case 8: strcpy(strReason, "TG1WDT_SYS_RESET"); break;        /**<8,  Timer Group1 Watch dog reset digital core*/
    case 9: strcpy(strReason, "RTCWDT_SYS_RESET"); break;        /**<9,  RTC Watch dog Reset digital core*/
    case 10: strcpy(strReason, "INTRUSION_RESET"); break;        /**<10, Instrusion tested to reset CPU*/
    case 11: strcpy(strReason, "TGWDT_CPU_RESET"); break;        /**<11, Time Group reset CPU*/
    case 12: strcpy(strReason, "SW_CPU_RESET"); break;           /**<12, Software reset CPU*/
    case 13: strcpy(strReason, "RTCWDT_CPU_RESET"); break;       /**<13, RTC Watch dog Reset CPU*/
    case 14: strcpy(strReason, "EXT_CPU_RESET"); break;          /**<14, for APP CPU, reseted by PRO CPU*/
    case 15: strcpy(strReason, "RTCWDT_BROWN_OUT_RESET"); break; /**<15, Reset when the vdd voltage is not stable*/
    case 16: strcpy(strReason, "RTCWDT_RTC_RESET"); break;       /**<16, RTC Watch dog reset digital core and rtc module*/
    default: strcpy(strReason, "NO_MEAN");
  }
  
}
void getBootTime() {
  const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -18000;;
const int   daylightOffset_sec = 3600;
  char strReason[80], lastBoot[80];
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;

  int reset_reason = esp_reset_reason();
  get_reset_reason(reset_reason, strReason);
  //Blynk.virtualWrite(V26, strReason);

  if (!getLocalTime(&timeinfo)) {
    strcpy(lastBoot, "Failed to obtain time");
    Serial.println("Failed to obtain time");
  } else {
    int hr = timeinfo.tm_hour;
    sprintf(lastBoot, "%d/%d/%d %d:%02d 0x%02x",
            timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900, hr, timeinfo.tm_min, reset_reason);
  }
  //Blynk.virtualWrite(V25, lastBoot);


  delay(500);
}