#define BLYNK_TEMPLATE_ID "TMPL2sDJhOygV"
#define BLYNK_TEMPLATE_NAME "House"
#define BLYNK_AUTH_TOKEN "3plcY4yZM3HpnupyR5nmnDlUcXADV9sU"
#include <Arduino.h>

#include <FS.h>
#include <SPIFFS.h>
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
#define ROOM1 "192.168.1.181"
#define ROOM2 "192.168.1.182"
#define CLNT "192.168.1.179"
#define BLYNK_PRINT Serial
String sensorName = "NO DEVICE";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#define SSD_ADDR 0x3c
void initRTOS();
void flashSSD();
void refreshWidgets();
int readCiphertext(char *ssid, char *psw);
int socketClient(char *espServer, char *command, char *sensor, bool updateErorrQue);
void getTemp();
void getBootTime();

const uint16_t port = 8888;
int failSocket, passSocket, recoveredSocket, retry, timerID1, passPost;
BlynkTimer timer;
float tokens[5];
bool setAlarm = false;
#include <Ticker.h>
Ticker lwdTicker;
#define LWD_TIMEOUT 15 * 1000 // Reboot if loop watchdog timer reaches this time out value
unsigned long lwdTime = 0;
unsigned long lwdTimeout = LWD_TIMEOUT;
SemaphoreHandle_t mutex_http, mutex_sock;
const char *getRowCnt = "http://192.168.1.252/rows.php";

// WiFiServer server(80);
HTTPClient http;
String apiKeyValue = "tPmAT5Ab3j7F9";
String lastMsg;

void setup()
{
  Serial.begin(115200);
  char ssid[40], pass[40];
  char auth[] = BLYNK_AUTH_TOKEN;
  readCiphertext(ssid, pass);
  Blynk.begin(auth, ssid, pass);
  // server.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, SSD_ADDR))
    Serial.println(F("SSD1306 allocation failed"));
  else
    flashSSD();

  mutex_sock = xSemaphoreCreateMutex();
  if (mutex_sock == NULL)
  {
    Serial.println("Mutex sock can not be created");
  }
  mutex_http = xSemaphoreCreateMutex();
  if (mutex_http == NULL)
  {
    Serial.println("Mutex sock can not be created");
  }

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
  sensorName = "ROOM1";
  if (socketClient((char *)ROOM1, (char *)"ALL", (char *)sensorName.c_str(), 1)) // get indoor temp
    Serial.println("socketClient() failed");

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

  // delay(2000);
  http.begin(getRowCnt);
  int httpResponseCode = http.GET();
  Serial.printf("httpResponseCode:%d\n", httpResponseCode);
  if (httpResponseCode != 200)
  {
    //  ESP.restart();
  }
  String payload = http.getString();
  Serial.println(payload);
  passSocket = payload.toInt();
  Serial.printf("passSocket %d failSocket %d  recovered %d retry %d \n", passSocket, failSocket, recoveredSocket, retry);

  http.end();
}