#include <Arduino.h>
#include <FS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <CRC32.h>
#include <Wire.h>

#define NO_UPDATE_FAIL 0
#define INPUT_BUFFER_LIMIT 2048
#define NO_SOCKET_AES
#define MAX_LINE_LENGTH 120
#define LED_BUILTIN 2
SemaphoreHandle_t xMutex_sock, xMutex_http;

uint16_t port = 8888;
extern String lastMsg;
extern int failSocket, passSocket, recoveredSocket, retry;
int socketClient(char *espServer, char *command, bool updateErorrQue);

#ifdef TEST
void blynkTimeOn();
void blynkTimeOff();
#endif
int deleteRow(String phpScript);
bool queStat();
typedef struct
{
    int (*fun_ptr)(char *, char *, bool);
    char ipAddr[20];
    char cmd[20];
} socket_t;
socket_t socketQue;

typedef struct
{
    char device[10];
    char line[MAX_LINE_LENGTH];
    int key;
} message_t;
message_t message;

QueueHandle_t QueSocket_Handle;
QueueHandle_t QueHTTP_Handle;
TaskHandle_t socket_task_handle, http_task_handle, blink_task_handle;
void initRTOS();
int socketRecovery(char *IP, char *cmd2Send);
void taskSocketRecov(void *pvParameters);
void taskSQL_HTTP(void *pvParameters);
void setupHTTP_request(String sensorName, float tokens[]);
void TaskBlink(void *pvParameters);

void initRTOS()
{
    uint32_t socket_delay = 50, http_delay = 2000, blink_delay = 1000;
    pinMode(LED_BUILTIN, OUTPUT);

    QueSocket_Handle = xQueueCreate(5, sizeof(socket_t));
    if (QueSocket_Handle == NULL)
        Serial.println("Queue  socket could not be created..");

    QueHTTP_Handle = xQueueCreate(5, sizeof(message_t));
    if (QueHTTP_Handle == NULL)
        Serial.println("Queue could not be created..");

    Serial.printf("initRTOS http %u socket %u blink %u\n", http_delay, socket_delay, blink_delay);

    xTaskCreatePinnedToCore(TaskBlink, "Task Blink", 2048, (uint32_t *)&blink_delay, 1, &blink_task_handle, 1);
    xTaskCreatePinnedToCore(taskSocketRecov, "Sockets", 2048, (uint32_t *)&socket_delay, 3, &socket_task_handle, 1);
    xTaskCreatePinnedToCore(taskSQL_HTTP, "http", 2048, (uint32_t *)&http_delay, 2, &http_task_handle, 0);

    xMutex_sock = xSemaphoreCreateMutex();
    if (xMutex_sock == NULL)
    {
        Serial.println("Mutex sock can not be created");
    }
    xMutex_http = xSemaphoreCreateMutex();
    if (xMutex_http == NULL)
    {
        Serial.println("Mutex sock can not be created");
    }
}

// This queue is  ONLY used when a socket error is detected in  fucntion "socketClient" 
// ie The server is down or timeout waiting for sensor data from the server
//
int socketRecovery(char *IP, char *cmd2Send)
{
    socket_t socketQue;
    if (QueSocket_Handle == NULL)
        Serial.println("QueSocket_Handle failed");
    else
    {
        socketQue.fun_ptr = &socketClient;
        strcpy(socketQue.ipAddr, IP);
        strcpy(socketQue.cmd, cmd2Send);
        int ret = xQueueSend(QueSocket_Handle, (void *)&socketQue, 0);
        if (ret == pdTRUE)
        { /* Serial.println("recovering struct send to QueSocket sucessfully"); */
        }
        else if (ret == errQUEUE_FULL)
        {
            Serial.println(".......unable to send data to socket  Queue is Full");
            String phpScript = "http://192.168.1.252/deleteMAC.php?key=" +
                               (String) "'" + (String)WiFi.macAddress() +  "'";
            deleteRow(phpScript); // delete Blynk.logEvent("3rd_WDTimer");

            xQueueReset(QueSocket_Handle);

            // delete entry in DB
        }
        return ret;
    }
    return 10;
}
// Start Task for IO fails on TCP/IP Error Connections

void taskSQL_HTTP(void *pvParameters)
{

    // This task logs the sensor data to mysql using POST()
    // HTTP Procol is slow .5sec - 2sec, per POST  therefore run on core 0 let core 1 run real time
    HTTPClient http;
    // mysql includes
    WiFiClient client_sql;
    int passPost = 0, failPost = 0, recovered = 0;
    String serverName = "http://192.168.1.252/post-esp-data.php";

    uint32_t http_delay = *((uint32_t *)pvParameters);
    TickType_t xDelay = http_delay / portTICK_PERIOD_MS;
    Serial.printf("Task Post SQL running on coreID:%d  xDelay %u , %u\n", xPortGetCoreID(), xDelay, http_delay);
    // if (xDelay != 2000)
    // {
    //     ESP.restart();
    // }
    for (;;)
    {
        if (QueHTTP_Handle != NULL)
        {
            int ret = xQueueReceive(QueHTTP_Handle, &message, portMAX_DELAY); // wait for message
            if (ret == pdPASS)
            {
                //  "take" blocks calls to esp restart while messages are on queue see queStat()
                xSemaphoreTake(xMutex_http, 0);
                http.begin(client_sql, serverName.c_str());
                http.addHeader("Content-Type", "application/x-www-form-urlencoded");
                delay(500);
                int httpResponseCode = http.POST(message.line);
                if (httpResponseCode > 0)
                {
                    passPost++;
                    String payload = http.getString();
                }
                else
                {
                    String phpScript = "http://192.168.1.252/delete.php?key=" + message.key;
                    Serial.println(phpScript);
                    failPost++;
                    int j = 0, rc = 0;
                    while (1)
                    {
                        vTaskDelay(xDelay); // mysql is slow wait (non-blocking other task won't be affected)
                        rc = deleteRow(phpScript);
                        if (rc || j++ == 5)
                            break; //
                    }
                    Serial.printf("rc %d\n", rc);
                    Serial.printf("HTTP Error rc: %d %s %d \n", httpResponseCode, message.line, message.key);
                    Serial.printf("passed %d  failed %d ", passPost, failPost);
                    int ret = xQueueSend(QueHTTP_Handle, (void *)&message, 0); // send message back to queue
                    if (ret == pdTRUE)
                        recovered++;                            //
                    Serial.printf("recoverd %d \n", recovered); // checked mySQL and the entry exists
                }
                http.end();
                vTaskDelay(xDelay);
                xSemaphoreGive(xMutex_http);
            }
            else if (ret == pdFALSE)
                Serial.println("The setSQL_HTTP was unable to receive data from the Queue");
        } // Sanity check
    }
}
void taskSocketRecov(void *pvParameters)
{
    socket_t socketQue;
    uint32_t socket_delay = *((uint32_t *)pvParameters);
    const TickType_t xDelay = socket_delay / portTICK_PERIOD_MS;
    Serial.printf("Task Socket Recover running on coreID:%d  xDelay:%u\n", xPortGetCoreID(), xDelay);
    for (;;)
    {
        if (QueSocket_Handle != NULL)
        {
            if (xQueueReceive(QueSocket_Handle, &socketQue, portMAX_DELAY) == pdPASS)
            {
                //"take" blocks calls to esp restart when messages are on queue
                // see queStat()
                xSemaphoreTake(xMutex_sock, 0);
                vTaskDelay(xDelay);
                retry++;
                // Serial.printf("socket error %s %s \n", socketQue.ipAddr, socketQue.cmd);
                int x = (*socketQue.fun_ptr)(socketQue.ipAddr, socketQue.cmd, NO_UPDATE_FAIL);
                if (!x)
                {
                    recoveredSocket++;
                    Serial.printf("Recovered last network fail for host:%s cmd:%s \n", socketQue.ipAddr, socketQue.cmd);
                    Serial.printf("passSocket %d failSocket %d  recovered %d retry %d \n", passSocket, failSocket, recoveredSocket, retry);
                }
                else
                    socketRecovery(socketQue.ipAddr, socketQue.cmd); //  ********SEND Fail to que here for recovery****
                xSemaphoreGive(xMutex_sock);
            }
        }
    }
}

void setupHTTP_request(String sensorName, float tokens[])
{
    message_t message;
    String apiKeyValue = "tPmAT5Ab3j7F9";
    String sensorLocation = "HOME";
    extern int passSocket;

    if (QueHTTP_Handle != NULL && uxQueueSpacesAvailable(QueHTTP_Handle) > 0)
    {

        String httpRequestData = "api_key=" + apiKeyValue + "&sensor=" + sensorName +
                                 "&location=" + sensorLocation + "&value1=" +
                                 String(tokens[1]) + "&value2=" + String(tokens[2]) + "&value3=" + String(passSocket) + "";
        strcpy(message.line, httpRequestData.c_str());
        message.key = tokens[3];
        message.line[strlen(message.line)] = 0; // Add the terminating nul char4
        int ret = xQueueSend(QueHTTP_Handle, (void *)&message, 0);
        if (ret == pdTRUE)
        {
            /*  Serial.println(" msg struct send to QueSocket sucessfully"); */
        }
        else if (ret == errQUEUE_FULL)
            Serial.println(".......unable to send data to htpp Queue is Full");
    }
}
void TaskBlink(void *pvParameters)
{
    uint32_t blink_delay = *((uint32_t *)pvParameters);
    const TickType_t xDelay = blink_delay / portTICK_PERIOD_MS;
    Serial.printf("Task Blink/Blynk running on coreID:%d xDelay:%u\n", xPortGetCoreID(), xDelay);
    for (;;)
    {
        digitalWrite(LED_BUILTIN, LOW);
        vTaskDelay(xDelay);
        digitalWrite(LED_BUILTIN, HIGH);
        vTaskDelay(xDelay);
    }
}
bool queStat()
{
    unsigned long timeout = millis();
    while (1)
    {
        if (millis() - timeout > 5000)
        {
            Serial.println(">>> Queue Timeout!");
            return false;
        }
        if ((uxQueueMessagesWaiting(QueSocket_Handle) == 0) &&
            (uxQueueMessagesWaiting(QueHTTP_Handle) == 0))
        {
            Serial.println("no messages on que");
            xSemaphoreTake(xMutex_sock, portMAX_DELAY);
            xSemaphoreTake(xMutex_http, portMAX_DELAY);
            Serial.println("tasks are now complete........bye!");
            break;
        }
        else
        {
            delay(1000);
            Serial.println(".... que busy");
        }
    }

    return true;
}
