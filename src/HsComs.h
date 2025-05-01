#ifndef MY_HSCOMS_H
#define MY_HSCOMS_H

#include "mySecrets.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>

// My Homeseer Setup
const String hsUser = HS_USER;       // Homeseer User
const String hsPass = HS_PASS;       // Homeseer Password
const String baseLink = HS_BASELINK; // Homeseer Link

// Define GET variables
const String varNames[] = {"request", "location2", "user", "pass"};
const String varValuesDom[] = {"getstatus", "Domestic%20Water", hsUser, hsPass};
const String varValuesIrr[] = {"getstatus", "Irrigation%20Water", hsUser, hsPass};


// Keys for domestic items
const int refKeysDom[] = {480, 484, 486, 490};
const String refNamesDom[] = {"Water Valve Domestic", "Water Flow Domestic", "Water Tank Raw", "Water Tank Potable"};
int numKeysDom = sizeof(refKeysDom) / sizeof(refKeysDom[0]);

// Keys for irrigation items
const int refKeysIrr[] = {482, 466, 488};
const String refNamesIrr[] = {"Water Valve Irrigation", "Water Flow Irrigation", "Water Tank Irrigation"};
int numKeysIrr = sizeof(refKeysIrr) / sizeof(refKeysIrr[0]);


String constructUrl(String baseLink, const String *varNames, const String *varValues, int numVars)
{
  String url = baseLink;
  if (numVars > 0)
  {
    url += "?";
    for (int i = 0; i < numVars; i++)
    {
      url += varNames[i] + "=" + varValues[i];
      if (i < numVars - 1)
      {
        url += "&";
      }
    }
  }
  Serial.println("GET/SEND Link:");
  Serial.println(url);
  return url;
}

String sendRequest(String baseLink, const String *varNames, const String *varValues, int numVars)
{

  String url = constructUrl(baseLink, varNames, varValues, numVars);

  HTTPClient http;

  http.begin(url);
  int httpResponseCode = http.GET();

  String payload;
  if (httpResponseCode > 0)
  {
    payload = http.getString();
  }
  else
  {
    payload = "Error: Unable to connect";
  }
  http.end();
  return payload;
}

// Function to send values to HS
void sendHS(String baseLink, float sendValue[], const int refKeys[], int numKeys)
{
  
  String sendNames[] = {"request", "ref", "value", "user", "pass"};
  Serial.print("numKeys:");
  Serial.println(numKeys);

  for (int i = 0; i < numKeys; i++)
  {
    String sendValSend[] = {"controldevicebyvalue", String(refKeys[i]), String(sendValue[i]), hsUser, hsPass};
    String payload = sendRequest(baseLink, sendNames, sendValSend, 5);
    
    Serial.println("Response:");
    Serial.println(payload);
  }
  

}

// Function to parse JSON and extract values
bool parseJsonAndExtractValues(const String &json, const int refKeys[], int numKeys, float outputValues[])
{
  StaticJsonDocument<8192> doc;

  DeserializationError error = deserializeJson(doc, json);
  if (error)
  {
    Serial.print("JSON Parse Error: ");
    Serial.println(error.f_str());
    return false;
  }

  if (!doc.containsKey("Devices"))
  {
    Serial.println("JSON missing 'Devices' key.");
    return false;
  }

  JsonArray devices = doc["Devices"].as<JsonArray>();

  // Initialize outputValues with NaN
  for (int i = 0; i < numKeys; i++)
  {
    outputValues[i] = NAN;
  }

  // Extract values
  for (JsonObject device : devices)
  {
    int ref = device["ref"];
    for (int i = 0; i < numKeys; i++)
    {
      if (ref == refKeys[i])
      {
        outputValues[i] = device["value"].as<float>();
        break;
      }
    }
  }
  return true;
}

// Function to process water system, send request, and parse response
bool processWaterSystem(const String &base, const String *varNames, const String *varValues,
                        const int *refKeys, const String *refNames, int numKeys, const String &type, float outputValues[])
{
  Serial.println("\n--- Processing " + type + " Water System ---");

  // Send request and get response
  String response = sendRequest(base, varNames, varValues, 4);
  // Serial.println("Response: " + response);

  // Parse JSON and extract values
  if (parseJsonAndExtractValues(response, refKeys, numKeys, outputValues))
  {
    Serial.println("Extracted Values:");
    for (int i = 0; i < numKeys; i++)
    {
      Serial.print(refKeys[i]);
      Serial.print(" - ");
      Serial.print(refNames[i]);
      Serial.print(": ");
      Serial.println(isnan(outputValues[i]) ? "Not Found" : String(outputValues[i], 2));
    }
    return true;
  }
  else
  {
    Serial.println("Failed to parse JSON!");
    return false;
  }
}


#endif