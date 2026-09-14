

void runHomeAssistantSyncBucket() {
  static unsigned long lastHAUpdate = 0;
  if (millis() - lastHAUpdate >= REFRESH_PERIOD) { 
    lastHAUpdate = millis();
    fetchHomeAssistantTemperature();
  }
}

// Drop your working fetchHomeAssistantTemperature(), sendHomeAssistantVoiceAlert(), 
// and sendHomeAssistantVoiceClear() functions right here!

// --- Outbound Voice API Push Engine ---
void sendHomeAssistantVoiceAlert(String alertMessage) {
    EthernetClient client;
    if (client.connect(config.haHost, config.haPort)) {
        JsonDocument doc;
        doc["title"] = "Hardware Fault Alert";
        doc["message"] = alertMessage;
        doc["notification_id"] = "FAN_CTRL_ERROR";
        String jsonPayload;
        serializeJson(doc, jsonPayload);

        client.println("POST /api/services/persistent_notification/create HTTP/1.1");
        client.print("Host: "); client.println(config.haHost);
        client.print("Authorization: "); client.println(config.haToken);
        client.println("Content-Type: application/json");
        client.print("Content-Length: "); client.println(jsonPayload.length());
        client.println("Connection: close\r\n");
        client.println(jsonPayload);
        client.stop();
        Serial.println("Voice alert posted to Home Assistant.");
    }
}

void sendHomeAssistantVoiceClear(String sensorType) {
    EthernetClient client;
    if (client.connect(config.haHost, config.haPort)) {
        JsonDocument doc;
        doc["title"] = "Hardware Status Normal";
        doc["message"] = "The " + sensorType + " thermal sensor connection has recovered. System operating normally.";
        doc["notification_id"] = "FAN_CTRL_CLEAR";
        String jsonPayload;
        serializeJson(doc, jsonPayload);

        client.println("POST /api/services/persistent_notification/create HTTP/1.1");
        client.print("Host: "); client.println(config.haHost);
        client.print("Authorization: "); client.println(config.haToken);
        client.println("Content-Type: application/json");
        client.print("Content-Length: "); client.println(jsonPayload.length());
        client.println("Connection: close\r\n");
        client.println(jsonPayload);
        client.stop();
        Serial.println("Voice recovery update posted to Home Assistant.");
    }
}



void fetchHomeAssistantTemperature() {
  EthernetClient client;
  
  Serial.print("Attempting outbound link connection to HA (");
  Serial.print(config.haHost); Serial.print(":"); Serial.print(config.haPort); Serial.println(")...");

  if (client.connect(config.haHost, config.haPort)) {
    Serial.println(" -> Success! Connected to HA port. Delivering POST JSON...");
    
    JsonDocument outboundDoc;
    outboundDoc["state"] = String(localTempC, 1); 
    
    // JsonObject attrs = outboundDoc.createNestedObject("attributes");
    // 🌟 FIXED: Modern ArduinoJson v7 syntax to clear the compiler warning
    JsonObject attrs = outboundDoc["attributes"].to<JsonObject>(); 
    attrs["device_class"] = "temperature";
    attrs["unit_scale"] = config.isFahrenheit ? "Fahrenheit" : "Celsius";
    attrs["fans_configured"] = config.fanCount;
    attrs["unit_of_measurement"] = "\xC2\xB0\x43"; // "°C"
    
    attrs["local_hardware_probe"] = String(localTempC, 1);
    attrs["ha_network_probe"]    = String(networkTempC, 1);
    attrs["blended_average"]     = String(blendedAverageC, 1);
    
    for (int i = 0; i < 4; i++) {
        String rpmKey = "fan" + String(i + 1) + "_rpm";
        String faultKey = "fan" + String(i + 1) + "_fault";
        if (i < config.fanCount) {
            attrs[rpmKey] = currentRPMs[i];
            attrs[faultKey] = (currentDutyCycles[i] > 51 && currentRPMs[i] == 0);
        } else {
            attrs[rpmKey] = 0;
            attrs[faultKey] = false; 
        }
    }

    String jsonPayload;
    serializeJson(outboundDoc, jsonPayload);

    String postRoute = "POST /api/states/sensor." + String(config.nodeID) + " HTTP/1.1";
    client.println(postRoute);
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.print("Content-Length: "); client.println(jsonPayload.length());
    client.println("Connection: keep-alive"); 
    client.println(); 
    client.println(jsonPayload);
    
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") break;
    }
    
    Serial.println(" -> POST acknowledged. Running consecutive GET request...");

    // 🌟 FIXED: Explicitly declared the missing String container for getRoute 
    // It now incorporates your new dynamic webpage text variable (config.haSensor) smoothly
    String getRoute = "GET /api/states/sensor." + String(config.haSensor) + " HTTP/1.1"; 
    client.println(getRoute);
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.println("Connection: close"); 
    client.println(); 
    
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") break;
    }
    
    String payload = "";
    while (client.available()) {
        payload += (char)client.read();
    }
    client.stop(); 

    JsonDocument inboundDoc;
    DeserializationError error = deserializeJson(inboundDoc, payload);
    
    if (!error) {
      String haStateStr = inboundDoc["state"].as<String>();
      
      if (haStateStr != "unknown" && haStateStr != "unavailable" && haStateStr.length() > 0) {
          float ha_val = haStateStr.toFloat();
          String haUnit = inboundDoc["attributes"]["unit_of_measurement"].as<String>();
          
          if (haUnit.indexOf("F") != -1) {
              networkTempC = (ha_val - 32.0) * 5.0 / 9.0; 
          } else {
              networkTempC = ha_val; 
          }
          
          if (!networkSensorHealthy && networkAlertSent) { sendHomeAssistantVoiceClear("network"); }
          networkSensorHealthy = true;
          networkAlertSent = false;
          Serial.print(" -> Sync Success! HA Sensor parsed to Celsius: "); Serial.println(networkTempC);
      } else {
          networkSensorHealthy = false;
      }
    } else {
      networkSensorHealthy = false; 
    }
    Serial.println(" -> Bidirectional HA transactions completed safely.");
  } 
  else {
    Serial.println(" -> ❌ CONNECTION ERROR: HA host timed out.");
    networkSensorHealthy = false;
  }
}