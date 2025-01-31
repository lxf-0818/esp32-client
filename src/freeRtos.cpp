#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
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

uint16_t port = 8888;
extern String lastMsg;
extern int failSocket, passSocket, recoveredSocket, retry;
int socketClient(char *espServer, char *command, char *sensor, bool updateErorrQue);
extern SemaphoreHandle_t mutex_sock, mutex_http;
bool queStat();
typedef struct
{
    int (*fun_ptr)(char *, char *, char *, bool);
    char ipAddr[20];
    char cmd[20];
    char sensorName[20];
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
int socketRecovery(char *IP, char *cmd2Send, char *sensor);
void taskSocketRecov(void *pvParameters);
void taskSQL_HTTP(void *pvParameters);
void setupHTTP_request(String sensorName, float tokens[]);
void TaskBlink(void *pvParameters);

void initRTOS()
{
    uint32_t socket_delay = 50;
    uint32_t http_delay = 2000;
    uint32_t blink_delay = 1000;
    pinMode(LED_BUILTIN, OUTPUT);

    QueSocket_Handle = xQueueCreate(20, sizeof(socket_t));
    if (QueSocket_Handle == NULL)
        Serial.println("Queue  socket could not be created..");

    QueHTTP_Handle = xQueueCreate(5, sizeof(message_t));
    if (QueHTTP_Handle == NULL)
        Serial.println("Queue could not be created..");

    xTaskCreatePinnedToCore(taskSocketRecov, "Sockets", 2048, (void *)&socket_delay, 3, &socket_task_handle, 1);
    xTaskCreatePinnedToCore(taskSQL_HTTP, "http", 2048, (void *)&http_delay, 2, &http_task_handle, 0);
    xTaskCreatePinnedToCore(TaskBlink, "Task Blink", 2048, (void *)&blink_delay, 1, &blink_task_handle, 1);
}

// This queue is  ONLY used when a socket error is detected in  fucntion "socketClient" above
// ie The server is down or timeout waiting for sensor data from the server
//
int socketRecovery(char *IP, char *cmd2Send, char *sensor)
{
    socket_t socketQue;
    if (QueSocket_Handle == NULL)
        Serial.println("QueSocket_Handle failed");
    else
    {
        socketQue.fun_ptr = &socketClient;
        strcpy(socketQue.ipAddr, IP);
        strcpy(socketQue.cmd, cmd2Send);
        strcpy(socketQue.sensorName, sensor);
        int ret = xQueueSend(QueSocket_Handle, (void *)&socketQue, 0);
        if (ret == pdTRUE)
        { /* Serial.println("recovering struct send to QueSocket sucessfully"); */
        }
        else if (ret == errQUEUE_FULL)
            Serial.println(".......unable to send data to socket  Queue is Full");

        return ret;
    }
    return 10;
}

void taskSocketRecov(void *pvParameters)
{
    socket_t socketQue;
    uint32_t socket_delay = *((uint32_t *)pvParameters);
    const TickType_t xDelay = socket_delay / portTICK_PERIOD_MS;
    Serial.printf("Task Socket Recov running on coreID:%d  xDelay:%lu\n", xPortGetCoreID(), xDelay);
    for (;;)
    {
        if (QueSocket_Handle != NULL)
        {
            int rc = xQueueReceive(QueSocket_Handle, &socketQue, portMAX_DELAY);
            if (rc == pdPASS)
            {
                //"take" blocks calls to esp restart when messages are on queue
                // see queStat()
                xSemaphoreTake(mutex_sock, 0);
                vTaskDelay(xDelay);
                retry++;
                int x = (*socketQue.fun_ptr)(socketQue.ipAddr, socketQue.cmd,
                                             socketQue.sensorName, NO_UPDATE_FAIL); // don't send fail to queue see below
                if (!x)
                {
                    recoveredSocket++;
                    Serial.printf("passSocket %d failSocket %d  recovered %d retry %d \n", passSocket, failSocket, recoveredSocket, retry);
                    Serial.printf("Recovered last network fail for host:%s cmd:%s sensor:%s\n", socketQue.ipAddr, socketQue.cmd, socketQue.sensorName);
                }
                else
                    socketRecovery(socketQue.ipAddr, socketQue.cmd, socketQue.sensorName); //  ********SEND Fail to que here for recovery****
                xSemaphoreGive(mutex_sock);
            }
        }
    }
}
void taskSQL_HTTP(void *pvParameters)
{

    // This task logs the sensor data to mysql using POST()
    HTTPClient http;
    // mysql includes
    WiFiClient client_sql;
    int passPost = 0, failPost = 0, recovered = 0;

    String serverName = "http://192.168.1.252/post-esp-data.php";
    uint32_t http_delay = *((uint32_t *)pvParameters);
    const TickType_t xDelay = http_delay / portTICK_PERIOD_MS;
    Serial.printf("Task Post SQL running on coreID:%d  xDelay %lu\n", xPortGetCoreID(), xDelay);
    for (;;)
    {
        if (QueHTTP_Handle != NULL)
        {
            int ret = xQueueReceive(QueHTTP_Handle, &message, portMAX_DELAY); // wait for message
            if (ret == pdPASS)
            {
                //  "take" blocks calls to esp restart while messages are on queue see queStat()
                xSemaphoreTake(mutex_http, 0);
                http.begin(client_sql, serverName.c_str());
                http.addHeader("Content-Type", "application/x-www-form-urlencoded");
                delay(500);
                int httpResponseCode = http.POST(message.line);
                if (httpResponseCode > 0)
                {

                    passPost++;
                    String payload = http.getString();
                    char *token = strtok((char *)payload.c_str(), "|"); //
                    int pID = atoi(token);
                    //   Serial.printf("DB  pid %d  passPost %d payload %s \n", pID, passPost, payload.c_str());

                    //  if (pID != passPost)
                    //   Serial.printf("DB corrupted pid %d  passPost %d payload %s \n", pID, passPost, payload.c_str());
                }
                else
                {
                    failPost++;
                    int j = 0, rows = 0;
                    while (1)
                    {
                        vTaskDelay(xDelay); // mysql is slow wait (non-blocking other task won't be affected)
                        // rows = rollBack(message.key);
                        if (rows >= 0 || j == 5)
                            break; // if http error call back
                        j++;
                    }
                    Serial.printf("rows %d\n", rows);
                    Serial.printf("HTTP Error rc: %d %s %d \n", httpResponseCode, message.line, message.key);
                    Serial.printf("passed %d  failed %d ", passPost, failPost);
                    int ret = xQueueSend(QueHTTP_Handle, (void *)&message, 0); // send message back to queue
                    if (ret == pdTRUE)
                        recovered++;                            //
                    Serial.printf("recoverd %d \n", recovered); // checked mySQL and the entry exists
                }
                http.end();
                vTaskDelay(xDelay);
                xSemaphoreGive(mutex_http);
            }
            else if (ret == pdFALSE)
                Serial.println("The setSQL_HTTP was unable to receive data from the Queue");
        } // Sanity check
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

        String httpRequestData = "api_key=" + apiKeyValue + "&sensor=" + sensorName + "&location=" + sensorLocation + "&value1=" +
                                 String(tokens[1]) + "&value2=" + String(tokens[2]) + "&value3=" + String(passSocket) + "";
        strcpy(message.line, httpRequestData.c_str());
        message.key = tokens[4];
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
    Serial.printf("Task Blink/Blynk running on coreID:%d xDelay:%lu\n", xPortGetCoreID(), xDelay);
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
        if ((uxQueueMessagesWaiting(QueSocket_Handle) == 0) && (uxQueueMessagesWaiting(QueHTTP_Handle) == 0))
        {
            Serial.println("no messages on que");
            xSemaphoreTake(mutex_http, portMAX_DELAY);
            xSemaphoreTake(mutex_sock, portMAX_DELAY);
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
