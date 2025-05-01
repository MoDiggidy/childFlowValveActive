
#include "wifiComs.h"


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
  1.56,
  24.55,
  0,
  11,
  "12:24",
  "01:13",
  "08:05",
  "04:45:1",
  1,
  2
};

// === Function Declarations ===
void initESPNow();
void onDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len);
void sendChildData(ChildPayload data);



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




void setup() {
  Serial.begin(115200);
  delay(3000);
  connectToWiFi();
  initESPNow();
  startNeoPixel();
}

void loop() {
  // Nothing in loop – all handled in callback
  delay(1000);
  Serial.println("Unknown data length received.");
}
