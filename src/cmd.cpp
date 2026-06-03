#include "config.h"
#include "esp32-hal-rgb-led.h"

// DEPRECIATED

// Note to people, for somereason the it is G R B instead of RGB so be careful

void cmd(char *payload) {
  if(strcmp(payload, "reboot") == 0) {
    ESP.restart();
  }
  if(strcmp(payload, "red") == 0) {
    rgbLedWrite(LED_PIN, 0, 255, 0);
  }
  if(strcmp(payload, "blue") == 0) {
    rgbLedWrite(LED_PIN, 0, 0, 255);
  }
  if(strcmp(payload, "green") == 0) {
    rgbLedWrite(LED_PIN, 255, 0, 0);
  }
  if(strcmp(payload, "white") == 0) {
    rgbLedWrite(LED_PIN, 255, 255, 255);
  } 
  if(strcmp(payload, "off") == 0) {
    rgbLedWrite(LED_PIN, 0, 0, 0);
  } 
  if(strcmp(payload, "rp") == 0) {
    digitalWrite(RED_PIN, HIGH);
  }
  if(strcmp(payload, "bp") == 0) {
    digitalWrite(BLUE_PIN, HIGH);
  }
  if(strcmp(payload, "gr") == 0) {
    digitalWrite(RED_PIN, HIGH);
    digitalWrite(BLUE_PIN, HIGH);
  }
  if(strcmp(payload, "off_pin") == 0) {
    digitalWrite(BLUE_PIN, LOW);
    digitalWrite(RED_PIN, LOW);
  }
  if(strcmp(payload, "on_pin") == 0) {
    digitalWrite(BLUE_PIN, HIGH);
    digitalWrite(RED_PIN, HIGH);
  }
}