#ifndef MY_ESPMQTT_H
#define MY_ESPMQTT_H

#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_task_wdt.h"

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;

// === CONFIGURATION ===

const char *MQTT_Client_ID = "6556DomFlow1";
const char *mqtt_server = "10.140.1.95";                                             // Replace with your MQTT broker IP
const char *mqtt_fullflow_topic = "6556/water/domestic/domesticSupplyFlow/flowData"; // Your topic
const char *mqtt_simpleflow_topic = "6556/water/domestic/domesticSupplyFlow/simpleFlowData";
const char *mqtt_lwt_topic = "6556/water/domestic/domesticSupplyFlow/status";
const char *mqtt_lwt_message = "offline";
const char *mqtt_online_message = "online";

const char *mqtt_command_topic = "6556/water/domestic/domesticSupplyFlow/cmdSend";
const char *mqtt_ack_topic = "6556/water/domestic/domesticSupplyFlow/cmdAck";


#define RETRY_INTERVAL 60000UL // 1 min
#define BUFFER_SIZE 24         // up to 24 hours of data
#define CUSTOM_MQTT_KEEPALIVE 60

unsigned long lastRetryTime = 0;
unsigned long lastSend = 0;
bool isCyclingValve = false;

extern float flow10s;
extern float flowAvgValue;
extern bool valveStatus;
extern long unsigned int waterRunDurSec;
extern int statusMonitor;

// ==== FUNCTION DECLARATIONS ====
extern void closeValve();
extern void openValve();
extern void cycleValve();
void connectToWiFi();
void connectToMQTT();
bool sendMQTTMessage(const char *payload);
void savePayloadToBuffer(const char *payload);
bool loadPayloadFromBuffer(int index, String &payloadOut);
void clearPayloadFromBuffer(int index);
int getIndex(const char *key);
void setIndex(const char *key, int value);
void retryUnsentPayloads();
void sendFlowData(
    float max10s_fl, float max1m_fl, float max10m_fl, float total_fl,
    String max10sTimeStamp, String max1mTimeStamp, String max10mTimeStamp,
    String timeStamp,
    int valveStatusDom, int valveModeDom);

void sendSimpleFlowData(int warning);

// ==== IMPLEMENTATIONS ====

void sendAck(const char *cmd, const char *status)
{
    StaticJsonDocument<128> ack;
    ack["cmd"] = cmd;
    ack["status"] = status;
    ack["timeStamp"] = getDateTimeMin();

    char response[128];
    serializeJson(ack, response);
    mqttClient.publish(mqtt_ack_topic, response, true);
    Serial.printf("Acknowledgment sent: %s\n", response);
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    String msg;
    for (unsigned int i = 0; i < length; i++)
        msg += (char)payload[i];

    Serial.printf("Message received on topic %s: %s\n", topic, msg.c_str());

    if (String(topic) == mqtt_command_topic)
    {
        StaticJsonDocument<128> doc;
        DeserializationError error = deserializeJson(doc, msg);
        if (error)
        {
            Serial.println("Failed to parse JSON command.");
            return;
        }

        const char *cmd = doc["cmd"];

        if (strcmp(cmd, "close_valve") == 0)
        {
            closeValve();
            sendAck(cmd, "received");
        }
        else if (strcmp(cmd, "open_valve") == 0)
        {
            openValve();
            sendAck(cmd, "received");
        }
        else if (strcmp(cmd, "cycle_valve") == 0)
        {
            if (isCyclingValve)
            {
                sendAck("cycle_valve", "already_running");
                return;
            }
            isCyclingValve = true;
            cycleValve();
            isCyclingValve = false;
        }
        else if (strcmp(cmd, "Status0") == 0)
        {
            statusMonitor = 0;
            Serial.printf("valve mode changed to Manual");
            sendAck(cmd, "received");
        }
        else if (strcmp(cmd, "Status1") == 0)
        {
            statusMonitor = 1;
            Serial.printf("valve mode changed to Home");
            sendAck(cmd, "received");
        }
        else if (strcmp(cmd, "Status2") == 0)
        {
            statusMonitor = 2;
            Serial.printf("valve mode changed to Away");
            sendAck(cmd, "received");
        }
        else
        {
            Serial.printf("Unknown command: %s\n", cmd);
            sendAck(cmd, "unknown");
        }
    }
}

void connectToMQTT()
{
    unsigned long startAttempt = millis();
    const unsigned long maxDuration = 5000; // 5 seconds max

    while (!mqttClient.connected())
    {
        Serial.print("Connecting to MQTT...");
        mqttClient.setBufferSize(512); // or 1024 for very large messages
        // Set username and password here
        if (mqttClient.connect(
                MQTT_Client_ID,       // Client ID
                MQTT_USER, MQTT_PASS, // Username and Password
                mqtt_lwt_topic, 1, true,
                mqtt_lwt_message)) // Last Will message
        {
            mqttClient.publish(mqtt_lwt_topic, mqtt_online_message, true); // Publish online status
    mqttClient.setCallback(mqttCallback);
    mqttClient.subscribe(mqtt_command_topic);
            Serial.println(" connected.");
            return;
        }
        else
        {
            Serial.print(" failed, rc=");
            Serial.println(mqttClient.state());
            delay(1000);

            if (millis() - startAttempt > maxDuration)
            {
                Serial.println("MQTT short timeout. Giving up for now.");
                return;
            }
        }
    }

}

bool sendMQTTMessage(const char *payload)
{
    connectToMQTT();
    bool success = mqttClient.publish(mqtt_fullflow_topic, payload, true);
    if (success)
    {
        Serial.println("MQTT message sent successfully.");
    }
    else
    {

        Serial.print("MQTT publish failed. State: ");
        Serial.println(mqttClient.state()); // Shows error code
        Serial.print("Payload length: ");
        Serial.println(strlen(payload));
        Serial.println(payload);
    }
    return success;
}

void savePayloadToBuffer(const char *payload)
{
    int head = getIndex("head");
    char key[8];
    sprintf(key, "buf%d", head);

    preferences.begin("buffer", false);
    preferences.putString(key, payload);
    preferences.end();

    head = (head + 1) % BUFFER_SIZE;
    setIndex("head", head);
    Serial.printf("Saved payload to %s\n", key);
}

bool loadPayloadFromBuffer(int index, String &payloadOut)
{
    char key[8];
    sprintf(key, "buf%d", index);

    preferences.begin("buffer", true);
    payloadOut = preferences.getString(key, "");
    preferences.end();

    return payloadOut.length() > 0;
}

void clearPayloadFromBuffer(int index)
{
    char key[8];
    sprintf(key, "buf%d", index);

    preferences.begin("buffer", false);
    preferences.remove(key);
    preferences.end();
}

int getIndex(const char *key)
{
    preferences.begin("bufidx", true);
    int index = preferences.getInt(key, 0);
    preferences.end();
    return index;
}

void setIndex(const char *key, int value)
{
    preferences.begin("bufidx", false);
    preferences.putInt(key, value);
    preferences.end();
}

void retryUnsentPayloads()
{
    int tail = getIndex("tail");
    int head = getIndex("head");

    while (tail != head)
    {
        String payload;
        if (loadPayloadFromBuffer(tail, payload))
        {
            Serial.printf("Retrying buf%d: %s\n", tail, payload.c_str());
            if (sendMQTTMessage(payload.c_str()))
            {
                clearPayloadFromBuffer(tail);
                tail = (tail + 1) % BUFFER_SIZE;
                setIndex("tail", tail);
            }
            else
            {
                break; // Stop if a resend fails
            }
        }
        else
        {
            clearPayloadFromBuffer(tail);
            tail = (tail + 1) % BUFFER_SIZE;
            setIndex("tail", tail);
        }
    }
}

void sendFlowData(
    float max10s_fl, float max1m_fl, float max10m_fl, float total_fl,
    String max10sTimeStamp, String max1mTimeStamp, String max10mTimeStamp,
    String timeStamp,
    int valveStatusDom, int valveModeDom)
{
    StaticJsonDocument<512> doc;

    doc["max10s_fl"] = max10s_fl;
    doc["max1m_fl"] = max1m_fl;
    doc["max10m_fl"] = max10m_fl;
    doc["total_fl"] = total_fl;

    doc["max10sTimeStamp"] = max10sTimeStamp;
    doc["max1mTimeStamp"] = max1mTimeStamp;
    doc["max10mTimeStamp"] = max10mTimeStamp;
    doc["timeStamp"] = timeStamp;

    doc["valveStatusDom"] = valveStatusDom;
    doc["valveModeDom"] = valveModeDom;

    size_t size = measureJson(doc);
    if (size > 512)
    {
        Serial.printf("JSON size %d too large for buffer!\n", size);
        return;
    }

    char payload[512];
    if (serializeJson(doc, payload, sizeof(payload)) == 0)
    {
        Serial.println("Failed to serialize sendFlowData JSON");
        return;
    }

    Serial.println("[MQTT] Sending FlowData:");
    serializeJsonPretty(doc, Serial);
    Serial.println();

    if (!sendMQTTMessage(payload))
    {
        savePayloadToBuffer(payload);
    }
}

void sendSimpleFlowData(int warning)
{

    StaticJsonDocument<384> doc;

    doc["flow10s"] = flow10s;
    doc["flow30s"] = flowAvgValue;
    doc["valveStatus"] = valveStatus;
    doc["runTime"] = waterRunDurSec;
    doc["valveMode"] = statusMonitor;
    doc["warning"] = warning;
    doc["timeStamp"] = getDateTimeMin();

    size_t size = measureJson(doc);
    if (size > 384)
    {
        Serial.printf("JSON size %d too large for buffer!\n", size);
        return;
    }

    char payload[384];
    if (serializeJson(doc, payload, sizeof(payload)) == 0)
    {
        Serial.println("Failed to serialize sendSimpleFlowData JSON");
        return;
    }

    Serial.println("[MQTT] Sending SimpleFlowData:");
    serializeJsonPretty(doc, Serial);
    Serial.println();

    connectToMQTT();
    bool success = mqttClient.publish(mqtt_simpleflow_topic, payload, true);

    if (success)
    {
        Serial.println("Simple flow MQTT message sent successfully.");
    }
    else
    {
        Serial.println("Simple flow MQTT publish failed.");
        Serial.println(mqttClient.state()); // Shows error code
                                            // Optional: buffer this message too if needed
    }
}

#endif