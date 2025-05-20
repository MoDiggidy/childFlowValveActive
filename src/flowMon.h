#ifndef MY_FLOWMON_H
#define MY_FLOWMON_H

#include <Preferences.h>
#include "espMqtt.h"

#define FLOW_SENSOR_PIN 27 // Replace with your flowmeter pin

// === Configuration Constants ===
const int sendFlowTimeMs = 10000;        // rate at which simpleflow data gets sent
const int updateFlowTimeMs = 10000;      // cycle time to check pulse count on flowmeter
const float calibrationFactor = 10.0;    // pulses per gallon
const unsigned long waterRunMinSec = 15; // how long flow stops before updateing flow to stopped
const int waterRunMaxSec[3] = {0, 60, 30};
const int maxIntervals = 3600 / (updateFlowTimeMs / 1000);
const float galPerMinFactor = 60.0 / (updateFlowTimeMs / 1000);
#define VALVE_CYCLE_TIMEOUT 40000 // Max cycle time in ms (e.g. 10 seconds)
#define VALVE_CYCLE_DELAY 10000   // Time valve remains closed before reopening 
#define VALVE_RELAY 33            // Valve Relay Pin

// === Global Flow Variables ===
bool waterRun = false;
unsigned long waterRunDurSec = 0;
unsigned long lastWaterRunDurSec = 0;

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
unsigned long timerUpdateCheckMs = 0;
unsigned long timerSendFlowCheckMs = 0;
volatile unsigned long pulseCount = 0;
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

String oldTimeStamp = getDateTimeMin();

// === Max Volume Tracking ===
float max1Min = 0, max10Sec = 0, max10Min = 0, max30Min = 0;
String max1MinTime = "", max10SecTime = "", max10MinTime = "", max30MinTime = "";

// === External Valve/State Flags ===
extern bool valveClosed;
extern int statusMonitor;

// declaire functions early
void saveVolumeToPrefs();
void loadVolumeFromPrefs();

// === ISR ===
void IRAM_ATTR pulseCounter()
{
    pulseCount++;
}

void flowMeterSetup()
{
    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);
    Serial.println("Flowmeter Monitoring Started...");

    oldDay = getDay();
    oldHour = getHour();
    oldMin = getMinute();
    minuteStampsPrevious = getDateTimeMin();

    Serial.printf("Current Times >> Day: %d  Hour: %d  Min: %d\n", oldDay, oldHour, oldMin);
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
    minuteStampsPrevious = getDateTimeMin();

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
    sendSimpleFlowData(1);
}

void openValve()
{
    digitalWrite(VALVE_RELAY, LOW); // Open Valve
    Serial.println("===========================");
    Serial.println("Valve is now OPEN");
    Serial.println("===========================");
    valveClosed = false;
    sendSimpleFlowData(0);
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
    if (oldMin != getMinute())
    {
        Serial.println("<<<>>> NEW Minute");
        sendflow(oldTimeStamp, volumeHour);
        volumeMin = 0;
        oldTimeStamp = getDateTimeMin();
        oldMin = getMinute();
        volumeNeedsSave = true;
    }

    if (oldHour != getHour())
    {
        Serial.printf("\n<<<>>> NEW Hour:: Day Volume: %.2f\n\n", volumeHour);
        volumeHour = 0;
        oldHour = getHour();
        volumeNeedsSave = true;
        resetMaxValues();
    }

    if (oldDay != getDay())
    {
        Serial.printf("\n<<<>>> NEW DAY:: Day Volume: %.2f\n\n", volumeDay);
        volumeDay = 0;
        oldDay = getDay();
        volumeNeedsSave = true;
    }

    if (millis() - timerSendFlowCheckMs > sendFlowTimeMs)
    {
        timerSendFlowCheckMs = millis();
        if (flow10s != lastFlow10s || flowAvgValue != lastFlowAvgValue || waterRunDurSec != lastWaterRunDurSec)
        {

            lastFlow10s = flow10s;
            lastFlowAvgValue = flowAvgValue;
            lastWaterRunDurSec = waterRunDurSec;
            sendSimpleFlowData(0);
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

void loadVolumeFromPrefs()
{
    volumePrefs.begin("flowvol", true); // read-only
    volumeHour = volumePrefs.getFloat("volHour", 0.0);
    volumeMin = volumePrefs.getFloat("volMin", 0.0);
    volumeDay = volumePrefs.getFloat("volDay", 0.0);
    oldHour = volumePrefs.getInt("oldHour", oldHour);
    oldDay = volumePrefs.getInt("oldDay", oldDay);
    volumePrefs.end();

    Serial.printf("Restored Volumes - Hour: %.2f  Min: %.2f  Day: %.2f || Old Hour: %d  Old Day: %d\n",
                  volumeHour, volumeMin, volumeDay, oldHour, oldDay);
}

void saveVolumeToPrefs()
{
    volumePrefs.begin("flowvol", false);
    volumePrefs.putFloat("volHour", volumeHour);
    volumePrefs.putFloat("volMin", volumeMin);
    volumePrefs.putFloat("volDay", volumeDay);
    volumePrefs.putInt("oldHour", oldHour);
    volumePrefs.putInt("oldDay", oldDay);

    volumePrefs.end();

    Serial.println("Saved volume values to preferences.");
    volumeNeedsSave = false;
    lastVolumeSave = millis();
}

#endif
