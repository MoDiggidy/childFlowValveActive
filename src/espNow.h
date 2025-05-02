#ifndef MY_ESPNOW_H
#define MY_ESPNOW_H

#include <esp_now.h>


///  declare message variables
bool closeValve = false;                      // false:open true:Closed
int statusMonitor = 1;                        // 0:Manual 1:home 2:Away
int statusProperty = 1;                       // 1:home 2:Away

const int sendSimpleRetries = 5;
const int sendFullRetries = 10;

const int retriesDelayMs = 150;

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

struct fullFlowPayload {

  float max10s_fl;
  float max1m_fl;
  float max10m_fl;
  float total_fl;
  int max10s_T_M;
  int max1m_T_M;
  int max10m_T_M;
  int dayStamp;
  int minutesStamp;
  int valveStatusDom;
  int valveModeDom;
};

struct SimpleFlowPayload {
  float flow10s;
  float flow30s;
  int valveStatus;
  int runTime;
  int valveMode;
  int warning;
};




// === Send Full Data to Parent ===
void sendFullFlowData(
  float max10s_fl, float max1m_fl, float max10m_fl, float total_fl,
  int max10s_T_M, int max1m_T_M, int max10m_T_M,
  int dayStamp, int minutesStamp,
  int valveStatusDom, int valveModeDom
) {
  fullFlowPayload payload;
  payload.max10s_fl = max10s_fl;
  payload.max1m_fl = max1m_fl;
  payload.max10m_fl = max10m_fl;
  payload.total_fl = total_fl;

  payload.max10s_T_M = max10s_T_M;
  payload.max1m_T_M = max1m_T_M;
  payload.max10m_T_M = max10m_T_M;

  payload.dayStamp = dayStamp;
  payload.minutesStamp = minutesStamp;
  payload.valveStatusDom = valveStatusDom;
  payload.valveModeDom = valveModeDom;

  int attempts = 0;
  esp_err_t result;

  Serial.println("SENDING FULL FLOW DATA...");
  Serial.printf("max10s_fl:     %.2f\n", payload.max10s_fl);
  Serial.printf("max1m_fl:      %.2f\n", payload.max1m_fl);
  Serial.printf("max10m_fl:     %.2f\n", payload.max10m_fl);
  Serial.printf("total_fl:      %.2f\n", payload.total_fl);
  
  Serial.printf("max10s_T_M:    %d\n", payload.max10s_T_M);
  Serial.printf("max1m_T_M:     %d\n", payload.max1m_T_M);
  Serial.printf("max10m_T_M:    %d\n", payload.max10m_T_M);
  
  Serial.printf("dayStamp:      %d\n", payload.dayStamp);
  Serial.printf("minutesStamp:  %d\n", payload.minutesStamp);
  Serial.printf("valveStatusDom:%d\n", payload.valveStatusDom);
  Serial.printf("valveModeDom:  %d\n", payload.valveModeDom);
  Serial.println("==============================");
  do {
    result = esp_now_send(parentAddress, (uint8_t*)&payload, sizeof(payload));
    if (result == ESP_OK) {
      Serial.println("Full flow data sent successfully");
      break;
    } else {
      Serial.printf("Send attempt %d failed: %d\n", attempts + 1, result);
      delay(retriesDelayMs);  // brief pause before retry
    }
    attempts++;
  } while (result != ESP_OK && attempts < sendFullRetries);

  if (result != ESP_OK) {
    Serial.println("Failed to send simple flow data after 5 attempts.");
  }
}

// === Send Simple Data to Parent ===
void sendSimpleFlowData(float flow10s, float flow30s, int valveStatus, int runTime, int valveMode, int warning) {
  SimpleFlowPayload payload;
  payload.flow10s = flow10s;
  payload.flow30s = flow30s;
  payload.valveStatus = valveStatus;
  payload.runTime = runTime;
  payload.valveMode = valveMode;
  payload.warning = warning;

  int attempts = 0;
  esp_err_t result;

  Serial.println("SENDING SIMPLE FLOW DATA...");
  Serial.printf("flow10s:      %.2f\n", payload.flow10s);
  Serial.printf("flow30s:      %.2f\n", payload.flow30s);
  Serial.printf("valveStatus:  %d\n", payload.valveStatus);
  Serial.printf("runTime:      %d\n", payload.runTime);
  Serial.printf("valveMode:    %d\n", payload.valveMode);
  Serial.printf("warning:      %d\n", payload.warning);
  Serial.println("================================");

  do {
    result = esp_now_send(parentAddress, (uint8_t*)&payload, sizeof(payload));
    if (result == ESP_OK) {
      Serial.println("Simple flow data sent successfully");
      break;
    } else {
      Serial.printf("Send attempt %d failed: %d\n", attempts + 1, result);
      delay(retriesDelayMs);  // brief pause before retry
    }
    attempts++;
  } while (result != ESP_OK && attempts < sendSimpleRetries);

  if (result != ESP_OK) {
    Serial.println("Failed to send simple flow data after 5 attempts.");
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
      //sendFullFlowData(localData); // Send current values back
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