#define BLYNK_TEMPLATE_ID "TMPL2sDJhOygV"
#define BLYNK_TEMPLATE_NAME "House"
#define BLYNK_AUTH_TOKEN "3plcY4yZM3HpnupyR5nmnDlUcXADV9sU"
#include <Arduino.h>

#include <FS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <BlynkSimpleEsp32.h>
#include <time.h>
#include <CRC32.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#define INPUT_BUFFER_LIMIT 2048
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define CLNT "192.168.1.179"
#define BLYNK_PRINT Serial
// #define DEBUG
String sensorName = "NO DEVICE";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#define SSD_ADDR 0x3c
void initRTOS();
void flashSSD();
void refreshWidgets();
int readCiphertext(char *ssid, char *psw);
int socketClient(char *espServer, char *command,bool updateErorrQue);
bool queStat();
void getBootTime();
void blynkTimeOn();
void blynkTimeOff();

const uint16_t port = 8888;
int failSocket, passSocket, recoveredSocket, retry, timerID1, passPost;
String devicesConnected;
BlynkTimer timer;
float tokens[5];
bool setAlarm = false;
#include <Ticker.h>
Ticker lwdTicker;
#define LWD_TIMEOUT 15 * 1000 // Reboot if loop watchdog timer reaches this time out value
unsigned long lwdTime = 0;
unsigned long lwdTimeout = LWD_TIMEOUT;
const char *getRowCnt = "http://192.168.1.252/rows.php";
const char *deleteAll = "http://192.168.1.252/deleteALL.php";
const char *ipList = "http://192.168.1.252/ip.php";
const char *macList = "http://192.168.1.252/getMAC.php";
const char *ipDelete = "http://192.168.1.252/deleteIP.php";
const char *esp_data = "http://192.168.1.252/esp-data.php";

// WiFiServer server(80);
HTTPClient http;
String apiKeyValue = "tPmAT5Ab3j7F9";
String lastMsg;

void setup()
{
  Serial.begin(115200);

  char ssid[40], pass[40];
  char auth[] = BLYNK_AUTH_TOKEN;

  readCiphertext(ssid, pass); //  ssid and password are encrypted on LittleFS
  Blynk.begin(auth, ssid, pass);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SSD_ADDR))
    Serial.println(F("SSD1306 allocation failed"));
  else
    flashSSD();

  //  Serial.println("Turned off timer");
  timerID1 = timer.setInterval(1000L * 20, refreshWidgets); //
  initRTOS();
}
void loop()
{
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
  String sensorName, ip;
  int index, index1, index2;

  http.begin(ipList);
  int httpResponseCode = http.GET();
  if (httpResponseCode != 200)
  {
    //  ESP.restart();
  }
  devicesConnected = http.getString();
  // Each device/row will have its unique IP address 
  // list of devices 2|10,BMP_ADC:192.168.1.7|8,BME:192.168.1.8|

  String rows = devicesConnected.substring(0, devicesConnected.indexOf("|"));
  int numberOfRows = atoi(rows.c_str());
#ifdef DEBUG
  Serial.printf("list of devices %s", devicesConnected.c_str());
#endif

  index = devicesConnected.indexOf("|");
  String deviceConn = devicesConnected.substring(index + 1, devicesConnected.lastIndexOf("|"));
  for (int i = 0; i < numberOfRows; i++)
  {
    index = deviceConn.indexOf(":");
    index1 = deviceConn.indexOf(",");
    sensorName = deviceConn.substring(index1 + 1, index);

    index2 = deviceConn.indexOf("|");
    ip = deviceConn.substring(index + 1, index2);
    deviceConn = deviceConn.substring(index2 + 1);

#ifdef DEBUG
    Serial.printf(" sensor %s ip %s \n", sensorName.c_str(), ip.c_str());
#endif

    if (socketClient((char *)ip.c_str(), (char *)"ALL", 1))
      Serial.println("socketClient() failed");
  }

  // Blynk.virtualWrite(V7, passSocket);
  // Blynk.virtualWrite(V20, failSocket);
  // Blynk.virtualWrite(V19, recoveredSocket);
  // Blynk.virtualWrite(V34, retry);
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
  getBootTime();

  http.begin(getRowCnt);
  int httpResponseCode = http.GET();
  Serial.printf("httpResponseCode:%d\n", httpResponseCode);
  if (httpResponseCode != 200)
  {
    //  ESP.restart();
  }
  String payload = http.getString();
  passSocket = payload.toInt();
  Serial.printf("passSocket %d  \n", passSocket);
  http.end();
}
// BLYNK_WRITE(BLINK_TST) {
//   timer.disable(timerID1);
//   int index = param.asInt();
//   char *str = socketClient((char *)ipAddr[index], (char *)"TST");
//   String foo = String(str);
//   index = foo.indexOf(":");
//   //skip crc
//   Blynk.virtualWrite(V12, (foo.substring(index + 1)));
//   free(str);
//   timer.enable(timerID1);
// }
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
// #define FOO
#ifdef FOO
void blynkTimeOn()
{
  timer.enable(timerID1);
}
void blynkTimeOff()
{
  timer.disable(timerID1);
}
#endif
