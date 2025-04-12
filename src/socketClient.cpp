/**
 * @file socketClient.cpp
 * @brief Implementation of socket client communication for ESP32.
 *
 * This file contains functions for handling socket communication with an ESP8266 server,
 * processing sensor data, and managing error recovery. It also includes utility functions
 * for handling sensor data and CRC validation.
 *
 * @details
 * - The `socketClient` function handles communication with the server, including sending
 *   commands, receiving data, and validating the received data using CRC.
 * - The `processSensorData` function processes the received sensor data and updates widgets 
 *   and sends HTTP requests based on the sensor type.
 * - The `printTokens` function is a debug utility for printing parsed sensor data.
 * - The file also includes an overloaded version of `socketClient` that returns a dynamically
 *   allocated buffer containing the server's response.
 *
 * @note
 * - The `NO_SOCKET_AES` macro disables AES decryption for socket communication.
 * - The `DEBUG` macro enables debug output for token printing.
 * - The file uses a map to associate sensor ids with their corresponding sensor names.
 *
 * @dependencies
 * - Arduino framework
 * - WiFi library for ESP32
 * - CRC32 library for checksum validation
 * - HTTPClient library for HTTP requests
 * - FS library for file system operations
 * - Wire library for I2C communication
 *
 * @author Leon Freimour
 * @date 2025-03-30
 */
#include <Arduino.h>
#include <FS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <CRC32.h>
#include <Wire.h>
#include <map>
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
int socketClient(char *espServer, char *command, bool updateErrorQueue);
void upDataWidget(char *sensor, float tokens[]);
void processSensorData(float tokens[5][5], bool updateErrorQueue);
void printTokens(float tokens[5][5]);

/**
 * @brief Establishes a socket connection to a server, sends a command, and processes the response.
 * 
 * @param espServer A pointer to a character array containing the server address.
 * @param command A pointer to a character array containing the command to send to the server.
 * @param updateErrorQueue A boolean flag indicating whether to update the error recovery queue in case of failure.
 * 
 * @return int Returns:
 *         - 0 on success.
 *         - 1 if the connection to the server fails.
 *         - 2 if the client times out while waiting for a response.
 *         - 3 if the CRC validation fails.
 * 
 * @details
 * The function performs the following steps:
 * 1. Attempts to connect to the server using the provided address and port.
 * 2. Sends the specified command to the server if the connection is successful.
 * 3. Waits for a response from the server with a timeout of 5 seconds.
 * 4. Reads the response data and optionally decrypts it if AES encryption is enabled.
 * 5. Validates the response using CRC to ensure data integrity.
 * 6. Parses the response data into tokens and processes the sensor data.
 * 
 * If the connection fails, times out, or CRC validation fails, the function updates the error recovery queue
 * (if `updateErrorQueue` is true) and increments the failure counter (`failSocket`).
 * 
 * @note The function uses global variables such as `lastMsg`, `failSocket`, and `port`.
 * 
 * @warning Ensure that the server address and command strings are properly null-terminated.
 * 
 * @todo Debug and fix the decryption logic as it is currently not working.
 */
int socketClient(char *espServer, char *command, bool updateErrorQueue)
{
    float tokens[5][5] = {};
    char str[80];
    bzero(str, 80);
    WiFiClient client;
    CRC32 crc;

    if (!client.connect(espServer, port))
    {
        if (updateErrorQueue)
        {                                       // don't update if in recovery mode ie last i/o failed
            socketRecovery(espServer, command); // current failed write to error recovery queue
            failSocket++;
            Serial.printf(">>> failed to connect: %s!\n", espServer);
            lastMsg = "failed to connect " + String(espServer);
        }
        return 1;
    }

    if (client.connected())
        client.println(command); // send cmd to esp8266 server  ie ALL/BLK/RST

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
            if (updateErrorQueue)
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
        lastMsg = "CRC invalid " + String(espServer);
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
#ifdef DEBUG_TOKENS
    printTokens(tokens);
#endif
    processSensorData(tokens, updateErrorQueue);

    return 0;
}
/**
 * @brief Processes sensor data and performs actions based on sensor type.
 *
 * This function takes a 2D array of sensor data tokens and processes each sensor's data
 * by identifying the sensor type using a predefined mapping. It then performs specific
 * actions such as setting up an HTTP request and updating a data widget if required.
 *
 * @param tokens A 2D array of floats where each row represents a sensor's data.
 *               The first element in each row is the sensor code used for identification.
 * @param updateErrorQueue A boolean flag indicating whether to update the error queue
 *                         (e.g., update the data widget) for the processed sensor.
 *
 * @note The function uses a predefined mapping of sensor codes to sensor names.
 *       If an unknown sensor code is encountered, the function will terminate early.
 *
 * @warning Ensure that the `tokens` array contains exactly 5 rows and each row has 5 elements.
 *          Behavior is undefined if the array dimensions are incorrect.
 */
void processSensorData(float tokens[5][5], bool updateErrorQueue)
{
    // Map sensor id to their corresponding sensor names for identification
    const std::map<int, const char *> sensorMap =
        {
            {77, "BMP390"},
            {76, "BME280"},
            {58, "BMP280"},
            {44, "SHT"},
            {48, "ADC"},
            {28, "DS1"}};

    char sensor[10];
    for (int i = 0; i < 5; i++)
    {
        int sensorCode = static_cast<int>(tokens[i][0]);
        auto it = sensorMap.find(sensorCode);
        if (it != sensorMap.end())
        {
            strcpy(sensor, it->second);
            passSocket++;
            setupHTTP_request(sensor, tokens[i]);
            if (updateErrorQueue)    // got a bug , guru exception 
            {
                upDataWidget(sensor, tokens[i]);
            }
        }
        else
        {
            // Unknown sensor code
            return;
        }
    }
}
void printTokens(float tokens[5][5])
{
    for (int i = 0; i < 5; i++)
    {
        if (!tokens[i][0])
            break;

        for (int j = 0; j < 5; j++)
        {
            if (j == 0)
                Serial.printf("sensor code %d ", static_cast<int>(tokens[i][j]));
            else
                Serial.printf("%f ", tokens[i][j]);
        }
        Serial.println();
    }
}
// this overload returns malloc its your duty to free!!!
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
        client.println(command); // send cmd to server (esp8266) ie "BLK"/"RST"

    unsigned long timeout = millis();
    // wait for data to be available
    while (client.available() == 0)
    {
        if (millis() - timeout > 35000)
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
        //  did you csll free()? 
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