#include <Arduino.h>
#include <HTTPClient.h>
// command line on pi $curl http://192.168.1.252/delete.php?key=
int rollBack(int key);

int rollBack(int key)
{
    int  httpResponseCode;
    String http_delete = "http://192.168.1.252/delete.php?key=";

    String idKey;
    WiFiClient client_sql;
    HTTPClient http;

    idKey = (String)key;
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
    return 1;

return 0;
}