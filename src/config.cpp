#include "config.h"
#include <Preferences.h>

#include "statusManager/statusManager.h"
#include "provision.h"

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
    String _ssid    = prefs.getString("wifi_ssid", "readError");
    String _wifiPwd = prefs.getString("wifi_pass", "readError");
    String _devId   = prefs.getString("dev_id",    "readError");
    String _mqttPwd = prefs.getString("mqtt_pass", "readError");
    prefs.end();

    prefs.begin("pinDistribution", true);
    uint8_t _offset = prefs.getUChar("offset",   255);
    uint8_t _total  = prefs.getUChar("totalPin", 255);
    prefs.end();

    uint8_t _pinMap[16];
    prefs.begin("pinMapping", true);
    char k[2] = {0};
    for (int i = 0; i < 16; i++) {
        k[0] = 'A' + i;
        _pinMap[i] = prefs.getUChar(k, 255);
    }
    prefs.end();

    bool pinMapOk = (_total >= 1 && _total <= 16);
    for (int i = 0; pinMapOk && i < _total; i++) {
        if (_pinMap[i] == 255) pinMapOk = false;
    }
    const bool nvsOk =
        _ssid    != "readError" &&
        _wifiPwd != "readError" &&
        _devId   != "readError" &&
        _mqttPwd != "readError" &&
        _offset <= _total &&
        pinMapOk;

    if (!nvsOk) {
        provision();
    }

    ssid         = _ssid;
    wifiPassword = _wifiPwd;
    username     = _devId;
    password     = _mqttPwd;
    pinOffset    = _offset;
    totalPins    = _total;

    chargerPin = new int[pinOffset];
    lightPin   = new int[totalPins - pinOffset];
    relayState = new int[totalPins]();   // value-initialised → all zero at boot

    for (int i = 0; i < pinOffset; i++) {
        chargerPin[i] = _pinMap[i];
        pinMode(chargerPin[i], OUTPUT);
        digitalWrite(chargerPin[i], HIGH);
        relayState[i] = 1;
    }

    for (int i = pinOffset, j = 0; i < totalPins; i++, j++) {
        lightPin[j] = _pinMap[i];
        pinMode(lightPin[j], OUTPUT);
        digitalWrite(lightPin[j], LOW);
    }
}