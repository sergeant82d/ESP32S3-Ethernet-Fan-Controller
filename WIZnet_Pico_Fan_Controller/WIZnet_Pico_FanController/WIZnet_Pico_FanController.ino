#include "pins.h"
#include "config.h"
#include "sensors.h"
#include "display.h"
#include "network.h"
#include "home_assistant.h"
#include "web_server.h"
#include "SPI.h"
#include "Wire.h"

static byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x02, 0x01 };

void setup() {
    Serial.begin(115200);
    delay(1000);

//    mountStorage();
    loadSettings();

    // 1. Initialize Display first (SPI1)
    displayInit();

    // 2. Initialize Hardware PWM and DS18B20
    sensorsInit();

    // 3. Initialize Hardwired W5500/W6100 (SPI0)
    pinMode(W5500_RST, OUTPUT);
    digitalWrite(W5500_RST, LOW);
    delay(50);
    digitalWrite(W5500_RST, HIGH);
    delay(100);

    // Configure SPI0 for Ethernet
    SPI.setSCK(W5500_SCK);
    SPI.setTX(W5500_MOSI);
    SPI.setRX(W5500_MISO);
    SPI.setCS(W5500_CS);
    Ethernet.init(W5500_CS);

    Serial.println("Acquiring network address...");
    if (config.useDHCP) {
        if (Ethernet.begin(mac) == 0) {
            Serial.println("DHCP Failed! Fallback to static 192.168.1.50");
            Ethernet.begin(mac, IPAddress(192, 168, 1, 50));
        }
    } else {
        Ethernet.begin(mac, config.ip, config.dns, config.gateway, config.subnet);
    }

    server.begin();
    updateDashboardUI();
}

void loop() {
    // Bucket 1: 1000ms Sensor Sampling & RPM math
    static unsigned long lastSensor = 0;
    if (millis() - lastSensor >= 1000) {
        unsigned long elapsed = millis() - lastSensor;
        lastSensor = millis();
        sampleSensors();
        calculateFanSpeeds();
        calculateRPMs(elapsed);
    }

    // Bucket 2: 1000ms Dashboard Screen Redraw
    static unsigned long lastDisplay = 0;
    if (millis() - lastDisplay >= 1000) {
        lastDisplay = millis();
        updateDashboardUI();
    }

    // Bucket 3: Continuous Web traffic
    EthernetClient client = server.available();
    if (client) {
        handleNativeWebTraffic(client);
    }

    // Bucket 4: 2000ms HA Push
    static unsigned long lastHA = 0;
    if (millis() - lastHA >= REFRESH_PERIOD_MS) {
        lastHA = millis();
        postTelemetryToHomeAssistant();
    }
}