#include "config.h"
#include <LittleFS.h>

SystemConfig config = {
    CONFIG_STRUCT_VERSION,
    true,                       // Default to DHCP
    IPAddress(0, 0, 0, 0),
    IPAddress(255, 255, 255, 0),
    IPAddress(0, 0, 0, 0),
    IPAddress(0, 0, 0, 0),
    26.7f,                      // tMin (C)[cite: 2]
    37.8f,                      // tMax (C)[cite: 2]
    true,                       // isFahrenheit[cite: 2]
    -5,                         // tzOffset[cite: 2]
    true,                       // is24Hour[cite: 2]
    "pico_fan_controller",
    "",                         // HA Token
    "192.168.1.100",
    8123
};

bool mountStorage() {
    // Earle Philhower core uses LittleFS directly on flash
    if (!LittleFS.begin()) {
        Serial.println("Formatting LittleFS partition...");
        LittleFS.format();
        return LittleFS.begin();
    }
    return true;
}

void loadSettings() {
    if (!LittleFS.exists("/settings.bin")) {
        Serial.println("No settings found. Using dynamic defaults.");
        return;
    }
    File f = LittleFS.open("/settings.bin", "r");
    if (!f) return;

    SystemConfig loaded;
    if (f.read((uint8_t*)&loaded, sizeof(loaded)) == sizeof(loaded)) {
        if (loaded.configVersion == CONFIG_STRUCT_VERSION) {
            config = loaded;
            Serial.println("Settings loaded successfully.");
        }
    }
    f.close();
}

void saveSettings() {
    File f = LittleFS.open("/settings.bin", "w");
    if (f) {
        f.write((uint8_t*)&config, sizeof(config));
        f.close();
        Serial.println("Settings saved to flash.");
    }
}