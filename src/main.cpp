#include <stdio.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <MD_MAX72xx.h>
#include <Arduino.h>
#include "time.h"

#define D1 5
#define D2 4
#define D3 0
#define D4 2
#define D5 14
#define D6 12
#define D7 13
#define D8 15

#define  DEBUG  1

#define DISPLAY_LAG 7

const char* ssid     = "no";
const char* password = "no";
const char* url      = "no";
// Expires July 2020
// 52:89:9D:90:86:66:6F:B5:27:DE:DC:01:06:81:64:B9:B1:53:54:ED
const uint8_t fingerprint[20] = {0x52, 0x89, 0x9d, 0x90, 0x86, 0x66, 0x6f, 0xb5, 0x27, 0xde, 0xdc, 0x01, 0x06, 0x81, 0x64, 0xb9, 0xb1, 0x53, 0x54, 0xed};
unsigned long lastUpdate = 0;
unsigned long staticUpdate = 0;
int arrivalTimes[2];
int arrivalCount = 2;

// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES	4

#define CLK_PIN   D5  // or SCK
#define DATA_PIN  D7  // or MOSI
#define CS_PIN    D8  // or SS

// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
// Arbitrary pins
//MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// We always wait a bit between updates of the display
#define  DELAYTIME  100  // in milliseconds

String displayDiff(int seconds) {
  int minutes = seconds/60;
  char* string;
  asprintf(&string, "%d:%02d", minutes, seconds % 60);
  return string;
}

void scrollText(const char *p)
{
  uint8_t charWidth;
  uint8_t cBuf[8];  // this should be ok for all built-in fonts

  mx.clear();

  while (*p != '\0')
  {
    charWidth = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);

    for (uint8_t i=0; i<=charWidth; i++)	// allow space between characters
    {
      mx.transform(MD_MAX72XX::TSL);
      if (i < charWidth)
        mx.setColumn(0, cBuf[i]);
      delay(DELAYTIME);
    }
  }

  // Finish scrolling halfway
  uint8_t screenWidth = MAX_DEVICES * COL_SIZE / 2;
  for (uint8_t i=0; i<=screenWidth; i++) {
    mx.transform(MD_MAX72XX::TSL);
    delay(DELAYTIME);
  }
}

/**
 * @param buf_c buf array length
 * @param buf Buffer array to store seconds till arrival
 * @returns Cound of arrival times returned
*/
int getArrivalTimes(int buf_c, int buf[2]) {
  // Avoid syncs by manualy decrementing
  if (lastUpdate > 0 && millis()-lastUpdate < 60000) {
    for (int i=0; i<buf_c; i++) {
      buf[i] = buf[i]-(millis()-staticUpdate)/1000;
    }
    staticUpdate = millis(); // TODO time drifts?
    return buf_c;
  }
  std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
  client->setFingerprint(fingerprint);
  HTTPClient https;
  int arrivalTimeCount = 0;

  if (https.begin(*client, url)) {  // HTTP
    Serial.println("[HTTP] GET...\n");
    // start connection and send HTTP header
    int httpCode = https.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);
      String response = https.getString();
      char* test = (char*)response.c_str();
      char *str; // Current token in the string being parsed
      int arrivalTimeArrIndex = 0;
      while ((str = strtok_r(test, ",", &test)) != NULL) {
        Serial.println(str);
        buf[arrivalTimeArrIndex++] = atoi(str) - DISPLAY_LAG;
        arrivalTimeCount++;
      }
      lastUpdate = millis();
      staticUpdate = millis();
      return arrivalTimeCount;
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
      return 0;
    }

    https.end();
  } else {
    Serial.printf("[HTTP} Unable to connect\n");
    return 0;
  }
}

void setup()
{
  mx.begin();
  Serial.begin(115200);
  delay(10);

  Serial.println("--------------------");
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA); //We don't want the ESP to act as an AP
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  arrivalCount = getArrivalTimes(arrivalCount, arrivalTimes);
  if (arrivalCount == 0) {
    delay(10 * 60000); // Wait 10 minutes to sync if trains aren't running
    return;
  }
  String text = "";
  for (int i=0; i<arrivalCount; i++) {
    text = text + displayDiff(arrivalTimes[i]) + " ";
  }
  scrollText(text.c_str());
}