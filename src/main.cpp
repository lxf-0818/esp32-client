/**
 * @file main.cpp
 * @brief ESP32 client application using Blynk and OLED display for IoT monitoring and control.
 *
 * This program connects an ESP32 to a Wi-Fi network, integrates with the Blynk IoT platform,
 * and communicates with a server to fetch and display device data. It also uses an OLED display
 * to show basic information and implements a watchdog timer for system reliability.
 *
 * @details
 * - The program uses Blynk for IoT communication and virtual pin updates.
 * - It fetches device information from a server and processes it for display and control.
 * - A loop watchdog timer (LWD) is implemented to reboot the system in case of a hang.
 * - The program supports updating widgets with sensor data and managing device connections.
 *
 * @dependencies
 * - Arduino core for ESP32
 * - Blynk library
 * - Adafruit SSD1306 library for OLED display
 * - HTTPClient for server communication
 * - Ticker for watchdog timer
 *
 * @author Leon Freimour
 * @date YYYY-MM-DD
 *
 * @note Replace sensitive information such as Blynk authentication tokens before deployment.
 *
 * @section Functions
 * - setup(): Initializes the system, connects to Wi-Fi, and sets up Blynk and the OLED display.
 * - loop(): Runs the Blynk and timer tasks.
 * - flashSSD(): Displays basic information on the OLED screen.
 * - refreshWidgets(): Periodically fetches device data from the server(s) and updates Blynk widgets.
 * - lwdtcb(): Watchdog timer callback to restart the system if the loop hangs.
 * - lwdtFeed(): Feeds the watchdog timer to prevent unnecessary restarts.
 * - upDataWidget(): Updates Blynk widgets with sensor data based on the sensor type.
 * - decryptWifiCredentials(): Decrypts Wi-Fi credentials for secure connection.
 * - socketClient(): Handles socket communication with devices.
 * - queStat(): Checks the status of the error queues.
 * - BLYNK_CONNECTED(): Callback for Blynk connection events.
 * - BLYNK_WRITE(): Handles virtual pin writes from the Blynk app.
 *
 * @section Constants
 * - BLYNK_TEMPLATE_ID, BLYNK_TEMPLATE_NAME, BLYNK_AUTH_TOKEN: Blynk configuration constants.
 * - SCREEN_WIDTH, SCREEN_HEIGHT: OLED display dimensions.
 * - LWD_TIMEOUT: Timeout value for the loop watchdog timer.
 * - Various server URLs for fetching and managing device data.
 *
 * @section Notes
 * - Debugging can be enabled by defining the DEBUG macro.
 * - Ensure the OLED display is properly connected to the ESP32.
 * - The program assumes a specific server API for fetching device data.
 */
#define BLYNK_TEMPLATE_ID "TMPL21W-vgTej"
#define BLYNK_TEMPLATE_NAME "autoStart"
#define BLYNK_AUTH_TOKEN "Z1kJtYwbYfKjPOEsLoXMeeTo8DZiq85H"

// #define BLYNK_TEMPLATE_ID "TMPL2sDJhOygV"
// #define BLYNK_TEMPLATE_NAME "House"
// #define BLYNK_AUTH_TOKEN "3plcY4yZM3HpnupyR5nmnDlUcXADV9sU"
#include <Arduino.h>
#include <map>
#include <FS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <BlynkSimpleEsp32.h>
#include <time.h>
#include <CRC32.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "blynk_widget.h"
#include <Ticker.h>
#define INPUT_BUFFER_LIMIT 2048
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define BLYNK_PRINT Serial
#define DEBUG_LIST
// #define TEMPV6 V6 // Define TEMPV6 as virtual pin V6
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#define SSD_ADDR 0x3c
void initRTOS();
void flashSSD();
void refreshWidgets();
void getBootTime(char *lastBook, char *strReason);
void parseSensorsConnected(const String &sensorsConnected);
String performHttpGet(const char *url);
int decryptWifiCredentials(char *ssid, char *psw);
int socketClient(char *espServer, char *command, bool updateErorrQue);
char *socketClient(char *espServer, char *command);

// char *socketClient(char *espServer, char *command)
// {
//   // Placeholder implementation for socketClient
//   char *response = (char *)malloc(100);
//   if (response == nullptr)
//   {
//     Serial.println("Memory allocation failed");
//     return nullptr;
//   }
//   snprintf(response, 100, "Response from %s with command %s", espServer, command);
//   return response;
// }
void upDataWidget(char *sensorName, float tokens[]);
void lwdtFeed(void);
void ICACHE_RAM_ATTR lwdtcb(void);
bool queStat();
#ifdef TEST
void blynkTimeOn();
void blynkTimeOff();
#endif
std::map<std::string, std::string> ipMap;
const uint16_t port = 8888;
String sensorName = "NO DEVICE";
int failSocket, passSocket, recoveredSocket, retry, timerID1, passPost;
String sensorsConnected;
HTTPClient http;
String lastMsg;
char lastBoot[20], strReason[60];
BlynkTimer timer;
float tokens[5];
bool setAlarm = false;
Ticker lwdTicker;
#define LWD_TIMEOUT 15 * 1000 // Reboot if loop watchdog timer reaches this time out value
unsigned long lwdTime = 0;
unsigned long lwdTimeout = LWD_TIMEOUT;
const char *getRowCnt = "http://192.168.1.252/rows.php";
const char *deleteAll = "http://192.168.1.252/deleteALL.php";
const char *ipList = "http://192.168.1.252/ip.php";
const char *ipDelete = "http://192.168.1.252/deleteIP.php";
const char *esp_data = "http://192.168.1.252/esp-data.php";

void setup()
{
  Serial.begin(115200);
  char auth[] = BLYNK_AUTH_TOKEN;
  char ssid[40], pass[40];
  lastMsg = "no warnings";

  decryptWifiCredentials(ssid, pass);
  Blynk.begin(auth, ssid, pass);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SSD_ADDR))
    Serial.println(F("SSD1306 allocation failed"));
  else
    flashSSD();

  //  Serial.println("Turned off timer");
  timerID1 = timer.setInterval(1000L * 20, refreshWidgets); //
  initRTOS();
  lwdtFeed();
  lwdTicker.attach_ms(LWD_TIMEOUT, lwdtcb); // attach lwdt callback routine to Ticker object
}
void loop()
{
  lwdtFeed();
  Blynk.run();
  timer.run();
}

void flashSSD()
{
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("ESP32");
  display.println("Client PIO");
  display.println(WiFi.localIP());
  display.display();
}

void refreshWidgets() // called every x seconds by SimpleTimer
{
  String sensorsConnected = performHttpGet(ipList);
  if (sensorsConnected.isEmpty())
  {
    Serial.println("Failed to fetch sensorsConnected data");
    return;
  }
  parseSensorsConnected(sensorsConnected);

  Blynk.virtualWrite(V7, passSocket);
  Blynk.virtualWrite(V20, failSocket);
  Blynk.virtualWrite(V19, recoveredSocket);
  Blynk.virtualWrite(V34, retry);
  Blynk.virtualWrite(V39, lastMsg);
}
BLYNK_CONNECTED()
{
  bool isconnected = Blynk.connected();
  if (isconnected == false)
  {
    Serial.println("Blynk Not Connected");
    ESP.restart();
  }
  else
    Serial.println("Blynk Connected");

  getBootTime(lastBoot, strReason);
  Blynk.virtualWrite(V25, lastBoot);
  Blynk.virtualWrite(V26, strReason);

  String payload = performHttpGet(getRowCnt);
  if (payload.isEmpty())
  {
    Serial.println("Failed to fetch ip for connected devices");
    return;
  }
  else
  {
    passSocket = payload.toInt();
    Serial.printf("passSocket %d  \n", passSocket);
  }
}
BLYNK_WRITE(V18)
{
  String payload = performHttpGet(ipDelete);
  if (payload.isEmpty())
  {
    Serial.println("Failed to fetch ip for connected devices or no devices connected");
    return;
  }
}
BLYNK_WRITE(BLINK_TST)
{
  timer.disable(timerID1);
  char *str = nullptr;
  // Iterate through the map
  for (const auto &pair : ipMap)
  {
    Serial.printf("Key: %s, Value: %s\n", pair.first.c_str(), pair.second.c_str());
    str = socketClient((char *)pair.second.c_str(), (char *)"BLK");
    Serial.printf("blk_tst %s \n", str);
    free(str);
  }

  // int index = param.asInt();
  //  char *str = socketClient((char *)ipAddr[index], (char *)"TST");
  //  String foo = String(str);
  //  index = foo.indexOf(":");
  //  skip crc
  //  Blynk.virtualWrite(V12, (foo.substring(index + 1)));
  //  free(str);
  timer.enable(timerID1);
}
void ICACHE_RAM_ATTR lwdtcb(void)
{
  if ((millis() - lwdTime > LWD_TIMEOUT) || (lwdTimeout - lwdTime != LWD_TIMEOUT))
  {
    Blynk.logEvent("3rd_WDTimer");
    Serial.printf("3rd_WDTimer esp.restart %lu %lu\n", (millis() - lwdTime), (lwdTimeout - lwdTime));
    Blynk.virtualWrite(V39, "3rd_WDTimer");
    queStat();
    ESP.restart();
  }
}
void lwdtFeed(void)
{
  lwdTime = millis();
  lwdTimeout = lwdTime + LWD_TIMEOUT;
}
#ifdef TEST
void blynkTimeOn()
{
  timer.enable(timerID1);
}
void blynkTimeOff()
{
  timer.disable(timerID1);
}
#endif

void upDataWidget(char *sensor, float tokens[])
{
  String localSensorName = sensor;
  if (localSensorName == "BMP")
  {
    Blynk.virtualWrite(V4, tokens[1]); // display temp to android app
    Blynk.virtualWrite(V6, 0);         // display humidity
    return;
  }
  if (localSensorName == "SHT")
  {
    Blynk.virtualWrite(V5, tokens[1]);
    Blynk.virtualWrite(V15, tokens[2]);
    return;
  }
  if (localSensorName == "ADC")
  {
    Blynk.virtualWrite(GAUGE_HOUSE, tokens[1]);
    return;
  }
}
// Function to handle HTTP GET requests and return the response as a string
String performHttpGet(const char *url)
{
  http.begin(url);
  int httpResponseCode = http.GET();
  if (httpResponseCode != 200)
  {
    Serial.printf("HTTP GET failed with code: %d\n", httpResponseCode);
    return ""; // Return an empty string on failure
  }
  String response = http.getString();
  http.end();
  return response;
}

// Function to parse the sensorsConnected string and populate the ipMap
void parseSensorsConnected(const String &sensorsConnected)
{
  // Each device/row will have its unique IP address
  // list of devices "2|3,DS1_DS1:192.168.1.5|2,BMP_ADC:192.168.1.7|"

  String rows = sensorsConnected.substring(0, sensorsConnected.indexOf("|"));
  int numberOfRows = atoi(rows.c_str());

#ifdef DEBUG_LIST
  Serial.printf("list of devices: %s", sensorsConnected.c_str());   // warning "\n" in sensorConnected string
#endif

  String deviceConn = sensorsConnected.substring(sensorsConnected.indexOf("|") + 1, sensorsConnected.lastIndexOf("|"));

  for (int i = 0; i < numberOfRows; i++)
  {
    int index = deviceConn.indexOf(":");
    int index1 = deviceConn.indexOf(",");
    int index2 = deviceConn.indexOf("|");


    String ip = deviceConn.substring(index + 1, index2);
    String sensorName = deviceConn.substring(index1 + 1, index);
    ipMap[sensorName.c_str()] = ip.c_str(); // Store the IP address in the map

#ifdef DEBUG
    Serial.printf("Sensor: %s, IP: %s\n", sensorName.c_str(), ip.c_str());
#endif

    if (socketClient((char *)ip.c_str(), (char *)"ALL", 1)) // read sensor data from connected device
    {
      Serial.println("socketClient() failed");
    }

    deviceConn = deviceConn.substring(index2 + 1); // Move to the next device in string
#ifdef DEBUG
    Serial.printf("device connect %s \n ", deviceConn.c_str());
#endif
  } //end for
}
