#ifndef MY_WIFICOMS_H
#define MY_WIFICOMS_H

#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_NeoPixel.h>
#include "mySecrets.h"



#define WIFI_SSID "iceshots24"
#define WIFI_PASS "987654321"

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




/////////////////////////////
///board to board coms
////////////////////////////




#endif