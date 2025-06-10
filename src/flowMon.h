#ifndef MY_FLOWMON_H
#define MY_FLOWMON_H

#include <Preferences.h>
#include "espMqtt.h"

// === Pins ===
#define FLOW_SENSOR_PIN    25
#define VALVE_RELAY        4
#define BUTTON_MODE_PIN    32
#define BUTTON_VALVE_PIN   33

// === Flow & Timing Configuration ===
const unsigned int pulseDebounceUs = 200000;
const int sendFlowTimeMs = 10000;
const int updateFlowTimeMs = 10000;
const float calibrationFactor = 10.0;
const unsigned long waterRunMinSec = 15;
const int waterRunMaxSec[3] = {0, 60, 30};
const int maxIntervals = 3600 / (updateFlowTimeMs / 1000);
const float galPerMinFactor = 60.0 / (updateFlowTimeMs / 1000);
#define VALVE_CYCLE_TIMEOUT 40000
#define VALVE_CYCLE_DELAY   10000

// === State Variables ===
bool valveClosed = false;
int statusMonitor = 3; // 0: manual, 1: home, 2: away

// === Flow Tracking ===
volatile unsigned long pulseCount = 0;
volatile unsigned long lastPulseTime = 0;
float flowSamples[maxIntervals] = {0};
String minuteStamps[maxIntervals] = {""};
String minuteStampsPrevious = "";
float flowAvg[6] = {0};
float flow10s = 0, lastFlow10s = 0;
float flowAvgValue = 0, lastFlowAvgValue = 0;
int sampleIndex = 0, flowAvgIndex = 0;

// === Time Tracking ===
unsigned long waterRunDurSec = 0, lastWaterRunDurSec = 0, waterStopDurSec = 0;
unsigned long timerUpdateCheckMs = 0, timerSendFlowCheckMs = 0;
bool waterRun = false;
int oldHour = 0, oldDay = 0, oldMin = 0;
int lastValveClosed = 3, LastStatusMonitor = 4;
String oldTimeStamp = getTimeString("DateTimeMin");

// === Max Flow Volumes ===
float max1Min = 0, max10Sec = 0, max10Min = 0, max30Min = 0;
String max1MinTime = "", max10SecTime = "", max10MinTime = "", max30MinTime = "";

// === Volume Tracking ===
float volumeHour = 0.0, volumeMin = 0.0, volumeDay = 0.0;
bool volumeNeedsSave = false;
unsigned long lastVolumeSave = 0;
const unsigned long volumeSaveInterval = 60000;
int warningAlert = 0;

// === Button Debounce ===
const unsigned long debounceDelay = 50;
const unsigned long LONG_PRESS_DURATION = 10000;
bool buttonModeState = HIGH, buttonModeLastReading = HIGH, buttonModePreviouslyPressed = false;
unsigned long buttonModePressStart = 0, buttonModeLastDebounceTime = 0;
bool buttonValveState = HIGH, buttonValveLastReading = HIGH, buttonValvePreviouslyPressed = false;
unsigned long buttonValveLastDebounceTime = 0;

// === Preferences Storage ===
Preferences volumePrefs;

// === Function Declarations ===
void loadVolumeFromPrefs();


// === Interrupt: Flow Pulse Counter ===
void IRAM_ATTR pulseCounter() {
    unsigned long now = micros();
    if (now - lastPulseTime > pulseDebounceUs) {
        pulseCount++;
        lastPulseTime = now;
    }
}

// === Setup ===
void flowMeterSetup() {
    Serial.print("Initializing Flowmeter Monitoring...");
    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);
    pinMode(BUTTON_MODE_PIN, INPUT_PULLUP);
    pinMode(BUTTON_VALVE_PIN, INPUT_PULLUP);
    Serial.println("Done");
    loadVolumeFromPrefs();
}

void valveRelaySetup() {
    pinMode(VALVE_RELAY, OUTPUT);
}

// === Valve Control ===
void closeValve() {
    digitalWrite(VALVE_RELAY, HIGH);
    Serial.println("!!! SHUT OFF WATER !!!");
    valveClosed = true;
    warningAlert = 1;
    showPixelColorEx(1, 255, 0, 0);
}

void openValve() {
    digitalWrite(VALVE_RELAY, LOW);
    Serial.println("Valve OPEN");
    valveClosed = false;
    waterRunDurSec = 0;
    showPixelColorEx(1, 0, 255, 0);
}

void cycleValve() {
    Serial.println("Starting valve cycle...");
    unsigned long startTime = millis();
    closeValve();
    while (millis() - startTime < VALVE_CYCLE_DELAY) {
        delay(100);
        if (millis() - startTime > VALVE_CYCLE_TIMEOUT) {
            Serial.println("Cycle timeout before reopening valve!");
            sendAck("cycle_valve", "timeout_before_open");
            return;
        }
    }
    openValve();
    if (millis() - startTime > VALVE_CYCLE_TIMEOUT) {
        Serial.println("Cycle timeout after reopening valve!");
        sendAck("cycle_valve", "timeout_after_open");
        return;
    }
    sendAck("cycle_valve", "completed");
}

// === Flow & Volume Processing ===
void resetMaxValues() {
    memset(flowSamples, 0, sizeof(flowSamples));
    for (int i = 0; i < maxIntervals; i++) minuteStamps[i] = "";
    sampleIndex = 0;
    max1Min = max10Sec = max10Min = max30Min = 0;
    max1MinTime = max10SecTime = max10MinTime = max30MinTime = "";
}

void updateMax(float *maxVol, String *maxTime, int numSamples, int index) {
    if (numSamples > index + 1) return;
    float sum = 0;
    for (int i = index + 1 - numSamples; i <= index; i++) sum += flowSamples[i];
    float avg = sum / numSamples;
    if (avg > *maxVol) {
        *maxVol = avg;
        *maxTime = minuteStamps[index - numSamples + 1];
    }
}

void updateVolumes(int index) {
    updateMax(&max1Min, &max1MinTime, 60 / (updateFlowTimeMs / 1000), index);
    updateMax(&max10Sec, &max10SecTime, 10 / (updateFlowTimeMs / 1000), index);
    updateMax(&max10Min, &max10MinTime, 600 / (updateFlowTimeMs / 1000), index);
    updateMax(&max30Min, &max30MinTime, 1800 / (updateFlowTimeMs / 1000), index);
}

void sendflow(String oldTimeStamp, int volumeSend) {
    sendFlowData(max10Sec, max1Min, max10Min, volumeSend,
                 max10SecTime, max1MinTime, max10MinTime,
                 oldTimeStamp, valveClosed, statusMonitor);
}

void calculateFlowStats(float volumeNowgal) {
    flowSamples[sampleIndex] = flowAvg[flowAvgIndex] = volumeNowgal * galPerMinFactor;
    minuteStamps[sampleIndex] = minuteStampsPrevious;
    minuteStampsPrevious = getTimeString("DateTimeMin");
    updateVolumes(sampleIndex);
    sampleIndex = (sampleIndex + 1) % maxIntervals;

    flowAvgValue = 0;
    for (int i = 0; i < 3; i++) flowAvgValue += flowAvg[i];
    flowAvgValue /= 3;
    flowAvgIndex = (flowAvgIndex + 1) % 3;
}

void updateWaterState(long pulses, float volumeNowgal) {
    long deltaSec = updateFlowTimeMs / 1000;
    if (pulses > 0) {
        waterRunDurSec += deltaSec;
        waterStopDurSec = 0;
        waterRun = true;
        volumeHour += volumeNowgal;
        volumeDay += volumeNowgal;
        volumeMin += volumeNowgal;
        volumeNeedsSave = true;
    } else {
        if (waterStopDurSec >= waterRunMinSec) {
            waterRunDurSec = 0;
            waterRun = false;
        }
        waterStopDurSec += deltaSec;
    }
}

void handleValveLogic() {
    if (statusMonitor < 0 || statusMonitor > 2) statusMonitor = 1;
    if (!valveClosed && statusMonitor != 0 && waterRunDurSec > waterRunMaxSec[statusMonitor])
        closeValve();
}

void handleTimedEvents() {
    if (oldMin != getTimeInt("Minute")) {
        volumeMin = 0;
        oldMin = getTimeInt("Minute");
        volumeNeedsSave = true;
    }

    if (oldHour != getTimeInt("Hour")) {
        sendflow(oldTimeStamp, volumeHour);
        oldTimeStamp = getTimeString("DateTimeMin");
        volumeHour = 0;
        oldHour = getTimeInt("Hour");
        volumeNeedsSave = true;
        resetMaxValues();
    }

    if (oldDay != getTimeInt("Day")) {
        volumeDay = 0;
        oldDay = getTimeInt("Day");
        volumeNeedsSave = true;
    }

    if (millis() - timerSendFlowCheckMs > sendFlowTimeMs) {
        timerSendFlowCheckMs = millis();
        if (flow10s != lastFlow10s || flowAvgValue != lastFlowAvgValue ||
            waterRunDurSec != lastWaterRunDurSec || lastValveClosed != valveClosed ||
            LastStatusMonitor != statusMonitor) {

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

void logFlowStatus(long pulseCountNow, float volumeNowgal) {
    Serial.printf("\nPulse Count: %ld\n", pulseCountNow);
    Serial.printf("Flow10s: %.2f GPM | FlowAvg: %.2f GPM | VolumeHour: %.2f gal\n",
                  flow10s, flowAvgValue, volumeHour);
    Serial.printf("Running: %d | Time Run: %lu s | Stop: %lu s | ValveClosed: %d\n",
                  waterRun, waterRunDurSec, waterStopDurSec, valveClosed);
}

void flowCalcs() {
    if (millis() - timerUpdateCheckMs > updateFlowTimeMs) {
        checkWiFiReconnect();
        timerUpdateCheckMs = millis();

        long pulseNow = pulseCount;
        pulseCount = 0;
        float volNowGal = pulseNow / calibrationFactor;
        flow10s = volNowGal * galPerMinFactor;

        calculateFlowStats(volNowGal);
        updateWaterState(pulseNow, volNowGal);
        handleValveLogic();
        handleTimedEvents();
        logFlowStatus(pulseNow, volNowGal);

        if (volumeNeedsSave && millis() - lastVolumeSave > volumeSaveInterval)
            saveVolumeToPrefs();
    }
}

void checkButtonMode() {
    bool reading = digitalRead(BUTTON_MODE_PIN);
    if (reading != buttonModeLastReading) buttonModeLastDebounceTime = millis();

    if (millis() - buttonModeLastDebounceTime > debounceDelay) {
        if (reading != buttonModeState) {
            buttonModeState = reading;
            if (buttonModeState == LOW) {
                buttonModePressStart = millis();
                buttonModePreviouslyPressed = true;
            } else if (buttonModePreviouslyPressed) {
                unsigned long pressDur = millis() - buttonModePressStart;
                if (pressDur >= LONG_PRESS_DURATION) {
                    Serial.println("Long press: restarting");
                    ESP.restart();
                } else {
                    int newMode = (statusMonitor + 1) % 3;
                    setValveMode(newMode);
                }
                buttonModePreviouslyPressed = false;
            }
        }
    }
    buttonModeLastReading = reading;
}

void checkButtonValve() {
    bool reading = digitalRead(BUTTON_VALVE_PIN);
    if (reading != buttonValveLastReading) buttonValveLastDebounceTime = millis();

    if (millis() - buttonValveLastDebounceTime > debounceDelay) {
        if (reading != buttonValveState) {
            buttonValveState = reading;
            if (buttonValveState == LOW) {
                buttonValvePreviouslyPressed = true;
            } else if (buttonValvePreviouslyPressed) {
                valveClosed ? openValve() : closeValve();
                buttonValvePreviouslyPressed = false;
            }
        }
    }
    buttonValveLastReading = reading;
}

void loadVolumeFromPrefs() {
    Serial.println("Loading values...");
    volumePrefs.begin("flowvol", true);
    volumeHour = volumePrefs.getFloat("volHour", 0.0);
    volumeMin = volumePrefs.getFloat("volMin", 0.0);
    volumeDay = volumePrefs.getFloat("volDay", 0.0);
    oldHour = volumePrefs.getInt("oldHour", 0);
    oldDay = volumePrefs.getInt("oldDay", 0);
    oldTimeStamp = volumePrefs.getString("oldTimeStamp", "");
    minuteStampsPrevious = volumePrefs.getString("minStP", "");
    int savedStatus = volumePrefs.getInt("statusMonitor", 1);
    valveClosed = volumePrefs.getBool("valveClosed", false);
    volumePrefs.end();
    setValveMode(savedStatus);
    showPixelColorEx(1, valveClosed ? 255 : 0, valveClosed ? 0 : 255, 0);
}

void saveVolumeToPrefs() {
    Serial.println("Saving values...");
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

void setValveMode(int newMode) {
    if (newMode != statusMonitor) {
        statusMonitor = newMode;
        saveVolumeToPrefs();
        Serial.printf("Valve mode updated: %d\n", statusMonitor);
        if (statusMonitor == 0)      showPixelColorEx(0, 255, 102, 0);
        else if (statusMonitor == 1) showPixelColorEx(0, 0, 0, 255);
        else if (statusMonitor == 2) showPixelColorEx(0, 255, 0, 255);
    } else {
        Serial.printf("Valve mode unchanged: %d\n", statusMonitor);
    }
}

#endif
