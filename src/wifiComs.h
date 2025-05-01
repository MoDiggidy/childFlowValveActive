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
uint8_t parentAddress[] = {0xDC, 0x54, 0x75, 0xC1, 0x5B, 0x90};

// === Structs ===
struct ParentPayload {
  int propertyStatus;
  int status;
  int sendData;
};

struct ChildPayload {

  float max10s_fl;
  float max1m_fl;
  float max10m_fl;
  float total_fl;
  char max10s_T[6];
  char max1m_T[6];
  char max10m_T[6];
  char total_T[8];
  int valveStatusDom;
  int valveModeDom;
};

ChildPayload localData = {
  1.11,
  1.11,
  1.11,
  1.11,
  "11:11",
  "11:11",
  "11:11",
  "11:11:1",
  1,
  1
};  ///preliminary declaration


void updateLocalData(
  float max10s, float max1m, float max10m, float total,
  const char* t10s, const char* t1m, const char* t10m, const char* tTotal,
  int valveStatus, int valveMode
) {
  localData.max10s_fl = max10s;
  localData.max1m_fl = max1m;
  localData.max10m_fl = max10m;
  localData.total_fl = total;

  strncpy(localData.max10s_T, t10s, sizeof(localData.max10s_T));
  strncpy(localData.max1m_T, t1m, sizeof(localData.max1m_T));
  strncpy(localData.max10m_T, t10m, sizeof(localData.max10m_T));
  strncpy(localData.total_T,  tTotal, sizeof(localData.total_T));

  localData.valveStatusDom = valveStatus;
  localData.valveModeDom = valveMode;
}

// === Send Data to Parent ===
void sendChildData(ChildPayload data) {
  Serial.println("SENDING DATA...");
  esp_err_t result = esp_now_send(parentAddress, (uint8_t*)&data, sizeof(data));

  if (result == ESP_OK) {
    Serial.println("Data sent to parent successfully");
  } else {
    Serial.printf("Error sending data: %d\n", result);
  }
}

// === Callback: Receive Parent Data ===
void onDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  ParentPayload received;
  if (len == sizeof(received)) {
    memcpy(&received, incomingData, sizeof(received));
    Serial.printf("Received from parent: propertyStatus=%d, status=%d, sendData=%d\n",
                  received.propertyStatus, received.status, received.sendData);

    if (received.sendData == 1) {
      Serial.println("SENDING DATA");
      sendChildData(localData); // Send current values back
    }
  } else {
    Serial.println("Unknown data length received.");
  }
}

// === ESP-NOW Setup ===
void initESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  Serial.println("ESP-NOW initialized");

  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, parentAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (!esp_now_is_peer_exist(parentAddress)) {
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      Serial.println("Parent peer added");
    } else {
      Serial.println("Failed to add parent peer");
    }
  }
}






#endif