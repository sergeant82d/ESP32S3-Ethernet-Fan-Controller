

// 🌟 Ensure these match your prototypes perfectly at the top of your sensor tab file
// 🌟 FIXED: Removed duplicate IRAM_ATTR keyword here to eliminate the linker layout warnings
void przypisISR0() { tachCounts[0] += 1; } 
void przypisISR1() { tachCounts[1] += 1; }
void przypisISR2() { tachCounts[2] += 1; }
void przypisISR3() { tachCounts[3] += 1; }

void initFanHardwarePWM() {
  for (int i = 0; i < 4; i++) {
      ledcAttach(pwmPins[i], 25000, 8); 
      pinMode(tachPins[i], INPUT_PULLUP); 
  }
  if (config.fanCount >= 1) attachInterrupt(digitalPinToInterrupt(tachPins[0]), przypisISR0, FALLING);
  if (config.fanCount >= 2) attachInterrupt(digitalPinToInterrupt(tachPins[1]), przypisISR1, FALLING);
}

void runTemperatureSamplingBucket() {
  static unsigned long lastThermalSample = 0;
  if (millis() - lastThermalSample >= 1000) {
      lastThermalSample = millis();
      sensors.requestTemperatures();
      float rawLocal = sensors.getTempCByIndex(0);
      if (rawLocal > -50.0 && rawLocal != 85.0 && rawLocal != -127.0) {
          localTempC = rawLocal; localSensorHealthy = true;
      } else { localSensorHealthy = false; }
      
      evaluateSensorFailsafes();
  }
}

void runTachometerCalculationBucket() {
  static unsigned long lastRPMCalcTime = 0;
  if (millis() - lastRPMCalcTime >= 1000) { 
      unsigned long timeElapsed = millis() - lastRPMCalcTime;
      lastRPMCalcTime = millis();
      for (int i = 0; i < 4; i++) {
                    if (i < config.fanCount) {
              // 🌟 FIXED: Safe, compiler-native atomic interrupt brackets
              portDISABLE_INTERRUPTS(); 
              unsigned long pulses = tachCounts[i];
              tachCounts[i] = 0; 
              portENABLE_INTERRUPTS();

              if (pulses > 0 && timeElapsed > 0) {
                  currentRPMs[i] = (pulses * 60000) / (2 * timeElapsed);
              } else {
                  currentRPMs[i] = 0;
              }
          }

      }
  }
}

// Drop your calculateFanCurve() and evaluateSensorFailsafes() right here!

// --- Failsafe Sensor Arbitration Logic ---
void evaluateSensorFailsafes() {
    if (localSensorHealthy && networkSensorHealthy) {
        blendedAverageC = (localTempC + networkTempC) / 2.0;
        calculateFanCurve(blendedAverageC);
        totalAlertSent = false;
    }
    else if (localSensorHealthy && !networkSensorHealthy) {
        blendedAverageC = localTempC;
        calculateFanCurve(blendedAverageC);
        if (!networkAlertSent) {
            sendHomeAssistantVoiceAlert("Network temperature sensor offline. Falling back to local hardware probe.");
            networkAlertSent = true;
        }
    }
    else if (!localSensorHealthy && networkSensorHealthy) {
        blendedAverageC = networkTempC;
        calculateFanCurve(blendedAverageC);
        if (!localAlertSent) {
            sendHomeAssistantVoiceAlert("Warning. Local hardware temperature probe error. Falling back to network sensor data.");
            localAlertSent = true;
        }
    }
    else {
        // Total Sensor Blackout Failsafe
        for (int i = 0; i < NUM_FANS; i++) {
            currentDutyCycles[i] = 255;
            ledcWrite(pwmPins[i], 255);
        }
        if (!totalAlertSent) {
            sendHomeAssistantVoiceAlert("Critical Alert! All thermal sensors have failed. Fan system locked to maximum emergency power.");
            totalAlertSent = true;
        }
    }
}


// --- Fan Control Automation Core ---
void calculateFanCurve(float targetTemp) {
    int targetDuty = 0;
    
    // Shield constraint completely prevents divide-by-zero if limits are identical or uninitialized
    if (config.tMax <= config.tMin) {
        targetDuty = 255; // Secure safety override to full power if thresholds are corrupt
    } else {
        if (targetTemp < config.tMin) {
            targetDuty = 0; 
        } else if (targetTemp >= config.tMax) {
            targetDuty = 255; 
        } else {
            // Dynamically scale from FAN_MIN_DUTY to 255
            float rangeFraction = (targetTemp - config.tMin) / (config.tMax - config.tMin);
            targetDuty = (int)(FAN_MIN_DUTY + (rangeFraction * (255.0f - (float)FAN_MIN_DUTY)));
        }
    }

    // Pushes the 25 kHz clock signal straight to physical hardware pins
    for (int i = 0; i < 4; i++) {
        if (i < config.fanCount) {
            currentDutyCycles[i] = targetDuty;
            ledcWrite(pwmPins[i], targetDuty); 
        } else {
            currentDutyCycles[i] = 0;
            ledcWrite(pwmPins[i], 0); 
        }
    }
}



void parseIpString(String ipStr, IPAddress &ip) {
  int p0 = 0, p1 = 0, p2 = 0, p3 = 0;
  if (sscanf(ipStr.c_str(), "%d.%d.%d.%d", &p0, &p1, &p2, &p3) == 4) {
    ip = IPAddress(p0, p1, p2, p3);
  } else {
    Serial.println("Warning: Failed to parse IP address string layout!");
  }
}



