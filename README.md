# childFlowValveActive
child board monitoring flow, controlling valve and keeping active info



Domestic Flow Monitor:
#define MQTT_CLIENT_ID "domesticSupplyFlow"
#define TOPIC_BASE_STR "6556/controller/water/domestic/"
#define MQTT_SERVER "10.140.1.95"
#define MQTT_USER "6556mqtt"
#define MQTT_PASS "123456"


Irrigation Bypass Flow Monitor:
#define MQTT_CLIENT_ID "irrigationBypassFlow"
#define TOPIC_BASE_STR "6556/controller/water/irrigation/"
#define MQTT_SERVER "10.140.1.95"
#define MQTT_USER "6556mqtt"
#define MQTT_PASS "123456"




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