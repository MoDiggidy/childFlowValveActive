
#include "wifiComs.h"
#include "espNow.h"
#include "flowMon.h"

////////////
// Timer Setup
////////////
unsigned long timerCheckMs;        // establish variable to check timer loop
unsigned long timerTimeMs = 60000; // set timer loop  to 15 seconds

void setup()
{
  Serial.begin(115200);
  delay(3000);
  connectToWiFi();
  initESPNow();
  startNeoPixel();

  flowMeterSetup();
  // Initialize NTP client and force sync
  initTime();
}

void loop()
{
  checkflow(); // run flow check
  /// basic timer
  ///////////////
  if (millis() - timerCheckMs > timerTimeMs)
  {
    timerCheckMs = millis();
    Serial.print("Month: ");
    Serial.println(getMonth());
    Serial.print("Day: ");
    Serial.println(getDay());
    Serial.print("Hour: ");
    Serial.println(getHour());
    Serial.print("Minute: ");
    Serial.println(getMinute());
    Serial.print("Second: ");
    Serial.println(getSecond());

  }
}
