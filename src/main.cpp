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
 * - upDateWidget(): Updates Blynk widgets with sensor data based on the sensor type.
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
 * - Debugging can be enabled by defining the DEBUG macron.
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
// #define DEBUG_LIST
// #define DEBUG
//  #define TEMPV6 V6 // Define TEMPV6 as virtual pin V6
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
void upDateWidget(char *sensorName, float tokens[]);
void lwdtFeed(void);
void ICACHE_RAM_ATTR lwdtcb(void);
bool queStat();

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
int lastSize = 0;
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

/**
 * @brief Refreshes the widgets by fetching sensor data and updating Blynk virtual pins.
 *
 * This function is called periodically by a timer (e.g., SimpleTimer). It performs an HTTP GET
 * request to retrieve sensor connection data, parses the data, and updates various Blynk virtual
 * pins with the latest information.
 *
 * @details
 * - Fetches sensor connection data from mySQL using the `performHttpGet` function.
 * - If the data retrieval fails, logs an error message and exits the function.
 * - Parses the fetched data using the `parseSensorsConnected` function.
 * - Updates the following Blynk virtual pins:
 *   - V7: Updated with the value of `passSocket`.
 *   - V20: Updated with the value of `failSocket`.
 *   - V19: Updated with the value of `recoveredSocket`.
 *   - V34: Updated with the value of `retry`.
 *   - V39: Updated with the value of `lastMsg`.
 *
 */
void refreshWidgets() // called every x seconds by SimpleTimer
{
  char tmp[256];
  String sensorsConnected = performHttpGet(ipList);
  if (sensorsConnected.isEmpty())
  {
    Serial.println("Failed to fetch sensorsConnected data");
    return;
  }
  if (lastSize != ipMap.size())
  {
    
    Blynk.virtualWrite(V42, "\n\n"); // clear Blynk terminal
    for (const auto &pair : ipMap)
    {
      Serial.printf("Sensor: %s, IP: %s\n", pair.first.c_str(), pair.second.c_str());
      sprintf(tmp, "Sensor: %s, IP: %s\n", pair.first.c_str(), pair.second.c_str());
      Blynk.virtualWrite(V42, tmp);
    }
    lastSize = ipMap.size();
  }
  else Serial.printf("ipMap did not change\n");
  
  parseSensorsConnected(sensorsConnected);

  Blynk.virtualWrite(V7, passSocket);
  Blynk.virtualWrite(V20, failSocket);
  Blynk.virtualWrite(V19, recoveredSocket);
  Blynk.virtualWrite(V34, retry);
  Blynk.virtualWrite(V39, lastMsg);
}
BLYNK_CONNECTED()
{

  failSocket = recoveredSocket = retry = 0;
  bool isconnected = Blynk.connected();
  if (isconnected == false)
  {
    Serial.println("Blynk Not Connected");
    ESP.restart();
  }
  else
    Serial.println("Blynk Connected");
  refreshWidgets();
  getBootTime(lastBoot, strReason);
  Blynk.virtualWrite(V25, lastBoot);
  Blynk.virtualWrite(V26, strReason);
  Blynk.virtualWrite(V20, failSocket);
  Blynk.virtualWrite(V19, recoveredSocket);
  Blynk.virtualWrite(V34, retry);

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
  refreshWidgets();
  
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
BLYNK_WRITE(V42)
{
  String input = param.asStr(); // Read the input from the terminal widget
  Serial.println("Received from terminal: " + input);
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
    // Blynk.logEvent("3rd_WDTimer");
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

void upDateWidget(char *sensor, float tokens[])
{
  String localSensorName = sensor;
  if (localSensorName == "BME280")
  {
    Blynk.virtualWrite(V4, tokens[1]); // display temp to android app
    Blynk.virtualWrite(V6, tokens[2]); // display humidity
    return;
  }
  if (localSensorName == "SHT35")
  {
    Blynk.virtualWrite(V5, tokens[1]);
    Blynk.virtualWrite(V15, tokens[2]);
    return;
  }
  if (localSensorName == "ADS1115")
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
// #define DEBUG_PHP
#ifdef DEBUG_PHP
  Serial.printf("Payload: %s\n", response.c_str());
#endif
  return response;
}

/**
 * @brief Parses a string containing information about connected sensors and their IP addresses,
 *        and stores the sensor names and IPs in a map. Additionally, attempts to read sensor data
 *        from each connected device using a socket client.
 *
 * @param sensorsConnected A formatted string containing the number of devices and their details.
 *        Format: "<number_of_devices>|<sensor_name1>:<ip1>|<sensor_name2>:<ip2>|..."
 *        Example: "2|DS1_DS1:192.168.1.5|BMP:192.168.1.7|"
 *
 * @note The function assumes that the input string is well-formed and contains valid data.
 *       Debugging information can be enabled by defining the macros DEBUG_LIST and DEBUG.
 *
 * @details The function performs the following steps:
 *          1. Extracts the number of devices from the input string.
 *          2. Iterates through each device's information, extracting the sensor name and IP address.
 *          3. Stores the sensor name and IP address in a map (`ipMap`).
 *          4. Attempts to read sensor data from each device using the `socketClient` function.
 *          5. Logs debugging information if the DEBUG or DEBUG_LIST macros are defined.
 *
 * @warning The function modifies the input string `sensorsConnected` during processing.
 *          Ensure that the input string is not needed elsewhere in its original form.
 *
 * @note If the `socketClient` function fails for a device, an error message is printed to the serial monitor.
 */
void parseSensorsConnected(const String &sensorsConnected)
{

  String rows = sensorsConnected.substring(0, sensorsConnected.indexOf("|"));
  int numberOfRows = atoi(rows.c_str());

#ifdef DEBUG_LIST
  Serial.printf("list of devices: %s", sensorsConnected.c_str()); // warning "\n" in sensorConnected string
#endif

  String deviceConn = sensorsConnected.substring(sensorsConnected.indexOf("|") + 1, sensorsConnected.lastIndexOf("|"));

  for (int i = 0; i < numberOfRows; i++)
  {
    int index = deviceConn.indexOf(":");
    int index1 = deviceConn.indexOf(",");
    int index2 = deviceConn.indexOf("|");
    String ip = deviceConn.substring(index + 1, index2);
    String sensorName = deviceConn.substring(index1 + 1, index);

    // Store the IP address in the map
    ipMap[sensorName.c_str()] = ip.c_str();
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
  } // end for
}
