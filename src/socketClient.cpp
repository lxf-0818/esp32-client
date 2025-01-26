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
//#define DEBUG

extern uint16_t port;
extern String lastMsg;
extern int failSocket, passSocket, recoveredSocket, retry;
void taskSQL_HTTP(void *pvParameters);
void setupHTTP_request(String sensorName, float tokens[]);
int socketRecovery(char *IP, char *cmd2Send, char *sensor);
int socketClient(char *espServer, char *command, char *sensor, bool updateErorrQue);

int socketClient(char *espServer, char *command, char *sensor, bool updateErorrQue)
{
    uint32_t CRCfromServer;
    char str[80];
    bzero(str, 80);
    WiFiClient client;
    float tokens[5][5] = {};
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
    int index = 0;
    while (client.available())
        str[index++] = client.read(); // read sensor data from sever
    Serial.printf("str %s\n", str);
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
    int j = 0, z = 0;
    while (token != NULL)
    {
        if (!strcmp(token, "|"))
        {
            z++;
            j = 0;
        }
        else
            tokens[z][j++] = atof(token);

        token = strtok(NULL, ",");
    }
    passSocket++;
#ifdef DEBUG
    for (int i = 0; i < 5; i++)
    {
        for (int j = 0; j < 5; j++)
            Serial.printf("%f ", tokens[i][j]);
        Serial.println();
    }
#endif

    for (int i = 0; i < 5; i++)
    {
        switch ((int)tokens[i][0])
        {
        case 58:
            strcpy(sensor, "BMP");
            break;
        case 44:
            strcpy(sensor, "SHT");
            break;
        case 48:
            strcpy(sensor, "ADC");
            break;
        default:
            return 0;
        }
        setupHTTP_request(sensor, tokens[i]);
        // upDateWidget(sensor,readings[i]);
    }
    return 0;
}