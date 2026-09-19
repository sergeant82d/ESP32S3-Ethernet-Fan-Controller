#include "home_assistant.h"
#include "config.h"
#include "sensors.h"
#include <Ethernet.h>
#include <ArduinoJson.h>

void postTelemetryToHomeAssistant() {
    if (strlen(config.haHost) == 0 || strlen(config.haToken) == 0) return;

    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) {
        Serial.println("HA connection failed.");
        return;
    }

    JsonDocument doc;
    doc["state"] = String(localTempC, 1);
    
    JsonObject attrs = doc["attributes"].to<JsonObject>();
    attrs["unit_of_measurement"] = config.isFahrenheit ? "\xC2\xB0\x46" : "\xC2\xB0\x43";
    attrs["temperature"] = String(localTempC, 1);
    attrs["fan1_rpm"]    = currentRPMs[0];
    attrs["fan2_rpm"]    = currentRPMs[1];

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    client.println("POST /api/states/sensor." + String(config.nodeID) + " HTTP/1.1");
    client.println("Host: " + String(config.haHost));
    client.println("Authorization: Bearer " + String(config.haToken));
    client.println("Content-Type: application/json");
    client.print("Content-Length: "); client.println(jsonPayload.length());
    client.println("Connection: close\r\n");
    client.println(jsonPayload);

    unsigned long start = millis();
    while (client.connected() && millis() - start < 1000) {
        if (client.available()) {
            client.stop();
            break;
        }
    }
    client.stop();
}