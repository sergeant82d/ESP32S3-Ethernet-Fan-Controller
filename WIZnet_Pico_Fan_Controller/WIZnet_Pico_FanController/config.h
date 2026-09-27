#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>

#define CONFIG_STRUCT_VERSION 10

struct SystemConfig {
    uint32_t configVersion;
    bool useDHCP;           // Default: true (Zero hardcoded networking)
    IPAddress ip;
    IPAddress subnet;
    IPAddress gateway;
    IPAddress dns;
    
    // Core parameters
    float tMin;
    float tMax;
    bool isFahrenheit;
    int tzOffset;
    bool is24Hour;
    
    // Reduced Home Assistant Integration
    char nodeID[64];
    char haToken[256];
    char haHost[64];
    int haPort;
};

extern SystemConfig config;

void loadSettings();
void saveSettings();
bool mountStorage();

void sampleSensors();
void calculateFanSpeeds();
void postTelemetryToHomeAssistant();

#endif // CONFIG_H