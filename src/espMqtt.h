#ifndef MY_ESPMQTT_H
#define MY_ESPMQTT_H

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_task_wdt.h"
#include "mySecrets.h"

// === Globals ===
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;

// === Config Constants ===
#define RETRY_INTERVAL 120000UL
#define BUFFER_SIZE 24
#define CUSTOM_MQTT_KEEPALIVE 60
const unsigned long mqttReconnectIntervalMS = 60000UL;
const unsigned long mqttPublishRetryDelayMS = 30000UL;

unsigned long lastMQTTConnectAttempt = 0;
unsigned long lastMQTTPublishFail = 0;
unsigned long lastRetryTime = 0;
unsigned long lastSend = 0;
bool isCyclingValve = false;

const char *mqtt_server = MQTT_SERVER;
const char *mqtt_lwt_message = "offline";
const char *mqtt_online_message = "online";

// === Topic Setup ===
String topicBaseStr        = String(TOPIC_BASE_STR);
String mqttClientBase      = String(MQTT_CLIENT_ID);
String topic_fullflow_str  = topicBaseStr + mqttClientBase + "/flowData";
String topic_simpleflow_str= topicBaseStr + mqttClientBase + "/simpleFlowData";
String topic_lwt_str       = topicBaseStr + mqttClientBase + "/status";
String topic_command_str   = topicBaseStr + mqttClientBase + "/cmdSend";
String topic_ack_str       = topicBaseStr + mqttClientBase + "/Ack";

const char *mqtt_fullflow_topic  = topic_fullflow_str.c_str();
const char *mqtt_simpleflow_topic = topic_simpleflow_str.c_str();
const char *mqtt_lwt_topic       = topic_lwt_str.c_str();
const char *mqtt_command_topic   = topic_command_str.c_str();
const char *mqtt_ack_topic       = topic_ack_str.c_str();

// === Externs ===
extern float flow10s, flowAvgValue;
extern bool valveClosed;
extern unsigned long waterRunDurSec;
extern int statusMonitor;
extern void closeValve(), openValve(), cycleValve(), saveVolumeToPrefs();
extern void setValveMode(int);
extern void connectToWiFi();
extern String getTimeString(String key);

// === Function Declarations ===
void connectToMQTT();
bool sendMQTTMessage(const char *payload);
void savePayloadToBuffer(const char *payload);
bool loadPayloadFromBuffer(int index, String &payloadOut);
void clearPayloadFromBuffer(int index);
int getIndex(const char *key);
void setIndex(const char *key, int value);
void retryUnsentPayloads();
void sendFlowData(float, float, float, float, String, String, String, String, int, int);
void sendSimpleFlowData(int warning);
void reconnectIfNeeded();

// === Acknowledgement Send ===
void sendAck(const char *cmd, const char *status) {
    StaticJsonDocument<128> ack;
    ack["cmd"] = cmd;
    ack["status"] = status;
    ack["timeStamp"] = getTimeString("DateTimeMin");

    char response[128];
    serializeJson(ack, response);
    mqttClient.publish(mqtt_ack_topic, response, true);
    Serial.printf("Acknowledgment sent: %s\n", response);
}

// === Command Callback Handler ===
void mqttCallback(char *topic, byte *payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    Serial.printf("Message received on topic %s: %s\n", topic, msg.c_str());

    if (String(topic) != mqtt_command_topic) return;

    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, msg)) {
        Serial.println("Failed to parse JSON command.");
        return;
    }

    const char *cmd = doc["cmd"];
    if (!cmd) return;

    if (strcmp(cmd, "close_valve") == 0)         closeValve();
    else if (strcmp(cmd, "open_valve") == 0)     openValve();
    else if (strcmp(cmd, "cycle_valve") == 0) {
        if (isCyclingValve) {
            sendAck("cycle_valve", "already_running");
            return;
        }
        isCyclingValve = true;
        cycleValve();
        isCyclingValve = false;
    }
    else if (strcmp(cmd, "Status0") == 0)         setValveMode(0);
    else if (strcmp(cmd, "Status1") == 0)         setValveMode(1);
    else if (strcmp(cmd, "Status2") == 0)         setValveMode(2);
    else {
        Serial.printf("Unknown command: %s\n", cmd);
        sendAck(cmd, "unknown");
        return;
    }

    sendAck(cmd, "received");
}

// === MQTT Connect ===
void connectToMQTT() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("MQTT skipped: WiFi not connected");
        return;
    }

    if (mqttClient.connected()) {
        Serial.println("MQTT already connected.");
        return;
    }

    if (millis() - lastMQTTConnectAttempt < mqttReconnectIntervalMS) {
        Serial.println("MQTT reconnect throttled.");
        return;
    }

    Serial.print("Connecting to MQTT...");
    lastMQTTConnectAttempt = millis();

    mqttClient.setBufferSize(512);
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, mqtt_lwt_topic, 1, true, mqtt_lwt_message)) {
        mqttClient.publish(mqtt_lwt_topic, mqtt_online_message, true);
        mqttClient.setCallback(mqttCallback);
        mqttClient.subscribe(mqtt_command_topic);
        Serial.println(" connected.");
    } else {
        Serial.print(" failed, rc=");
        Serial.println(mqttClient.state());
        delay(1000);
    }
}

// === Send MQTT Message ===
bool sendMQTTMessage(const char *payload) {
    connectToMQTT();
    if (!mqttClient.connected()) {
        Serial.println("MQTT not connected. Skipping send.");
        return false;
    }

    if (millis() - lastMQTTPublishFail < mqttPublishRetryDelayMS) {
        Serial.println("MQTT publish throttled.");
        return false;
    }

    bool success = mqttClient.publish(mqtt_fullflow_topic, payload, true);
    if (success) {
        Serial.println("MQTT message sent.");
    } else {
        Serial.print("MQTT publish failed. State: ");
        Serial.println(mqttClient.state());
        lastMQTTPublishFail = millis();
    }
    return success;
}

// === Buffer Management ===
void savePayloadToBuffer(const char *payload) {
    int head = getIndex("head");
    char key[8];
    sprintf(key, "buf%d", head);

    preferences.begin("buffer", false);
    preferences.putString(key, payload);
    preferences.end();

    setIndex("head", (head + 1) % BUFFER_SIZE);
    Serial.printf("Saved payload to %s\n", key);
}

bool loadPayloadFromBuffer(int index, String &payloadOut) {
    char key[8];
    sprintf(key, "buf%d", index);

    preferences.begin("buffer", true);
    payloadOut = preferences.getString(key, "");
    preferences.end();

    return payloadOut.length() > 0;
}

void clearPayloadFromBuffer(int index) {
    char key[8];
    sprintf(key, "buf%d", index);

    preferences.begin("buffer", false);
    preferences.remove(key);
    preferences.end();
}

int getIndex(const char *key) {
    preferences.begin("bufidx", true);
    int index = preferences.getInt(key, 0);
    preferences.end();
    return index;
}

void setIndex(const char *key, int value) {
    preferences.begin("bufidx", false);
    preferences.putInt(key, value);
    preferences.end();
}

// === Retry Buffer ===
void retryUnsentPayloads() {
    int tail = getIndex("tail");
    int head = getIndex("head");

    while (tail != head) {
        String payload;
        if (loadPayloadFromBuffer(tail, payload)) {
            Serial.printf("Retrying buf%d: %s\n", tail, payload.c_str());
            if (sendMQTTMessage(payload.c_str())) {
                clearPayloadFromBuffer(tail);
                setIndex("tail", (tail + 1) % BUFFER_SIZE);
                tail = getIndex("tail");
            } else {
                break;
            }
        } else {
            clearPayloadFromBuffer(tail);
            setIndex("tail", (tail + 1) % BUFFER_SIZE);
            tail = getIndex("tail");
        }
    }
}

// === Flow Data Publishing ===
void sendFlowData(float max10s_fl, float max1m_fl, float max10m_fl, float total_fl,
                  String max10sTimeStamp, String max1mTimeStamp, String max10mTimeStamp,
                  String timeStamp, int valveStatusDom, int valveModeDom) {
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

    char payload[512];
    if (serializeJson(doc, payload, sizeof(payload)) == 0) {
        Serial.println("FlowData serialization failed");
        return;
    }

    Serial.println("[MQTT] Sending FlowData:");
    serializeJsonPretty(doc, Serial);
    Serial.println();

    if (!sendMQTTMessage(payload)) {
        savePayloadToBuffer(payload);
    }
}

// === Simple Flow Data ===
void sendSimpleFlowData(int warning) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("SimpleFlow skipped: no WiFi.");
        return;
    }

    StaticJsonDocument<384> doc;
    doc["flow10s"] = flow10s;
    doc["flow30s"] = flowAvgValue;
    doc["valveClosed"] = valveClosed;
    doc["runTime"] = waterRunDurSec;
    doc["valveMode"] = statusMonitor;
    doc["warning"] = warning;
    doc["timeStamp"] = getTimeString("DateTimeMin");

    char payload[384];
    if (serializeJson(doc, payload, sizeof(payload)) == 0) {
        Serial.println("SimpleFlow serialization failed");
        return;
    }

    Serial.println("[MQTT] Sending SimpleFlowData:");
    serializeJsonPretty(doc, Serial);
    Serial.println();

    connectToMQTT();
    if (!mqttClient.connected() || millis() - lastMQTTPublishFail < mqttPublishRetryDelayMS) {
        Serial.println("SimpleFlow send skipped due to connection or throttle.");
        return;
    }

    if (!mqttClient.publish(mqtt_simpleflow_topic, payload, true)) {
        Serial.println("SimpleFlow publish failed.");
        lastMQTTPublishFail = millis();
    } else {
        Serial.println("SimpleFlow MQTT message sent.");
    }
}

// === MQTT Auto Reconnect ===
void reconnectIfNeeded() {
    if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
        connectToMQTT();
    }
}

#endif
