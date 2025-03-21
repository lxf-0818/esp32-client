#include <Arduino.h>
#include <HTTPClient.h>

// command line on pi $curl http://192.168.1.252/delete.php?key=
int deleteRow(String phpScript);

int deleteRow(String phpScript)
{
    WiFiClient client_sql;
    HTTPClient http;
    http.begin(phpScript.c_str());
    int httpResponseCode = http.GET();
    if (httpResponseCode != 200)
    {
        Serial.print("Error code: ");
        Serial.println(httpResponseCode);
        http.end();
        return -1;
    }
    http.end();
    return 1;

}



