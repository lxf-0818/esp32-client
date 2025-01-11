#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <CRC32.h>
#include <Wire.h>
#define INPUT_BUFFER_LIMIT 2048
#define NO_SOCKET_AES

WiFiClient client;
uint16_t port = 8888;
String lastMsg;
int failSocket, passSocket;
int socketRecovery(char *IP, char *cmd2Send, char *sensor);
typedef struct
{
    int (*fun_ptr)(char *, char *, float[], char *, bool);
    char ipAddr[20];
    char cmd[20];
    float tokens[5];
    char sensorName[20];
} socket_t;
socket_t socketQue;
extern QueueHandle_t QueSocket_Handle;
extern char cleartext[INPUT_BUFFER_LIMIT];
extern byte enc_iv_from[16];
void decrypt_to_cleartext(char *msg, uint16_t msgLen, byte iv[], char *cleartext);

int socketClient(char *espServer, char *command, float tokens[], char *sensor, bool updateErorrQue)
{
    int j = 0;
    uint32_t CRCfromServer;
    char str[80];
    bzero(str, 80);

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

    while (client.available())
        str[j++] = client.read(); // read sensor data from sever
    Serial.printf("data from sever %s\n", str);
    // Close the connection
    client.stop();

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
    int index = copyStr.indexOf(":");
    String crcString = copyStr.substring(0, index);
    sscanf(crcString.c_str(), "%x", &mycrc);
    Serial.printf("from server crc %x\n", mycrc);
    String parsed = copyStr.substring(index + 1);
    crc.add((uint8_t *)parsed.c_str(), parsed.length());
    if (mycrc == crc.calc())  // force
    {
        Serial.println("no moatch\n");
        socketRecovery(espServer, command, sensor); // write to error recovery queque
        return 3;
    }
    // crc passed !
    // Serial.printf("crc %s str %s\n", crcString.c_str(), parsed.c_str());
    char *token = strtok((char *)parsed.c_str(), ",");
    j = 0;
    while (token != NULL)
    {
        tokens[j++] = atof(token);
        token = strtok(NULL, ",");
    }

    passSocket++;
    tokens[4] = passSocket; //
    for (int i = 0; i < 5; i++)
        Serial.println(tokens[i]);
    return 0;
}
// This queue is  ONLY used when an error is detected in "socketClient.cpp"
// ie The server is down or timeout waiting for sensor data from server to client
//
int socketRecovery(char *IP, char *cmd2Send, char *sensor)
{
    Serial.printf("socketRec IP %s\n", IP);
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