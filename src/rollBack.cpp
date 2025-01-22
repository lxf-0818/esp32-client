#include <Arduino.h>
#include <HTTPClient.h>
int rollBack(int key);

int rollBack(int key)
{
    int rows = 0, httpResponseCode;
    String http_getID = "http://192.168.1.252/getID.php?key=" + String(key);
    String http_tmp = "http://192.168.1.252/delete.php?key=";

    String http_delete;
    String idKey;
    WiFiClient client_sql;
    HTTPClient http;
    http.begin(http_getID.c_str());
    httpResponseCode = http.GET();
    if (httpResponseCode != 200)
    {
        Serial.print("Error code: ");
        Serial.println(httpResponseCode);
        http.end();
        return -1;
    }
    String payload = http.getString();
    http.end();
    Serial.println(payload);
    char *token = strtok((char *)payload.c_str(), "|"); // 1st line is # of rows from query
    rows = atoi(token);
    if (rows)
    {
        while (token != NULL)
        {
            token = strtok(NULL, "|");
            break; // only need to "callback" last row
        }
        idKey = token;
        http_delete = http_tmp;
        idKey.replace("\n", "");
        http_delete.concat(idKey);
        Serial.println(http_delete);
        http.begin(http_delete.c_str());
        httpResponseCode = http.GET();
        if (httpResponseCode != 200)
        {
            Serial.print("Error code: ");
            Serial.println(httpResponseCode);
            http.end();
            return -1;
        }
        http.end();
        return rows;
    }
    return 0;
}