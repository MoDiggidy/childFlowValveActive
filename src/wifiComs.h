#ifndef MY_WIFICOMS_H
#define MY_WIFICOMS_H

#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include "time.h"
#include "mySecrets.h"




// ======================
// === Forward Decl ====
// ======================
void initTime(unsigned long timeoutMillis = 10000);
void reconnectIfNeeded(); // from espMqtt

// ======================
// === Global Decl ====
// ======================
bool isNtpTimeConnected = false;


// ==========================
// === Input Settings ====
// ==========================

//neopixels
#define NEOPIXELEX_PIN     7   // External pixel strip
#define NEOPIXEL_COUNT     3   //how many neopixels in sequence   

//wifi
const unsigned long wifiReconnectIntervalMS = 60000; // how long to wait until trying to reconnect wifi
const unsigned long wifiConnectTimeoutMS = 10000;     // how lon to wait for wifi to connect

//NTP time server 
const char* ntpServer = "pool.ntp.org";
const char* timeZone  = "PST8PDT,M3.2.0/2,M11.1.0/2"; // Pacific w/ DST


// ==========================
// === NeoPixel Settings ====
// ==========================
#define NEOPIXEL_PIN       PIN_NEOPIXEL      // Onboard pixel

Adafruit_NeoPixel pixelOnboard(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip(NEOPIXEL_COUNT, NEOPIXELEX_PIN, NEO_GRB + NEO_KHZ800);

void showPixelColorOnboard(uint8_t r, uint8_t g, uint8_t b) {
  pixelOnboard.setPixelColor(0, pixelOnboard.Color(r, g, b));
  pixelOnboard.show();
}

void showPixelColorEx(int ledNum, uint8_t r, uint8_t g, uint8_t b) {
  strip.setPixelColor(ledNum, strip.Color(g, r, b)); // Custom color order
  strip.show();
}

void startNeoPixel() {
  Serial.print("Starting NeoPixels...");
  pixelOnboard.begin();
  pixelOnboard.setBrightness(75);
  strip.begin();
  strip.setBrightness(75);

  showPixelColorOnboard(255, 0, 0); // Onboard red start
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    showPixelColorEx(i, 255, 0, 0); // External red start
  }
  Serial.println("Done");
}

// ======================
// === WiFi Settings ====
// ======================
unsigned long wifiCheckMs = 0;

bool isWifiConnected()   { return WiFi.status() == WL_CONNECTED; }
bool isWifiDisconnected(){ return !isWifiConnected(); }

// ==========================
// === WiFi Connection ======
// ==========================
void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SECRET_SSID, SECRET_PASS);

  Serial.println("Connecting to WiFi...");
  unsigned long startAttempt = millis();
  while (!isWifiConnected() && millis() - startAttempt < wifiConnectTimeoutMS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (isWifiConnected()) {
    showPixelColorOnboard(0, 255, 0);   // Green
    showPixelColorEx(2, 0, 255, 0);     // Green
    Serial.println("WiFi Connected!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    Serial.print("MAC: "); Serial.println(WiFi.macAddress());
    Serial.print("Channel: "); Serial.println(WiFi.channel());
    Serial.print("RSSI: "); Serial.println(WiFi.RSSI());
  } else {
    Serial.println("WiFi connection failed.");
    showPixelColorOnboard(255, 0, 0);   // Red
    showPixelColorEx(2, 255, 0, 0);
  }
}

void checkWiFiReconnect() {
  if (isWifiConnected()) return;

  if (millis() - wifiCheckMs < wifiReconnectIntervalMS) {
    Serial.println("WiFi not connected. Waiting...");
    isNtpTimeConnected = false;
    return;
  }

  wifiCheckMs = millis();
  Serial.println("WiFi disconnected! Attempting to reconnect...");
  showPixelColorOnboard(255, 255, 0);  // Yellow
  showPixelColorEx(2, 255, 255, 0);  // Yellow

  WiFi.disconnect(true);
  delay(1000);
  WiFi.begin(SECRET_SSID, SECRET_PASS);

  unsigned long startAttempt = millis();
  while (!isWifiConnected() && millis() - startAttempt < wifiConnectTimeoutMS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (isWifiConnected()) {
    Serial.println("WiFi reconnected.");
    showPixelColorOnboard(0, 255, 0);   // Green
    showPixelColorEx(2, 0, 255, 0);
    initTime();                         // Sync time again
    reconnectIfNeeded();               // Reconnect MQTT
  } else {
    Serial.println("Reconnection failed.");
    isNtpTimeConnected = false;
    showPixelColorOnboard(255, 0, 0);   // Red
    showPixelColorEx(2, 255, 0, 0);
  }
}

// =========================
// === Time Management =====
// =========================


void initTime(unsigned long timeoutMillis) {
  if (!isWifiConnected()) {
    Serial.println("NTP skipped: No WiFi.");
    isNtpTimeConnected = false;
    return;
  }

  configTzTime(timeZone, ntpServer);
  Serial.println("Waiting for NTP sync...");

  struct tm timeinfo;
  unsigned long start = millis();

  while (!getLocalTime(&timeinfo)) {
    if (millis() - start > timeoutMillis) {
      Serial.println("NTP sync failed: timeout.");
      isNtpTimeConnected = false;
      return;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nTime synchronized!");
  isNtpTimeConnected = true;
}

void updateTime() {
  if (!isWifiConnected() || !isNtpTimeConnected) {
    Serial.println("Time update skipped: No WiFi or NTP.");
    isNtpTimeConnected = false;
    return;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time.");
    isNtpTimeConnected = false;
  }
}

// ==============================
// === Time Utility Functions ===
// ==============================

int getTimeInt(String key) {
  if (!isWifiConnected() || !isNtpTimeConnected) return -1;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return -1;

  if (key == "Year") return timeinfo.tm_year + 1900;
  if (key == "Month") return timeinfo.tm_mon + 1;
  if (key == "Day") return timeinfo.tm_mday;
  if (key == "Hour") return timeinfo.tm_hour;
  if (key == "Minute") return timeinfo.tm_min;
  if (key == "Second") return timeinfo.tm_sec;
  if (key == "MinutesToday") return timeinfo.tm_hour * 60 + timeinfo.tm_min;

  return -1;
}

String getTimeString(String key) {
  if (!isWifiConnected() || !isNtpTimeConnected) return "0000-00-00 00:00:00";
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "0000-00-00 00:00:00";

  char buffer[26];
  if (key == "DateTime")     strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  else if (key == "Date")    strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
  else if (key == "Time")    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
  else if (key == "DateTimeMin") strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M", &timeinfo);
  else return "0000-00-00 00:00:00";

  return String(buffer);
}


#endif
