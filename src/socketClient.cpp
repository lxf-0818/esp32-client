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
// #define DEBUG

extern uint16_t port;
extern String lastMsg;
extern int failSocket, passSocket, recoveredSocket, retry;
void taskSQL_HTTP(void *pvParameters);
void setupHTTP_request(String sensorName, float tokens[]);
int socketRecovery(char *IP, char *cmd2Send);
int socketClient(char *espServer, char *command, bool updateErorrQue);
void upDateWidget(char *sensor, float tokens[]);

int socketClient(char *espServer, char *command, bool updateErorrQue)
{
    float tokens[5][5] = {};
    char str[80];
    bzero(str, 80);
    WiFiClient client;
    CRC32 crc;
    if (!updateErorrQue)
        Serial.printf("in err reoc %s %s\n", espServer, command);

    if (!client.connect(espServer, port))
    {
        if (updateErorrQue)
        {                                       // don't update if in recovery mode ie last i/o failed
            socketRecovery(espServer, command); // current failed write to error recovery queue
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
                socketRecovery(espServer, command); // write to error recovery queque
                failSocket++;
            }
            return 2;
        }
    }
    int index = 0;
    while (client.available())
        str[index++] = client.read(); // read sensor data from sever
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
        socketRecovery(espServer, command); // write to error recovery queque
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
// #define DEBUG
#ifdef DEBUG
    for (int i = 0; i < 5; i++)
    {
        if (!tokens[i][0])
            break;

        for (int j = 0; j < 5; j++)
        {

            Serial.printf("%f ", tokens[i][j]);
        }
        Serial.println();
    }
#endif
    char sensor[10];
    for (int i = 0; i < 5; i++)
    {
        switch ((int)tokens[i][0])
        {
        case 77:
            strcpy(sensor, "BME");
            break;
        case 58:
            strcpy(sensor, "BMP");
            break;
        case 44:
            strcpy(sensor, "SHT");
            break;
        case 48:
            strcpy(sensor, "ADC");
            break;
        case 28:
            strcpy(sensor, "DS1");
            break;
        default:
            return 0;
        }
        passSocket++;
        setupHTTP_request(sensor, tokens[i]);
        if (updateErorrQue)   // this is a "band aid" crashes in error recovering mode WTF FM! 
            upDateWidget(sensor, tokens[i]);   // skip if in error recover
    }
    return 0;
}
// this overload returns malloc its your do Diligence to free!!!
char *socketClient(char *espServer, char *command)
{
    int j = 0;
    WiFiClient client;
    if (!client.connect(espServer, port))
    {
        Serial.print("connection failed from socketClient ");
        Serial.println(espServer);
        delay(5000);
        return NULL;
    }
    if (client.connected())
        client.println(command); // set cmd to server (esp8266) ie "ADC"/"BMP"

    unsigned long timeout = millis();
    // wait for data to be available
    while (client.available() == 0)
    {
        if (millis() - timeout > 15000)
        {
            Serial.println(">>> Client Timeout !");
            client.stop();
            delay(600);
            return NULL;
        }
    }
    char *mem = (char *)malloc(80);
    if (mem == NULL)
    {
        // bad boy did you free in caller
        // Blynk.logEvent("mem_alloc_failed");
        // queStat();
        ESP.restart();
    }
    // read sensor data from sever
    while (client.available())
    { // read data from server (esp8266)
        char ch = static_cast<char>(client.read());
        mem[j++] = ch;
    }

    // Close the connection
    client.stop();
    // Serial.println("closing connection");

    mem[j--] = '\0';
    return mem;
}