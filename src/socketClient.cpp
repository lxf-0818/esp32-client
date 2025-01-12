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

uint16_t port = 8888;
extern String lastMsg;
extern int failSocket, passSocket, recoveredSocket, retry;
extern SemaphoreHandle_t mutex_sock;

typedef struct
{
    int (*fun_ptr)(char *, char *, float[], char *, bool);
    char ipAddr[20];
    char cmd[20];
    float tokens[5];
    char sensorName[20];
} socket_t;
socket_t socketQue;
QueueHandle_t QueSocket_Handle;
TaskHandle_t socket_task_handle;
void createSocketTask();
int socketRecovery(char *IP, char *cmd2Send, char *sensor);
void taskSocketRecov(void *pvParameters);

void createSocketTask()
{
    uint32_t socket_delay = 50;
    QueSocket_Handle = xQueueCreate(20, sizeof(socket_t));
    if (QueSocket_Handle == NULL)
        Serial.println("Queue  socket could not be created..");

    xTaskCreatePinnedToCore(taskSocketRecov, "Sockets", 2048, (void *)&socket_delay, 3, &socket_task_handle, 1);
}

int socketClient(char *espServer, char *command, float tokens[], char *sensor, bool updateErorrQue)
{
    uint32_t CRCfromServer;
    char str[80];
    bzero(str, 80);
    WiFiClient client;
    CRC32 crc;

    if (!client.connect(espServer, port))
    {
        if (updateErorrQue)
        {                                               // don't update if in recovery mode ie last i/o failed
            socketRecovery(espServer, command, sensor); // current failed write to error recovery queue
            failSocket++;
            Serial.printf(">>> failed to connect: %s!\n", espServer);
            lastMsg = "failed to connect " + String(espServer);
        }
        return 1;
    }
    if (client.connected())
        client.println(command); // send cmd to esp8266 server  ie "ADC"/"BME"

    unsigned long timeout = millis();
    // wait for data to be available
    while (client.available() == 0)
    {
        if (millis() - timeout > 5000)
        {
            Serial.println(">>> Client Timeout!");
            lastMsg = "Client Timeout " + String(espServer);
            client.stop();
            delay(600);
            if (updateErorrQue)
            {
                socketRecovery(espServer, command, sensor); // write to error recovery queque
                failSocket++;
            }
            return 2;
        }
    }
    int index=0;
    while (client.available()) str[index++] = client.read(); // read sensor data from sever
    client.stop();
    // TODO: need to debug  not working 1/11/25 the server encrypted the data correctly but fails decryption on the client 
#ifndef NO_SOCKET_AES
    decrypt_to_cleartext(str, strlen(str), enc_iv_from, cleartext);
    String copyStr = String(cleartext);
    Serial.printf("clear text %x copy str %s", cleartext, copyStr);
    String copyStr = cleartext;
#else
    String copyStr = str;
#endif
    int mycrc;
    copyStr = String(copyStr);
    index = copyStr.indexOf(":");
    String crcString = copyStr.substring(0, index);
    sscanf(crcString.c_str(), "%x", &mycrc);
   // Serial.printf("from server crc %x\n", mycrc);
    String parsed = copyStr.substring(index + 1);
    crc.add((uint8_t *)parsed.c_str(), parsed.length());
    if (mycrc != crc.calc()) 
    {
        Serial.println("no moatch\n");
        socketRecovery(espServer, command, sensor); // write to error recovery queque
        return 3;
    }
    // crc passed !
    char *token = strtok((char *)parsed.c_str(), ",");
    int j = 0;
    while (token != NULL)
    {
        tokens[j++] = atof(token);
        token = strtok(NULL, ",");
    }
    passSocket++;
    tokens[4] = passSocket; //
    // for (int i = 0; i < 5; i++) Serial.println(tokens[i]);
    return 0;
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
                //timer.disable(timerID1);

                //"take" blocks calls to esp restart while messages are on queue see queStat()
                xSemaphoreTake(mutex_sock, 0);
                vTaskDelay(xDelay);
                retry++;
                int x = (*socketQue.fun_ptr)(socketQue.ipAddr, socketQue.cmd, socketQue.tokens, socketQue.sensorName, NO_UPDATE_FAIL); // don't send fail to queue see below
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