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

// === Warning Topics ===
String topic_warning_str      = "6556/warnings";
String topic_warning_ack_str  = "6556/warnings/ack";
const char *mqtt_warning_topic     = topic_warning_str.c_str();
const char *mqtt_warning_ack_topic = topic_warning_ack_str.c_str();

// === Warning ACK State ===
String pendingWarnID = "";
String pendingWarnPayload = "";
unsigned long pendingWarnSentAt = 0;
int pendingWarnRetries = 0;

const unsigned long WARN_ACK_TIMEOUT_MS = 15000UL; // 15s
const int WARN_MAX_RETRIES = 3;

// === Forward Decls ===
bool sendWarning(const char *wLevel, const char *wMessage, const char *wTitle);
void processWarningAckTick();
void handleWarningAck(const String &msg);


// === Externs ===
extern String max1MinTime, max10SecTime, max10MinTime, max30MinTime;
extern float flow10s, flowAvgValue,max1Min, max10Sec, max10Min, max30Min, volumeAll;
extern bool valveClosed;
extern unsigned long waterRunDurSec;
extern int statusMonitor;
extern void closeValve(), openValve(), cycleValve(), setValveMode(int), saveVolumeToPrefs();

// === Function Declarations ===
int getIndex(const char *key);
void setIndex(const char *key, int value);

// === MQTT Adaptive Backoff ===
unsigned long bootMillis = 0;

const unsigned long MQTT_BACKOFF_MIN_MS   = 2000UL;   // 2s first retry
const unsigned long MQTT_BACKOFF_MAX_MS   = 60000UL;  // cap at 60s (same as your throttle)
const unsigned long MQTT_STARTUP_GRACE_MS = 120000UL; // 2 minutes of faster retries
unsigned long mqttBackoffMs = MQTT_BACKOFF_MIN_MS;
int mqttConsecutiveFails = 0;

// Forward decl
static unsigned long jitter(unsigned long base, uint8_t pct);



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

    // Handle warning ACKs
    if (String(topic) == mqtt_warning_ack_topic) {
        handleWarningAck(msg);
        return;
    }


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
    if (!isWifiReady()) { Serial.println("MQTT skipped: WiFi not ready"); return; }
    if (mqttClient.connected()) { Serial.println("MQTT already connected."); return; }

    unsigned long now = millis();
    if (bootMillis == 0) bootMillis = now;

    unsigned long interval = (now - bootMillis < MQTT_STARTUP_GRACE_MS)
                              ? mqttBackoffMs
                              : max(mqttBackoffMs, mqttReconnectIntervalMS);

    if (now - lastMQTTConnectAttempt < interval) {
        Serial.println("MQTT reconnect throttled.");
        return;
    }

    Serial.print("Connecting to MQTT...");
    lastMQTTConnectAttempt = now;
    mqttClient.setBufferSize(512);

    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, mqtt_lwt_topic, 1, true, mqtt_lwt_message)) {
        mqttBackoffMs = MQTT_BACKOFF_MIN_MS;
        mqttConsecutiveFails = 0;
        mqttClient.publish(mqtt_lwt_topic, mqtt_online_message, true);
        mqttClient.setCallback(mqttCallback);
        mqttClient.subscribe(mqtt_command_topic);
        mqttClient.subscribe(mqtt_warning_ack_topic);
        Serial.println(" connected.");
    } else {
        mqttConsecutiveFails++;
        unsigned long next = min(mqttBackoffMs * 2UL, MQTT_BACKOFF_MAX_MS);
        mqttBackoffMs = jitter(next, 10); // ±10% jitter
        Serial.print(" failed, rc="); Serial.print(mqttClient.state());
        Serial.print(" ; next retry ~"); Serial.print(mqttBackoffMs); Serial.println(" ms");
        delay(250);
    }
}



// Add small randomness so multiple devices don't slam the broker at the same instant
static unsigned long jitter(unsigned long base, uint8_t pct) {
    // pct = 10 means ±10%
    long span = (long)(base * (long)pct / 100L);
    long delta = (long)((int32_t)esp_random() % (2*span + 1)) - span; // [-span, +span]
    long val = (long)base + delta;
    if (val < 0) val = 0;
    return (unsigned long)val;
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
void sendBigData(float volumeTotalSend,
                  String timeStamp) {
    StaticJsonDocument<512> doc;
    doc["max10s_fl"] = max10Sec;
    doc["max1m_fl"] = max1Min;
    doc["max10m_fl"] = max10Min;
    doc["total_fl"] = volumeTotalSend;
    doc["volAll"] = volumeAll;
    doc["max10sTimeStamp"] = max10SecTime;
    doc["max1mTimeStamp"] = max1MinTime;
    doc["max10mTimeStamp"] = max10MinTime;
    doc["timeStamp"] = timeStamp;
    doc["valveStatusDom"] = valveClosed;
    doc["valveModeDom"] = statusMonitor;

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
void sendSimpleData(int warning) {
    if (!isWifiReady()) { Serial.println("SimpleFlow skipped: no WiFi."); return; }


    StaticJsonDocument<384> doc;
    doc["flow10s"] = flow10s;
    doc["flow30s"] = flowAvgValue;
    doc["valveClosed"] = valveClosed;
    doc["runTime"] = waterRunDurSec;
    doc["valveMode"] = statusMonitor;
    doc["volAll"] = volumeAll;
    doc["warning"] = warning;

    String ts = getTimeString("DateTimeMin");
    if (ts.startsWith("0000")) ts = "pending";  // or omit the field
    doc["timeStamp"] = ts;


    char payload[384];
    if (serializeJson(doc, payload, sizeof(payload)) == 0) {
        Serial.println("SimpleFlow serialization failed");
        return;
    }

    Serial.println("[MQTT] Sending SimpleFlowData:");
    serializeJsonPretty(doc, Serial);
    Serial.println();

    connectToMQTT();
    bool inStartup = (bootMillis != 0) && (millis() - bootMillis < MQTT_STARTUP_GRACE_MS);

    // During startup, only require connection (skip the publish cooldown)
    // After startup, keep your normal cooldown guard
    if (!mqttClient.connected() || (!inStartup && (millis() - lastMQTTPublishFail < mqttPublishRetryDelayMS))) {
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
    if (isWifiReady() && !mqttClient.connected()) connectToMQTT();
}


// Build a unique ID using client id + random + time
static String buildWarningID() {
    char rnd[9];
    snprintf(rnd, sizeof(rnd), "%08lx", (unsigned long)esp_random());
    return String(MQTT_CLIENT_ID) + "-" + String(rnd) + "-" + getTimeString("DateTimeMin");
}

// One-shot publish; sets pending state for retry-on-no-ACK
bool sendWarning(const char *wLevel, const char *wMessage, const char *wTitle) {
    connectToMQTT();
    if (!mqttClient.connected()) {
        Serial.println("[WARN] MQTT not connected; cannot send warning.");
        return false;
    }

    // if a previous warning still pending, don't overwrite—retry loop will handle it
    if (pendingWarnID.length() > 0) {
        Serial.println("[WARN] Previous warning still awaiting ACK; skipping new send.");
        return false;
    }

    String wID = buildWarningID();

    StaticJsonDocument<384> doc;
    doc["wLevel"] = wLevel;        // e.g. "info", "warn", "crit"
    doc["wMessage"] = wMessage;    // human-readable description
    doc["wTitle"] = wTitle;        // short title
    doc["wID"] = wID;              // unique id to match ACK
    doc["client"] = MQTT_CLIENT_ID;
    doc["timeStamp"] = getTimeString("DateTimeMin");

    char payload[384];
    if (serializeJson(doc, payload, sizeof(payload)) == 0) {
        Serial.println("[WARN] Warning JSON serialization failed.");
        return false;
    }

    bool ok = mqttClient.publish(mqtt_warning_topic, payload, false);
    if (!ok) {
        Serial.print("[WARN] Publish failed. State: ");
        Serial.println(mqttClient.state());
        return false;
    }

    // Track pending for ACK
    pendingWarnID = wID;
    pendingWarnPayload = payload;
    pendingWarnSentAt = millis();
    pendingWarnRetries = 0;

    Serial.printf("[WARN] Warning sent (wID=%s). Awaiting ACK...\n", pendingWarnID.c_str());
    return true;
}

// Call this frequently (e.g., from your main loop) to enforce the ACK timeout & retry
void processWarningAckTick() {
    if (pendingWarnID.length() == 0) return; // nothing pending

    unsigned long elapsed = millis() - pendingWarnSentAt;
    if (elapsed < WARN_ACK_TIMEOUT_MS) return;

    if (!mqttClient.connected()) connectToMQTT();

    if (pendingWarnRetries >= WARN_MAX_RETRIES) {
        Serial.printf("[WARN] No ACK after %d retries (wID=%s). Giving up.\n",
                      WARN_MAX_RETRIES, pendingWarnID.c_str());
        // Clear pending so future warnings can proceed
        pendingWarnID = "";
        pendingWarnPayload = "";
        pendingWarnRetries = 0;
        return;
    }

    // Retry
    bool ok = mqttClient.publish(mqtt_warning_topic, pendingWarnPayload.c_str(), false);
    pendingWarnRetries++;
    pendingWarnSentAt = millis();
    Serial.printf("[WARN] Retrying warning send (attempt %d of %d) wID=%s ok=%d\n",
                  pendingWarnRetries, WARN_MAX_RETRIES, pendingWarnID.c_str(), ok);
}

// Process an ACK message from Node-RED (or other consumer)
void handleWarningAck(const String &msg) {
    StaticJsonDocument<256> ack;
    if (deserializeJson(ack, msg)) {
        Serial.println("[WARN] Failed to parse warning ACK JSON.");
        return;
    }

    const char *ackID = ack["wID"] | "";
    const char *status = ack["status"] | "unknown";
    const char *receiver = ack["receiver"] | "n/a";

    if (pendingWarnID.length() == 0) {
        Serial.println("[WARN] Received ACK but no warning was pending.");
        return;
    }

    if (pendingWarnID != String(ackID)) {
        Serial.printf("[WARN] ACK wID mismatch (expected %s, got %s). Ignoring.\n",
                      pendingWarnID.c_str(), ackID);
        return;
    }

    Serial.printf("[WARN] ACK received for wID=%s status=%s receiver=%s\n",
                  ackID, status, receiver);

    // Clear pending now that we've confirmed delivery
    pendingWarnID = "";
    pendingWarnPayload = "";
    pendingWarnRetries = 0;
}


#endif
