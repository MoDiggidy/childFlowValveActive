
#include <wifiComs.h>
#include "flowMon.h"
#include "espMqtt.h"

// #include "espNow.h"

/// Declare message variables
int statusProperty = 2;

////////////
// Timer Setup
////////////
unsigned long timerCheckMs;        // establish variable to check timer loop
unsigned long timerTimeMs = 10000; // set timer loop  to 15 seconds
#define WDT_TIMEOUT 300            //  watchdog loop timer seconds

void setup() {
  Serial.begin(115200);
  delay(3000);
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  startNeoPixel();
  flowMeterSetup();
  valveRelaySetup();

  connectToWiFi();                 // events will trigger NTP + MQTT
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setKeepAlive(CUSTOM_MQTT_KEEPALIVE);
  // connectToMQTT();              // <-- remove; event will handle it
  // initTime();                   // <-- remove; event will handle it
}


void loop()
{
  esp_task_wdt_reset();

  // check buttons
  checkButtonMode();
  checkButtonValve();

  if (mqttClient.connected()) mqttClient.loop();
  processWarningAckTick();

  flowCalcs(); // run flow check

  // mqtt failed send resends
  if (millis() - lastRetryTime > RETRY_INTERVAL)
  {
    retryUnsentPayloads();
    lastRetryTime = millis();
  }

  /// basic timer
  ///////////////
  if (millis() - timerCheckMs > timerTimeMs)
  {
    timerCheckMs = millis();
    reconnectIfNeeded();  //reconnect mqtt if needed
  }
}
