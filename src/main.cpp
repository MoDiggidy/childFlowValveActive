
#include "wifiComs.h"



void setup() {
  Serial.begin(115200);
  delay(3000);
  connectToWiFi();
  initESPNow();
  startNeoPixel();
}

void loop() {
  // Nothing in loop – all handled in callback
  // Update global localData fields directly
  updateLocalData(
    1.22, 24.55, 0, 22,
    "12:24", "01:13", "22:05", "04:45:1",
    1, 3
  );

  delay(100);
}
