#ifndef MY_WIFICOMS_H
#define MY_WIFICOMS_H

#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include "time.h"
#include "mySecrets.h"


/////////////////////////////
///neopixel
////////////////////////////
#define NEOPIXEL_PIN PIN_NEOPIXEL
#define NEOPIXEL_COUNT 1
Adafruit_NeoPixel pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

void showPixelColor(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

void startNeoPixel() {
  pixel.begin();
  pixel.setBrightness(50);  // Optional brightness limit
  showPixelColor(255, 0, 0); // Start with red
}




/////////////////////////////
///WIFI
////////////////////////////

// === WiFi Connection ===
void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SECRET_SSID, SECRET_PASS);

  Serial.println("Connecting to WiFi...");
  for (int i = 0; i < 4; i++) {
    if (WiFi.waitForConnectResult() == WL_CONNECTED) break;
    delay(1000);
    Serial.println("Retrying...");
  }

  if (WiFi.status() == WL_CONNECTED) {
    
    showPixelColor(0, 255, 0);  // Turn green on success
    Serial.println("WiFi Connected!");
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
    Serial.print("MAC Address: "); Serial.println(WiFi.macAddress());
    Serial.print("WiFi Channel: "); Serial.println(WiFi.channel());    
    Serial.print("WiFi Signal Strength:");Serial.println(WiFi.RSSI()); 
  } else {
    Serial.println("Failed to connect to WiFi.");
  }
}


///////////////////
///Time
///////////

// NTP Server and POSIX TZ string for US Pacific Time (handles DST)
const char* ntpServer = "pool.ntp.org";
// PST8PDT: Pacific Standard Time with DST starting 2nd Sunday in March, ending 1st Sunday in November
const char* timeZone = "PST8PDT,M3.2.0/2,M11.1.0/2";


void initTime(unsigned long timeoutMillis = 10000) {
  configTzTime(timeZone, ntpServer);
  Serial.println("Waiting for NTP time sync...");

  struct tm timeinfo;
  unsigned long startAttempt = millis();

  while (!getLocalTime(&timeinfo)) {
    if (millis() - startAttempt > timeoutMillis) {
      Serial.println("Failed to sync time: timeout.");
      return;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nTime synchronized!");
}

void updateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  }
}

int getMonth() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) return timeinfo.tm_mon + 1;
  return -1;
}

int getDay() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) return timeinfo.tm_mday;
  return -1;
}

int getHour() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) return timeinfo.tm_hour;
  return -1;
}

int getMinute() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) return timeinfo.tm_min;
  return -1;
}

int getSecond() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) return timeinfo.tm_sec;
  return -1;
}
int getYear() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) return timeinfo.tm_year + 1900;
  return -1;
}

String getDateTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char buffer[25];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
  }
  return "Invalid";
}

String getDate() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char buffer[15];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
    return String(buffer);
  }
  return "Invalid";
}

String getTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char buffer[10];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
    return String(buffer);
  }
  return "Invalid";
}

int getMinutesToday() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    return timeinfo.tm_hour * 60 + timeinfo.tm_min;
  }
  return -1; // error
}

String getDateTimeMin() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char buffer[20];  // "YYYY-MM-DDTHH:MM" + null = 17 + 1
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M", &timeinfo);
    return String(buffer);
  }
  return "Invalid";
}


#endif