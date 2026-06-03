#include "config.h"
#include <Preferences.h>

#include "statusManager/statusManager.h"

const char* ca_cert = "-----BEGIN CERTIFICATE-----\n"
"MIIDVTCCAj2gAwIBAgIUPvVD1r0tzEn/Ha5rRQ3uNTuDAlwwDQYJKoZIhvcNAQEL\n"
"BQAwOjELMAkGA1UEBhMCTlAxETAPBgNVBAoMCEdyaWRGbG93MRgwFgYDVQQDDA9H\n"
"cmlkRmxvdy1Sb290Q0EwHhcNMjYwNTI5MDkwMzI1WhcNMzYwNTI2MDkwMzI1WjA6\n"
"MQswCQYDVQQGEwJOUDERMA8GA1UECgwIR3JpZEZsb3cxGDAWBgNVBAMMD0dyaWRG\n"
"bG93LVJvb3RDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMSW8rK3\n"
"bLUpAAfHGm5/WKfLMmQVKge3AKwYpIIjfWghYWOAxRGeskLkyqALX//hhO+fVM3N\n"
"1Z0FrX5WO1tXfJxNdtElgHRBiI0CiTt1FIg8wu6nSstqvb3Pb2pnw9nzzAvbj5b0\n"
"jHGuM80J0U9CaiUhSTZDDo3DQONP8LGL4EMFZ6Bu8RXsxONwYde1UajkBEn6nNTV\n"
"I/GPEvB5aewbEqCx1O9YejVJ/W0BwpuLnZj6No9750Q1xtpnNUIsr+aMn616u7Q9\n"
"ic9MdLs+xWeLZRR+uVPR8DDWXqOlyD0cVX9uj0FyKXhVTAAwR7Aft2cNu8EBkA1Q\n"
"iOj0F/UkyjV8XPsCAwEAAaNTMFEwHQYDVR0OBBYEFNzpGTOraFF8A5mGEV7L+n6a\n"
"mWOeMB8GA1UdIwQYMBaAFNzpGTOraFF8A5mGEV7L+n6amWOeMA8GA1UdEwEB/wQF\n"
"MAMBAf8wDQYJKoZIhvcNAQELBQADggEBABvIRFgdXYsehNDyiGTF/ScocXw0D6U3\n"
"VRHRW4HPDsvvJ1QYkFFrWnOOY1XdqiLWZ7VGsgbiGvqcSihKfTsNfXQm9eowRelW\n"
"o7NcsDDF2HaTOsF4fcp6e3GG6TtETSpupy+iota2itj1j55yKrrLNDudCkJbZ+CW\n"
"dKFtaxcDKEzO+iKI1Au9gErHlyQyn/zaG9n7VPTjY3VzeQxODS6n6Y3yUUF/EXA0\n"
"WSR6AjJeDsIcOkABxAq7HVRyRFBgzsWSDXWq8vbslXQ//TEYLwqzNFIRikeFUcPn\n"
"79T/48boJiApln967TXfcwQF74mvRSPlqsZTa/FxtCHRBx+n5drYF5M=\n"
"-----END CERTIFICATE-----\n";

// unsigned int globalErrorCount = 0;

const char* brokerUri = "mqtts://emqx.internal.grdflo.com:8883";
const char* testTopic = "test";


// Stuff which will be puleld from NVS
String ssid;
String wifiPassword;
unsigned int globalErrorCounter = 0;
String username;
String password;
u8_t pinOffset;
u8_t totalPins;
int *chargerPin, *lightPin;
int *relayState;


void getDeviceSpecificConfig() {
    Preferences prefs;
    prefs.begin("creds", true);

    ssid = prefs.getString("wifi_ssid", "readError");
    wifiPassword = prefs.getString("wifi_pass", "readError");
    username = prefs.getString("dev_id", "readError");
    password = prefs.getString("mqtt_pass", "readError");

    prefs.end();

    // due to  the  required needs the offset will be from 0 to 15 only
    // note to readers, i did this cause, if pin offset is 4, that means first  4 gpio will be used for charger, and rest 12 will be used for light, if it is 10, then 10 pins charger, last 6 lights, and so on.
    prefs.begin("pinDistribution", true);
    pinOffset = prefs.getUChar("offset", 255);
    totalPins = prefs.getUChar("totalPin", 255);
    prefs.end();

    if(ssid.compareTo("readError") == 0 || wifiPassword.compareTo("readError") == 0 || username.compareTo("readError") == 0 || password.compareTo("readError") == 0 || totalPins > 16 || pinOffset > totalPins) {
        statusHandler(STATE_ERROR);
        Serial.println("Could not, get the appropriate read value from NVS\nRecheck provisioned device specific config\nRebooting...");
        delay(5000);
        ESP.restart();

    }

    chargerPin = new int[pinOffset];
    lightPin = new int[totalPins - pinOffset];
    relayState = new int[totalPins]();   // value-initialised → all zero at boot

    int counter;
    int i = 0;

    prefs.begin("pinMapping", true);

    char tmp[2] = {0};
    for(counter = 65; counter < (65+pinOffset); counter++) {
        tmp[0] = (char)counter;
        tmp[1] = '\0';
        chargerPin[i] = prefs.getUChar(tmp, 255);

        if(chargerPin[i] == 255) {
            statusHandler(STATE_ERROR);
            Serial.println("Could not, get the appropriate read value from NVS\nRecheck provisioned device specific config\nRebooting...");
            delay(5000);
            ESP.restart();
        }

        pinMode(chargerPin[i], OUTPUT);
        digitalWrite(chargerPin[i], HIGH);

        i++;
    }
    i=0;

    for(counter; counter < (65 + totalPins); counter++) {
        tmp[0] = char(counter);
        tmp[1] = '\0';
        lightPin[i] = prefs.getUChar(tmp, 255);

        if(lightPin[i] == 255) {
            statusHandler(STATE_ERROR);
            Serial.println("Could not, get the appropriate read value from NVS\nRecheck provisioned device specific config\nRebooting...");
            delay(5000);
            ESP.restart();
        }

        pinMode(lightPin[i], OUTPUT);

        i++;
    }

    prefs.end();
}