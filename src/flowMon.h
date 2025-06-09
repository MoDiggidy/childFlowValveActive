#ifndef MY_FLOWMON_H
#define MY_FLOWMON_H

#include <Preferences.h>
#include "espMqtt.h"

#define FLOW_SENSOR_PIN 25  // Replace with your flowmeter pin
#define VALVE_RELAY 4       // Valve Relay Pin
#define BUTTON_MODE_PIN 32  // Button for mode control (statusMonitor)
#define BUTTON_VALVE_PIN 33 // Button for valve toggle (valveClosed)

bool valveClosed = false;
int statusMonitor = 3;  // 0 manual : 1 Home : 2 Away

// === Configuration Constants ===
const unsigned int pulseDebounceMmS = 200000 ;  //debounce time for pulse meter pule count in microseconds
const int sendFlowTimeMs = 10000;        // rate at which simpleflow data gets sent
const int updateFlowTimeMs = 10000;      // cycle time to check pulse count on flowmeter
const float calibrationFactor = 10.0;    // pulses per gallon
const unsigned long waterRunMinSec = 15; // how long flow stops before updateing flow to stopped
const int waterRunMaxSec[3] = {0, 60, 30};
const int maxIntervals = 3600 / (updateFlowTimeMs / 1000);
const float galPerMinFactor = 60.0 / (updateFlowTimeMs / 1000);
#define VALVE_CYCLE_TIMEOUT 40000 // Max cycle time in ms (e.g. 10 seconds)
#define VALVE_CYCLE_DELAY 10000   // Time valve remains closed before reopening

// === pulse tracking ===

// === ButtonMode tracking ===
unsigned long buttonModePressStart = 0;
unsigned long buttonModeLastDebounceTime = 0;
bool buttonModeState = HIGH;
bool buttonModeLastReading = HIGH;
bool buttonModePreviouslyPressed = false;
// === ButtonValve tracking ===
unsigned long buttonValveLastDebounceTime = 0;
bool buttonValveState = HIGH;
bool buttonValveLastReading = HIGH;
bool buttonValvePreviouslyPressed = false;
const unsigned long debounceDelay = 50;
const unsigned long LONG_PRESS_DURATION = 10000;

// === Global Flow Variables ===
bool waterRun = false;
unsigned long waterRunDurSec = 0;
unsigned long lastWaterRunDurSec = 0;
int warningAlert = 0;  //flag to include mqtt warning
unsigned long waterStopDurSec = 0;
float flowSamples[maxIntervals] = {0};
String minuteStamps[maxIntervals] = {""};
String minuteStampsPrevious = "";
int sampleIndex = 0;
int flowAvgIndex = 0;
float flowAvg[6];
float flowAvgValue = 0;
float lastFlowAvgValue = 0;
float flow10s = 0;
float lastFlow10s = 0;
int lastValveClosed = 3;
int LastStatusMonitor = 4;
unsigned long timerUpdateCheckMs = 0;
unsigned long timerSendFlowCheckMs = 0;
volatile unsigned long pulseCount = 0;
volatile unsigned long lastPulseTime = 0;  //pulse timer for debounce
int oldHour = 0;
int oldDay = 0;
int oldMin = 0;

Preferences volumePrefs;
float volumeHour = 0.0;
float volumeMin = 0.0;
float volumeDay = 0.0;

bool volumeNeedsSave = false;
unsigned long lastVolumeSave = 0;
const unsigned long volumeSaveInterval = 60000; // Save every 60s if dirty

String oldTimeStamp = getTimeString("DateTimeMin");

// === Max Volume Tracking ===
float max1Min = 0, max10Sec = 0, max10Min = 0, max30Min = 0;
String max1MinTime = "", max10SecTime = "", max10MinTime = "", max30MinTime = "";


// declaire functions early
void saveVolumeToPrefs();
void loadVolumeFromPrefs();

// === ISR ===
void IRAM_ATTR pulseCounter()
{
    unsigned long now = micros();
    if (now - lastPulseTime > pulseDebounceMmS) { 
        pulseCount++;
         lastPulseTime = now;
    }
}

void flowMeterSetup()
{

    Serial.print("Initialising Flowmeter Monitoring...");
    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);

    // establish buttons
    pinMode(BUTTON_MODE_PIN, INPUT_PULLUP);
    pinMode(BUTTON_VALVE_PIN, INPUT_PULLUP);
    Serial.println("Done");

    //  Serial.printf("Old Times >> Day: %d  Hour: %d  Min: %d\n", oldDay, oldHour, oldMin);
    loadVolumeFromPrefs(); // load values if lost on reset
}

void valveRelaySetup()
{
    pinMode(VALVE_RELAY, OUTPUT); // Set the Valve Relay pin as output
}

void resetMaxValues()
{
    memset(flowSamples, 0, sizeof(flowSamples));
    for (int i = 0; i < maxIntervals; i++)
    {
        minuteStamps[i] = "";
    }

    sampleIndex = 0;
    max1Min = max10Sec = max10Min = max30Min = 0;
    max1MinTime = max10SecTime = max10MinTime = max30MinTime = "";
}

void updateMax(float *maxVol, String *maxTime, int numSamples, int sampleIndex)
{
    if (numSamples > sampleIndex + 1)
        return;

    float sum = 0;
    for (int i = sampleIndex + 1 - numSamples; i <= sampleIndex; i++)
    {
        sum += flowSamples[i];
    }

    float avg = sum / numSamples;
    if (avg > *maxVol)
    {
        *maxVol = avg;
        *maxTime = minuteStamps[sampleIndex - numSamples + 1];
    }
}

void updateVolumes(int sampleIndex)
{
    updateMax(&max1Min, &max1MinTime, 60 / (updateFlowTimeMs / 1000), sampleIndex);
    updateMax(&max10Sec, &max10SecTime, 10 / (updateFlowTimeMs / 1000), sampleIndex);
    updateMax(&max10Min, &max10MinTime, 600 / (updateFlowTimeMs / 1000), sampleIndex);
    updateMax(&max30Min, &max30MinTime, 1800 / (updateFlowTimeMs / 1000), sampleIndex);
}

void sendflow(String oldTimeStamp, int volumeSend)
{
    sendFlowData(
        max10Sec, max1Min, max10Min, volumeSend,
        max10SecTime, max1MinTime, max10MinTime,
        oldTimeStamp,
        valveClosed, statusMonitor);
}

// === FlowCalc Helpers ===
void calculateFlowStats(float volumeNowgal)
{
    flowSamples[sampleIndex] = flowAvg[flowAvgIndex] = volumeNowgal * galPerMinFactor;
    minuteStamps[sampleIndex] = minuteStampsPrevious;
    minuteStampsPrevious = getTimeString("DateTimeMin");

    updateVolumes(sampleIndex);
    sampleIndex = (sampleIndex + 1) % maxIntervals;

    flowAvgValue = 0;
    for (int i = 0; i < 3; i++)
        flowAvgValue += flowAvg[i];
    flowAvgValue /= 3;
    flowAvgIndex = (flowAvgIndex + 1) % 3;
}

void updateWaterState(long pulseCountNow, float volumeNowgal)
{
    long deltaSec = updateFlowTimeMs / 1000;

    if (pulseCountNow > 0)
    {
        waterRunDurSec += deltaSec;
        waterStopDurSec = 0;
        waterRun = true;
        volumeHour += volumeNowgal;
        volumeDay += volumeNowgal;
        volumeMin += volumeNowgal;
        volumeNeedsSave = true;
    }
    else
    {
        if (waterStopDurSec >= waterRunMinSec)
        {
            waterRunDurSec = 0;
            waterRun = false;
            waterStopDurSec += deltaSec;
        }
        else
        {
            waterRunDurSec += deltaSec;
            waterStopDurSec += deltaSec;
        }
    }
}

void closeValve()
{
    digitalWrite(VALVE_RELAY, HIGH); // Close Valve
    Serial.println("===========================");
    Serial.println("!!!!!!!!SHUT OFF WATER!!!!!");
    Serial.println("===========================");
    valveClosed = true;
    warningAlert = 1;

    showPixelColorEx(1, 255, 0, 0); // Start with red
}

void openValve()
{
    digitalWrite(VALVE_RELAY, LOW); // Open Valve
    Serial.println("===========================");
    Serial.println("Valve is now OPEN");
    Serial.println("===========================");
    valveClosed = false;
    waterRunDurSec = 0;
 

    showPixelColorEx(1, 0, 255, 0); // green
}

void cycleValve()
{
    unsigned long startTime = millis();

    Serial.println("Starting valve cycle...");

    closeValve();

    while (millis() - startTime < VALVE_CYCLE_DELAY)
    {
        delay(100); // Small delay to avoid watchdog triggering
        if (millis() - startTime > VALVE_CYCLE_TIMEOUT)
        {
            Serial.println("Cycle timeout before opening valve!");
            sendAck("cycle_valve", "timeout_before_open");
            return;
        }
    }

    openValve();

    if (millis() - startTime > VALVE_CYCLE_TIMEOUT)
    {
        Serial.println("Cycle timeout after reopening valve!");
        sendAck("cycle_valve", "timeout_after_open");
        return;
    }

    sendAck("cycle_valve", "completed");
}

void handleValveLogic()
{
    if (statusMonitor < 0 || statusMonitor > 2)
        statusMonitor = 1; // default to safe value Home

    if (!valveClosed && statusMonitor != 0 && waterRunDurSec > waterRunMaxSec[statusMonitor])
    {
        closeValve();
    }
}

void handleTimedEvents()
{
    if (oldMin != getTimeInt("Minute"))
    {
        Serial.println("<<<>>> NEW Minute");
        volumeMin = 0;
        oldMin = getTimeInt("Minute");
        volumeNeedsSave = true;
    }

    if (oldHour != getTimeInt("Hour"))
    {
        Serial.printf("\n<<<>>> NEW Hour:: Day Volume: %.2f\n\n", volumeHour);
        sendflow(oldTimeStamp, volumeHour);
        oldTimeStamp = getTimeString("DateTimeMin");
        volumeHour = 0;
        oldHour = getTimeInt("Hour");
        volumeNeedsSave = true;
        resetMaxValues();
    }

    if (oldDay != getTimeInt("Day"))
    {
        Serial.printf("\n<<<>>> NEW DAY:: Day Volume: %.2f\n\n", volumeDay);
        volumeDay = 0;
        oldDay = getTimeInt("Day");
        volumeNeedsSave = true;
    }

    if (millis() - timerSendFlowCheckMs > sendFlowTimeMs)
    {
        timerSendFlowCheckMs = millis();
        if (flow10s != lastFlow10s || flowAvgValue != lastFlowAvgValue || waterRunDurSec != lastWaterRunDurSec || lastValveClosed != valveClosed || LastStatusMonitor!= statusMonitor)
        {

            lastFlow10s = flow10s;
            lastFlowAvgValue = flowAvgValue;
            lastWaterRunDurSec = waterRunDurSec;
            lastValveClosed = valveClosed;
            LastStatusMonitor = statusMonitor;
            sendSimpleFlowData(warningAlert);
            warningAlert = 0;
        }
    }
}

void logFlowStatus(long pulseCountNow, float volumeNowgal)
{
    Serial.println();
    Serial.printf("Pulse Count: %ld\n", pulseCountNow);
    Serial.printf("Flow update --> 10secvol: %.2f || 10secflow: %.2f || 30secFlow: %.2f\n", volumeNowgal, flow10s, flowAvgValue);
    Serial.printf("VolumeHour: %.2f || running: %d || Running Time: %lu || Stop Time: %lu || valveClosed: %d\n || waterRunMaxSec: %d\n",
                  volumeHour, waterRun, waterRunDurSec, waterStopDurSec, valveClosed, waterRunMaxSec[statusMonitor]);
}

void flowCalcs()
{
    if (millis() - timerUpdateCheckMs > updateFlowTimeMs)
    {
        checkWiFiReconnect();
        timerUpdateCheckMs = millis();

        long pulseCountNow = pulseCount;
        pulseCount = 0;
        float volumeNowgal = pulseCountNow / calibrationFactor;
        flow10s = volumeNowgal * galPerMinFactor;

        calculateFlowStats(volumeNowgal);
        updateWaterState(pulseCountNow, volumeNowgal);
        handleValveLogic();
        handleTimedEvents();
        logFlowStatus(pulseCountNow, volumeNowgal);

        if (volumeNeedsSave && (millis() - lastVolumeSave > volumeSaveInterval))
        {
            saveVolumeToPrefs();
        }
    }
}

void checkButtonMode()
{
    bool currentReading = digitalRead(BUTTON_MODE_PIN);

    if (currentReading != buttonModeLastReading)
    {
        buttonModeLastDebounceTime = millis();
    }

    if ((millis() - buttonModeLastDebounceTime) > debounceDelay)
    {
        if (currentReading != buttonModeState)
        {
            buttonModeState = currentReading;

            if (buttonModeState == LOW)
            {
                buttonModePressStart = millis();
                buttonModePreviouslyPressed = true;
            }
            else if (buttonModePreviouslyPressed && buttonModeState == HIGH)
            {
                unsigned long pressDuration = millis() - buttonModePressStart;

                if (pressDuration >= LONG_PRESS_DURATION)
                {
                    Serial.println("ButtonMode long press: restarting...");
                    ESP.restart();
                }
                else
                {
                    int statusMonitorTemp = statusMonitor + 1;
                    if (statusMonitorTemp > 2)
                    {
                        statusMonitorTemp = 0;
                    }
                    setValveMode(statusMonitorTemp);
                    Serial.printf("ButtonMode short press: statusMonitor = %d\n", statusMonitor);
                }

                buttonModePreviouslyPressed = false;
            }
        }
    }

    buttonModeLastReading = currentReading;
}

void checkButtonValve()
{
    bool currentReading = digitalRead(BUTTON_VALVE_PIN);

    if (currentReading != buttonValveLastReading)
    {
        buttonValveLastDebounceTime = millis();
    }

    if ((millis() - buttonValveLastDebounceTime) > debounceDelay)
    {
        if (currentReading != buttonValveState)
        {
            buttonValveState = currentReading;

            if (buttonValveState == LOW)
            {
                buttonValvePreviouslyPressed = true;
            }
            else if (buttonValvePreviouslyPressed && buttonValveState == HIGH)
            {
                if (valveClosed)
                {
                    openValve();
                }
                else
                {
                    closeValve();
                }
                Serial.printf("ButtonValve short press: valveClosed = %s\n", valveClosed ? "true" : "false");
                buttonValvePreviouslyPressed = false;
            }
        }
    }

    buttonValveLastReading = currentReading;
}

void loadVolumeFromPrefs()
{
    Serial.println("Loading Values from Memory...");
    volumePrefs.begin("flowvol", true); // read-only
    volumeHour = volumePrefs.getFloat("volHour", 0.0);
    volumeMin = volumePrefs.getFloat("volMin", 0.0);
    volumeDay = volumePrefs.getFloat("volDay", 0.0);
    oldHour = volumePrefs.getInt("oldHour", 0);
    oldDay = volumePrefs.getInt("oldDay", 0);
    oldTimeStamp = volumePrefs.getString("oldTimeStamp", "");
    minuteStampsPrevious = volumePrefs.getString("minStP", "");
    int statusMonitorTemp = volumePrefs.getInt("statusMonitor", 1);
    valveClosed = volumePrefs.getBool("valveClosed", false);
    volumePrefs.end();

    /// update LEDS
    setValveMode(statusMonitorTemp);
    // statusMonitor = statusMonitorTemp;

    /// confirm valve status is real
    if (valveClosed)
    {
        showPixelColorEx(1, 255, 0, 0); // red
    }
    else
    {
        showPixelColorEx(1, 0, 255, 0); // green
    }

    Serial.printf("Restored Volumes - Hour: %.2f  Min: %.2f  Day: %.2f || Old Hour: %d  Old Day: %d\n",
                  volumeHour, volumeMin, volumeDay, oldHour, oldDay);
}

void saveVolumeToPrefs()
{
    Serial.println("Saving values to memory...");
    volumePrefs.begin("flowvol", false);
    volumePrefs.putFloat("volHour", volumeHour);
    volumePrefs.putFloat("volMin", volumeMin);
    volumePrefs.putFloat("volDay", volumeDay);
    volumePrefs.putInt("oldHour", oldHour);
    volumePrefs.putInt("oldDay", oldDay);
    volumePrefs.putString("oldTimeStamp", oldTimeStamp);
    volumePrefs.putString("minStP", minuteStampsPrevious);
    volumePrefs.putInt("statusMonitor", statusMonitor);
    volumePrefs.putBool("valveClosed", valveClosed);

    volumePrefs.end();

    volumeNeedsSave = false;
    lastVolumeSave = millis();
    Serial.println("Done");
}

void setValveMode(int newMode)
{
    if (newMode != statusMonitor)
    {
        statusMonitor = newMode;
        saveVolumeToPrefs();
        Serial.print("Valve mode updated to: ");
        Serial.println(statusMonitor);

        
        
        if (statusMonitor == 0)
        {
            showPixelColorEx(0, 255, 102, 0);
        }
        else if (statusMonitor == 1)
        {
            showPixelColorEx(0, 0, 0, 255);
        }
        else if (statusMonitor == 2)
        {
            showPixelColorEx(0, 255, 0, 255);
        }

    }
    else
    {
        Serial.print("Valve mode not updated, already set to: ");
        Serial.println(statusMonitor);
    }
}

#endif
