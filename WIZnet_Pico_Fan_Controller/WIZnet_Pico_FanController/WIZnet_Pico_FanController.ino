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

    mountStorage();
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
// Configure SPI0 for Ethernet on W5500-EVB-Pico
SPI.setSCK(W5500_SCK);
SPI.setTX(W5500_MOSI);
SPI.setRX(W5500_MISO);
SPI.setCS(W5500_CS);
SPI.begin();

Ethernet.init(W5500_CS);

    Serial.println("Acquiring network address...");
    if (config.useDHCP) {
        if (Ethernet.begin(mac) == 0) {
            Serial.println("DHCP Failed! Fallback to static 192.168.1.50");
            Ethernet.begin(mac, IPAddress(192, 168, 1, 50));
        }
    } else {
        // Fall back to standard defaults if gateway or subnet are 0.0.0.0
        IPAddress gw = (config.gateway == IPAddress(0, 0, 0, 0)) ? IPAddress(192, 168, 1, 1) : config.gateway;
        IPAddress sn = (config.subnet == IPAddress(0, 0, 0, 0)) ? IPAddress(255, 255, 255, 0) : config.subnet;
        IPAddress dns = (config.dns == IPAddress(0, 0, 0, 0)) ? gw : config.dns;

        Ethernet.begin(mac, config.ip, dns, gw, sn);
    }

    delay(200); // Allow PHY and socket registers to stabilize
    server.begin();
    Serial.print("Web server active at: http://");
    Serial.println(Ethernet.localIP());

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
        Serial.print("IP Address: "); 
        Serial.println(Ethernet.localIP());
    }

}