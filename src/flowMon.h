#ifndef MY_FLOWMON_H
#define MY_FLOWMON_H

#define FLOW_SENSOR_PIN 27 // Replace with your flowmeter pin

// flow input global variables
const int sendFlowTimeMs = 10000;          /// default flow check time
const int updateFlowTimeMs = 10000;        /// default flow updatetime
const float calibrationFactor = 10.0;      // pulse per gallon
const unsigned long waterRunMinSec = 15;   // duration of no pulse to stop water as running
const int waterRunMaxSec[3] = {0, 60, 30}; // run time before shutoff {manual,home,away}

const int maxIntervals = 3600 / (updateFlowTimeMs / 1000); // Maximum 60 minutes of 5-second intervals
const float galPerMinFactor = 60.0 / (updateFlowTimeMs / 1000);

// Declare global variables
bool waterRun = false;                 // set status of whether water is curently running
unsigned long waterRunDurSec;          // how long water has been running
unsigned long waterStopDurSec;         // how long water has been stopped
float flowSamples[maxIntervals] = {0}; // Stores flow rate samples
int minuteStamps[maxIntervals] = {0};  // Timestamps for samples
int minuteStampsPrevious;              // Timestamps for samples
int sampleIndex = 0;                   // measurement counter
int flowAvgIndex = 0;                  // measurement counter
float flowAvg[6];                      // Stores flow rate samples
float flowAvgValue;
float flow10s;                         // 10 second flow rate g/m
unsigned long timerUpdateCheckMs;        // temp check value
unsigned long timerSendFlowCheckMs;      // temp check value
volatile unsigned long pulseCount = 0; // pulse counter
int oldHour;                           // establish variable to check for new hour
int oldDay;                            // establish variable to check for new day
int oldMin;                            // establish variable to check for new day
float volumeHour = 0.0;
float volumeMin = 0.0;
float volumeDay = 0.0;

// Tracking highest volumes and their timestamps
float max1Min = 0, max10Sec = 0, max10Min = 0, max30Min = 0;
int max1MinTime;
int max10SecTime;
int max10MinTime;
int max30MinTime;

// Interrupt Service Routine (ISR) for counting pulses
void IRAM_ATTR pulseCounter()
{
    pulseCount++;
}

void flowMeterSetup()
{
    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    // Attach interrupt to the flow sensor pin
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);
    Serial.println("Flowmeter Monitoring Started...");

    // set up hourly and minute timers
    oldDay = getDay();
    oldHour = getHour();
    oldMin = getMinute();
    minuteStampsPrevious = getMinutesToday();

    Serial.print("Current Times>> Day:");
    Serial.print(oldDay);
    Serial.print("  Hour:");
    Serial.print(oldHour);
    Serial.print("  Min:");
    Serial.println(oldMin);
}

void resetMaxValues()
{

    //for (int i = 0; i < sampleIndex - 1; i++)
    //{
    //    Serial.print(i);
    //   Serial.print(":");
    //    Serial.print(flowSamples[i]);
    //    Serial.print(" @ ");
    //    Serial.println(minuteStamps[i]);
    //}

    // Reset tracking variables
    memset(flowSamples, 0, sizeof(flowSamples));
    memset(minuteStamps, 0, sizeof(minuteStamps));
    sampleIndex = 0;
    max1Min = max10Sec = max10Min = max30Min = 0;
    max1MinTime = max10SecTime = max10MinTime = max30MinTime = 0;
}

void updateMax(float *maxVol, int *maxTime, int numSamples, int sampleIndex)
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
        *maxTime = minuteStamps[sampleIndex - numSamples + 1]; // Time of the first sample in the window
    }
}

void updateVolumes(int sampleIndex)
{
    updateMax(&max1Min, &max1MinTime, 60 / (updateFlowTimeMs / 1000), sampleIndex);     // 1 min = 60 samples
    updateMax(&max10Sec, &max10SecTime, 10 / (updateFlowTimeMs / 1000), sampleIndex);    // 10 seconds = 10 samples
    updateMax(&max10Min, &max10MinTime, 600 / (updateFlowTimeMs / 1000), sampleIndex);  // 10 min = 600 samples
    updateMax(&max30Min, &max30MinTime, 1800 / (updateFlowTimeMs / 1000), sampleIndex); // 30 min = 1800 samples
}

void sendflow( int hourSend,int volumeSend,int DaySend){

    sendFullFlowData(
        max10Sec, max1Min, max10Min, volumeSend,
        max10SecTime, max1MinTime, max10MinTime,
        DaySend, hourSend,
        closeValve, statusMonitor
      );

}


void flowCalcs()
{
    if (millis() - timerUpdateCheckMs > updateFlowTimeMs)
    {

        long pulseCountNow = pulseCount;
        pulseCount = 0;
        long timerFlowActualSec = (millis() - timerUpdateCheckMs) / 1000; // actual time between checks - seconds
        timerUpdateCheckMs = millis();
        Serial.println("");
        Serial.print("Pulse Count:");
        Serial.println(pulseCountNow);

        float volumeNowgal = pulseCountNow / calibrationFactor;

        // check flow
        if (pulseCountNow > 0)
        {
            waterRunDurSec += timerFlowActualSec;
            waterStopDurSec = 0;
            waterRun = true;
            volumeHour += volumeNowgal;
            volumeDay += volumeNowgal;
            volumeMin += volumeNowgal;
        }
        else
        {
            if (waterStopDurSec >= waterRunMinSec)
            {
                waterRunDurSec = 0;
                waterRun = false;
            }
            else
            {
                waterRunDurSec += timerFlowActualSec;
                waterStopDurSec += timerFlowActualSec;
            }
        }

        // Update the highest moving averages
        // Store the latest sample and timestamp

        flowSamples[sampleIndex] = flowAvg[flowAvgIndex] = volumeNowgal * galPerMinFactor;
        minuteStamps[sampleIndex] = minuteStampsPrevious;
        minuteStampsPrevious = getMinutesToday();
        updateVolumes(sampleIndex);
        sampleIndex = (sampleIndex + 1) % maxIntervals;

        /// calce 30 sec flow avg
        flowAvgValue = 0;
        for (int i = 0; i < 3; i++)
        {
            flowAvgValue += flowAvg[i];
        }
        flowAvgValue = flowAvgValue / 3;
        flowAvgIndex++;
        if (flowAvgIndex > 2)
        {
            flowAvgIndex = 0;
        }

        /// shut off valve if running too long
        if (!closeValve && statusMonitor != 3 && waterRunDurSec > waterRunMaxSec[statusMonitor])
        {
            Serial.println("===========================");
            Serial.println("!!!!!!!!SHUT OFF WATER!!!!!");
            Serial.println("===========================");
            closeValve = true;

            // send warning to parent
            sendSimpleFlowData(flow10s, flowAvgValue, closeValve, waterRunDurSec, statusMonitor, 1);
        }

        flow10s = volumeNowgal * galPerMinFactor;

        Serial.print("Flow update --> 10secvol:");
        Serial.print(volumeNowgal);
        Serial.print(" || 10secflow:");
        Serial.print(flow10s);
        Serial.print(" || 30secFlow:");
        Serial.print(flowAvgValue);
        Serial.print(" || VolumeHour:");
        Serial.print(volumeHour);
        Serial.print(" || running:");
        Serial.print(waterRun);
        Serial.print(" || Running Time:");
        Serial.print(waterRunDurSec);
        Serial.print(" || Stop Time:");
        Serial.print(waterStopDurSec);
        Serial.print(" || closeValve:");
        Serial.println(closeValve);

        

        ///time day timers
        
        if (oldMin != getMinute())
        {   
            Serial.println("<<<>>> NEW Minute");
            //send full data
            sendflow(oldMin,volumeMin,oldDay);
            //reset max values
            resetMaxValues();
            volumeMin = 0;
            oldMin = getMinute();
        }

        if (oldHour != getHour())
        {
            Serial.println("");
            Serial.print("<<<>>> NEW Hour:: Day Volume: ");
            Serial.println(volumeHour);
            Serial.println("");
            volumeHour = 0;
            oldHour = getHour();
        }
        if (oldDay != getDay())
        {
            Serial.println("");
            Serial.print("<<<>>> NEW DAY:: Day Volume: ");
            Serial.println(volumeDay);
            Serial.println("");
            volumeDay = 0;
            oldDay = getDay();
        }

        /// send flow info when time
        if (millis() - timerSendFlowCheckMs > sendFlowTimeMs)
        {
            timerSendFlowCheckMs = millis();
            sendSimpleFlowData(flow10s, flowAvgValue, closeValve, waterRunDurSec, statusMonitor, 0);
        }
    }
}

void checkflow()
{

    // update flow numbers every updateFlowTimeMs
    flowCalcs(); // check if water is running
}

#endif