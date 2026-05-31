#include "config.h"
#include <Preferences.h>

Preferences prefs;

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
const char* topic = "test";


// Stuff which will be puleld from NVS
String ssid;
String wifiPassword;
String username;
String password;


void getDeviceSpecificConfig() {
    prefs.begin("creds", true);

    ssid = prefs.getString("wifi_ssid", "readError");
    wifiPassword = prefs.getString("wifi_pass", "readError");
    username = prefs.getString("dev_id", "readError");
    password = prefs.getString("mqtt_pass", "readError");

    prefs.end();


    


    if(ssid.compareTo("readError") == 0 || wifiPassword.compareTo("readError") == 0 || username.compareTo("readError") == 0 || password.compareTo("readError") == 0) {
        Serial.println("Could not, get the appropriate read value from NVS\nRecheck provisioned device specific config\nRebooting...");
        delay(100);
        ESP.restart();

    }

}