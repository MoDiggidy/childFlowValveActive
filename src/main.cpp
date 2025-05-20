
#include <wifiComs.h>
#include "flowMon.h"
#include "espMqtt.h"

//#include "espNow.h"

/// Declare message variables
bool valveClosed = false;
int statusMonitor = 1;  // 0 manual : 1 Home : 2 Away
int statusProperty = 1;

////////////
// Timer Setup
////////////
unsigned long timerCheckMs;        // establish variable to check timer loop
unsigned long timerTimeMs = 30000; // set timer loop  to 15 seconds
#define WDT_TIMEOUT 300       //  watchdog loop timer seconds

void setup()
{
  Serial.begin(115200);
  delay(3000);
  // Start watchdog
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  
  flowMeterSetup();

  startNeoPixel();
  connectToWiFi();
  //initESPNow();

  // Initialize NTP client and force sync
  initTime();
  
  ///mqtt setup
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setKeepAlive(CUSTOM_MQTT_KEEPALIVE);
  connectToMQTT();

  valveRelaySetup();

}

void loop()
{
  esp_task_wdt_reset();
  mqttClient.loop();
  flowCalcs(); // run flow check
  
  //mqtt failed send resends
  if (millis() - lastRetryTime > RETRY_INTERVAL) {
    retryUnsentPayloads();
    lastRetryTime = millis();
  }
  
  /// basic timer
  ///////////////
  if (millis() - timerCheckMs > timerTimeMs)
  {
    timerCheckMs = millis();

  connectToMQTT();
  }
}
