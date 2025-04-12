#include <Arduino.h>
#include <HTTPClient.h>

// command line on pi $curl http://192.168.1.252/delete.php?key=
//                    $curl http://192.168.1.252/deleteMAC.php?key=
int deleteRow(String phpScript);
String performHttpGet(const char *url);


int deleteRow(String phpScript)
{
   String payroll = performHttpGet(phpScript.c_str());
       
    return 1;

}



