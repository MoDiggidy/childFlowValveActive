#ifndef MY_ESPNOW_H
#define MY_ESPNOW_H

#include <esp_now.h>

/// Declare message variables
bool closeValve = false;
int statusMonitor = 1;
int statusProperty = 1;

const int sendSimpleRetries = 5;
const int sendFullRetries = 10;
const int retriesDelayMs = 150;

/////////////////////////////
/// Board to board coms
////////////////////////////
uint8_t parentAddress[] = {0xDC, 0x54, 0x75, 0xC1, 0x5B, 0x90};

// === Structs ===
struct ParentPayload {
  int propertyStatus;
  int status;
  int sendData;
};

struct fullFlowPayload {
  float max10s_fl, max1m_fl, max10m_fl, total_fl;
  int max10s_T_M, max1m_T_M, max10m_T_M;
  int dayStamp, minutesStamp;
  int valveStatusDom, valveModeDom;
};

struct SimpleFlowPayload {
  float flow10s, flow30s;
  int valveStatus, runTime, valveMode, warning;
};

volatile bool sendSuccess = false;
volatile bool sendComplete = false;

// === Callback: Send Status ===
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  sendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  sendComplete = true;

  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2],
           mac_addr[3], mac_addr[4], mac_addr[5]);

  Serial.print("Send callback - to: ");
  Serial.print(macStr);
  Serial.print(" | status: ");
  Serial.println(sendSuccess ? "Success" : "Fail");
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
      // sendFullFlowData(...);
    }
  } else {
    Serial.println("Unknown data length received.");
  }
}

// === Send Full Data to Parent ===
void sendFullFlowData(
  float max10s_fl, float max1m_fl, float max10m_fl, float total_fl,
  int max10s_T_M, int max1m_T_M, int max10m_T_M,
  int dayStamp, int minutesStamp, int valveStatusDom, int valveModeDom
) {
  fullFlowPayload payload = {
    max10s_fl, max1m_fl, max10m_fl, total_fl,
    max10s_T_M, max1m_T_M, max10m_T_M,
    dayStamp, minutesStamp, valveStatusDom, valveModeDom
  };

  Serial.println("SENDING FULL FLOW DATA...");

  for (int attempt = 1; attempt <= sendFullRetries; attempt++) {
    sendComplete = false;
    sendSuccess = false;

    if (esp_now_send(parentAddress, (uint8_t*)&payload, sizeof(payload)) != ESP_OK) {
      Serial.printf("esp_now_send() failed to queue packet (attempt %d)\n", attempt);
      delay(retriesDelayMs);
      continue;
    }

    unsigned long tStart = millis();
    while (!sendComplete && (millis() - tStart < 200)) {
      delay(10);
    }

    if (sendSuccess) {
      Serial.printf("Send SUCCESS on attempt %d\n", attempt);
      return;
    } else {
      Serial.printf("Send FAILED on attempt %d\n", attempt);
    }

    delay(retriesDelayMs);
  }

  Serial.println("Failed to send full flow data after retries.");
}

// === Send Simple Data to Parent ===
void sendSimpleFlowData(float flow10s, float flow30s, int valveStatus, int runTime, int valveMode, int warning) {
  SimpleFlowPayload payload = {flow10s, flow30s, valveStatus, runTime, valveMode, warning};
  Serial.println("SENDING SIMPLE FLOW DATA...");

  for (int attempt = 1; attempt <= sendSimpleRetries; attempt++) {
    sendComplete = false;
    sendSuccess = false;

    if (esp_now_send(parentAddress, (uint8_t*)&payload, sizeof(payload)) != ESP_OK) {
      Serial.printf("esp_now_send() failed to queue packet (attempt %d)\n", attempt);
      delay(retriesDelayMs);
      continue;
    }

    unsigned long tStart = millis();
    while (!sendComplete && (millis() - tStart < 200)) {
      delay(10);
    }

    if (sendSuccess) {
      Serial.printf("Send SUCCESS on attempt %d\n", attempt);
      return;
    } else {
      Serial.printf("Send FAILED on attempt %d\n", attempt);
    }

    delay(retriesDelayMs);
  }

  Serial.println("Failed to send simple flow data after retries.");
}

// === ESP-NOW Setup ===
void initESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  Serial.println("ESP-NOW initialized");

  esp_now_register_send_cb(onDataSent);
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
