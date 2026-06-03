#include "esp32-hal-rgb-led.h"

#include "statusManager.h"
#include "config.h"


void statusHandler(const u8_t statusCode) {
    switch (statusCode)
    {
    case 0:
        rgbLedWrite(LED_PIN, 50, 50, 50);
        break;
    case 1:
        rgbLedWrite(LED_PIN, 100, 100, 0);
        break;
    case 2:
        rgbLedWrite(LED_PIN, 40, 150, 0);
        break;
    case 3:
        rgbLedWrite(LED_PIN, 100, 0, 100);
        break;
    case 4:
        rgbLedWrite(LED_PIN, 30, 0, 0);
        break;
    case 255:
        rgbLedWrite(LED_PIN, 0, 255, 0);
        break;
    
    default:
        break;
    }
}