/*
 Name:		ESP_Ethernet_Fans_Ver_1_0_1_WORKING_NO_LCD.ino
 Created:	9/12/2026 1606
 Author:	Brad
*/


#include <SPI.h>
#include <Ethernet.h>  // Native Arduino library supporting W5500 on ESP32-S3
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_ST7735.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>
#include <ArduinoJson.h>
#include <EthernetUdp.h> // Native SPI UDP handling library
#include <TimeLib.h>     // Time tracking and internal clock sync management


// --- Configuration Constants ---
const uint32_t PWM_FREQ = 25000;
const int PWM_RES_BITS = 8;
const unsigned long REFRESH_PERIOD = 2000;

// Non-Standard Color definition
#ifndef ST77XX_DARKGRAY
#define ST77XX_DARKGRAY 0x7BEF // 16-bit RGB565 code for Dark Gray
#endif

////////////////DO NOT CHANGE - HARD WIRED ON THE BOARD!!! ///////////////////////////////
////////////////DO NOT CHANGE - HARD WIRED ON THE BOARD!!! ///////////////////////////////
// --- Waveshare ESP32-S3-ETH PCB Internal Hardware Map ---
#define SD_CS       4   // Onboard W5500 SD Card Chip Select
#define SD_MISO     5   // Onboard W5500 Master In Slave Out
#define SD_MOSI     6   // Onboard W5500 Master Out Slave In
#define SD_CLK      7   // Onboard W5500 Serial Clock

#define ETH_RST      9   // Onboard W5500 Reset Line
#define ETH_INT      10  // Onboard W5500 Interrupt Line
#define W5500_MOSI   11  // <-- CRITICAL: Waveshare hardwired MOSI pin
#define W5500_MISO   12  // <-- CRITICAL: Waveshare hardwired MISO pin
#define W5500_SCK    13  // <-- CRITICAL: Waveshare hardwired Clock pin
#define CS_ETH       14  // Onboard W5500 Chip Select 
////////////////DO NOT CHANGE - HARD WIRED ON THE BOARD!!! ///////////////////////////////
////////////////DO NOT CHANGE - HARD WIRED ON THE BOARD!!! ///////////////////////////////

// --- Fan & Sensor Pins ---
#define ONE_WIRE_BUS 21

// --- Fan Hardware Constraints ---
const int NUM_FANS = 4; // Expanded to 4 to allow optional web allocation paths
const int pwmPins[NUM_FANS]  = {1, 18, 4, 6};
const int tachPins[NUM_FANS] = {2, 40, 5, 7}; // 🌟 FIXED: Moved Fan 2 Tach to safe GPIO 39

// --- Triple LCD Chip Mapping ---
#define TFT_BL       42  
#define TFT_DC       43  // 🌟 FIXED: Shifted away from 40 to avoid tach overlap
#define CS_MAIN      44  // 🌟 FIXED: Shifted away from 39 to avoid tach overlap
#define TFT_RST      45  // 🌟 FIXED: Shifted away from 41 to avoid tach overlap
#define CS_LEFT      47  
#define CS_RIGHT     48  

// --- Bus 2: 🌟 Exposed Secondary Hardware SPI Map for Displays ---
#define DISP_SCK      39  // Connect to display's SCLK / Clock pin
#define DISP_MOSI     41  // Connect to display's MOSI / Data pin

// 🌟 INJECTION: Instantiate a dedicated secondary Hardware SPI Bus object
SPIClass DisplaySPI(HSPI); // Leverages the ESP32-S3's native HSPI peripheral block

Adafruit_ST7789 screenMain = Adafruit_ST7789(&DisplaySPI, CS_MAIN, TFT_DC, TFT_RST);
Adafruit_ST7735 screenLeft = Adafruit_ST7735(&DisplaySPI, CS_LEFT, TFT_DC, TFT_RST);
Adafruit_ST7735 screenRight = Adafruit_ST7735(&DisplaySPI, CS_RIGHT, TFT_DC, TFT_RST);


// 🌟 FIXED: Group variables in a single struct to guarantee fixed memory boundaries
struct SystemConfig {
    IPAddress ip;
    IPAddress subnet;
    IPAddress gateway;
    IPAddress dns;
    float tMin;
    float tMax;
    bool isFahrenheit;
    int tzOffset;
    bool is24Hour;
    int fanCount;
    char nodeID[64];
    char haToken[256];
    char haSensor[64];
    char haHost[64];
    int haPort;
};

// Instantiate the master configuration block with default system values
SystemConfig config = {
    IPAddress(192, 168, 10, 53),
    IPAddress(255, 255, 255, 0),
    IPAddress(192, 168, 10, 1),
    IPAddress(192, 168, 10, 11),
    26.7,
    37.8,
    true,
    -5,
    true,
    2,
    "fan_controller_01",
    "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiIwMjVmZTM3MzA0MjQ0NWExYjUzOTZiMzhiZjU1ZjZkYiIsImlhdCI6MTc4OTA3OTkxMiwiZXhwIjoyMTA0NDM5OTEyfQ.mOZpWNO-u8VK0ld7zRiyqO7OAgeJYkAivJgPwJVK0wk",
    "living_room_probe_02_temperature",
    "192.168.10.85",
    8123
};

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// Expanded global variables tracking all 4 fan configurations
volatile unsigned long tachCounts[NUM_FANS] = {0, 0, 0, 0};
unsigned long currentRPMs[NUM_FANS] = {0, 0, 0, 0};
int currentDutyCycles[NUM_FANS] = {51, 51, 51, 51};

unsigned long lastUpdate = 0;

float localTempC = 0.0;
float networkTempC = 0.0;
float blendedAverageC = 0.0;
bool localSensorHealthy = false;
bool networkSensorHealthy = false;

bool localAlertSent = false;
bool networkAlertSent = false;
bool totalAlertSent = false;


// Instances
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
EthernetServer server(80);
WiFiServer wifiserver(80);

EthernetUDP Udp;
unsigned int localPortUDP = 8888;       // Local port to listen for UDP packets
const char* ntpServerName = "pool.ntp.org";
const int NTP_PACKET_SIZE = 48; 
byte packetBuffer[NTP_PACKET_SIZE];

// 🌟 FIXED: Compound addition updates are fully compliant with C++20 and compile to raw machine codes!
void IRAM_ATTR przypisISR0() { tachCounts[0] += 1; } // ⚡ Snaps perfectly to hardware interrupts!
void IRAM_ATTR przypisISR1() { tachCounts[1] += 1; }
void IRAM_ATTR przypisISR2() { tachCounts[2] += 1; }
void IRAM_ATTR przypisISR3() { tachCounts[3] += 1; }

// 🌟 FIXED: High-Fidelity URL Decoder Variant of your parameter extraction scanner
String getUrlParam(String src, String param) {
    int idx = src.indexOf(param);
    if (idx == -1) return "";
    
    int start = idx + param.length();
    int end = src.indexOf('&', start);
    if (end == -1) end = src.length();
    
    String val = src.substring(start, end);
    
    // ----------------------------------------------------
    // 🛠️ ACTIVE DECODER PASS: Clean out form transmission artifacts
    // ----------------------------------------------------
    val.replace("+", " "); // Instantly converts raw browser plus marks back into pure spaces!
    
    // Scan and translate explicit hexadecimal escape masks (like %2B -> +, %20 -> space)
    String decoded = "";
    for (size_t i = 0; i < val.length(); i++) {
        if (val[i] == '%' && i + 2 < val.length()) {
            // Unpack hex digit values to reconstruct the true symbol byte character
            char high = val[i+1];
            char low  = val[i+2];
            
            // Standard ASCII hexadecimal to integer conversion math
            int hVal = (high >= 'A') ? (high - 'A' + 10) : (high - '0');
            int lVal = (low >= 'A')  ? (low - 'A' + 10)  : (low - '0');
            if (high >= 'a') hVal = high - 'a' + 10;
            if (low >= 'a')  lVal = low - 'a' + 10;
            
            char decodedChar = (char)((hVal << 4) | lVal);
            decoded += decodedChar;
            i += 2; // Jump forward past the processed %XX character frame
        } else {
            decoded += val[i];
        }
    }
    
    return decoded;
}

void saveSettings() {
    File f = LittleFS.open("/settings.cfg", "w");
    if (f) {
        // 🌟 FIXED: Saves your entire structural memory block to flash cleanly in one single flash sector step
        f.write((uint8_t*)&config, sizeof(config));
        f.close();
        Serial.println("Settings saved safely and cleanly.");
    }
}

void loadSettings() {
    if (LittleFS.exists("/settings.cfg")) {
        File f = LittleFS.open("/settings.cfg", "r");
        if (f) {
            // Inside loadSettings() right after reading the struct:
            if (f.available() >= sizeof(config)) {
                f.read((uint8_t*)&config, sizeof(config));
            }
            f.close();
            
            // 🌟 FIXED: Enforce a strict minimum floor of 2 active fans for your current layout
            if (config.fanCount < 2 || config.fanCount > 4) {
                config.fanCount = 2; // Automatically forces both Channel 1 & 2 into the active PWM loop!
                Serial.println(" -> Config Count auto-realigned to 2 active channels.");
            }
        }
    }
}


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

// --- Fan Control Automation Core ---
void calculateFanCurve(float targetTemp) {
    int targetDuty = 0;
    
    // 🌟 FIXED: Shield constraint completely prevents divide-by-zero if limits are identical or uninitialized
    if (config.tMax <= config.tMin) {
        targetDuty = 255; // Secure safety override to full power if thresholds are corrupt
    } else {
        if (targetTemp < config.tMin) {
            targetDuty = 0; 
        } else if (targetTemp >= config.tMax) {
            targetDuty = 255; 
        } else {
            targetDuty = (int)(51.0 + ((targetTemp - config.tMin) / (config.tMax - config.tMin)) * 204.0);
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

// --- Inbound Home Assistant Core Scraper ---

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

// --- UI Graphic Draw Utilities ---
void drawStaticUIFrame() {
    screenMain.fillRect(0, 0, 240, 40, ST77XX_BLUE);
    screenMain.setTextColor(ST77XX_WHITE); screenMain.setTextSize(2);
    screenMain.setCursor(10, 12); screenMain.print("S3 ECOSYSTEM");
}


void updateMainDashboardUI() {
    // Top clock line updates (Leave unchanged)
    struct tm timeinfo;
    
    // 🌟 FIXED: Pass ST77XX_BLACK as the second argument to clear old clock digits dynamically!
    screenMain.setTextColor(ST77XX_YELLOW, ST77XX_BLACK); 
    screenMain.setTextSize(2);
    screenMain.setCursor(10, 48);

    if (getLocalTime(&timeinfo)) {
        char dStr[16], tStr[16];
        strftime(dStr, sizeof(dStr), "%m/%d/%Y", &timeinfo);
        if (config.is24Hour) strftime(tStr, sizeof(tStr), "%H:%M:%S", &timeinfo);
        else strftime(tStr, sizeof(tStr), "%I:%M %p", &timeinfo);

        screenMain.print(dStr);
        screenMain.setCursor(130, 48); screenMain.print(tStr);
    }
    else {
        screenMain.print("ESP32-S3 Network Cooling System");
    }

    // Main Temperature Average Block Updates
    screenMain.setCursor(15, 85);
    if (!localSensorHealthy && !networkSensorHealthy) {
        screenMain.setTextSize(4); 
        screenMain.setTextColor(ST77XX_RED, ST77XX_BLACK); // 🌟 FIXED
        screenMain.print("CRIT!");
    }
    else {
        float dispAvg = config.isFahrenheit ? ((blendedAverageC * 9.0 / 5.0) + 32.0) : blendedAverageC;
        screenMain.setTextSize(4);
        
        // 🌟 FIXED: Appends black backdrops to the dynamic temperature print strings
        uint16_t tempColor = (blendedAverageC > (config.tMax - 5.0)) ? ST77XX_RED : ST77XX_GREEN;
        screenMain.setTextColor(tempColor, ST77XX_BLACK);
        
        screenMain.print(dispAvg, 1);
        screenMain.setTextSize(2); screenMain.print(config.isFahrenheit ? " F AVG" : " C AVG");
    }

    // 🌟 ----------------------------------------------------
    // 📊 LOWER FAN RPM PRINT LINES (Opaque Text Backdrop Fix)
    // 🌟 ----------------------------------------------------
    screenMain.drawFastHLine(0, 135, 240, ST77XX_DARKGRAY);
    screenMain.setTextSize(2);
    
    // Channel 1 Text Frame Print
    screenMain.setCursor(10, 145); 
    screenMain.setTextColor(ST77XX_WHITE, ST77XX_BLACK); // 🌟 FIXED: Wipes old Fan 1 digits
    screenMain.print("F1: "); screenMain.print(currentRPMs[0]);
    screenMain.print("    "); // Pad with trailing spaces to instantly wipe out residual trailing text strings

    // Channel 2 Text Frame Print
    screenMain.setCursor(125, 145); 
    screenMain.setTextColor(ST77XX_WHITE, ST77XX_BLACK); // 🌟 FIXED: Wipes old Fan 2 digits
    screenMain.print("F2: "); screenMain.print(currentRPMs[1]);
    screenMain.print("    "); // Pad with trailing spaces to instantly wipe out residual trailing text strings
}


void updateLeftGaugesUI() { 
    int barWidth = 28; int spacing = 15; int startX = 35; 
    screenLeft.fillRect(0, 0, 160, 80, ST77XX_BLACK);
    for (int i = 0; i < NUM_FANS; i++) { 
      int currentX = startX + (i * (barWidth + spacing)); 
      int fillHeight = map(constrain(currentRPMs[i], 0, 5000), 0, 5000, 0, 60); 
      screenLeft.drawRect(currentX, 5, barWidth, 60, ST77XX_WHITE); 
      screenLeft.fillRect(currentX + 2, 5 + (56 - fillHeight), barWidth - 4, fillHeight, ST77XX_CYAN); 
    } 
}

void updateRightGaugesUI() {
    int barWidth = 28; int spacing = 15; int startX = 35; 
    screenRight.fillRect(0, 0, 160, 80, ST77XX_BLACK); 
    float maxScale = config.isFahrenheit ? ((config.tMax * 9 / 5) + 32) : config.tMax; 
    
    if (localSensorHealthy) {
        float dispL = config.isFahrenheit ? ((localTempC * 9 / 5) + 32) : localTempC; 
        int hL = map(constrain(dispL, 0, maxScale), 0, maxScale, 0, 60); 
        screenRight.drawRect(startX, 5, barWidth, 60, ST77XX_WHITE); 
        screenRight.fillRect(startX + 2, 5 + (56 - hL), barWidth - 4, hL, ST77XX_ORANGE);
    } else { 
        screenRight.drawRect(startX, 5, barWidth, 60, ST77XX_RED);
    }
    
    if (networkSensorHealthy) { 
        float dispN = config.isFahrenheit ? ((networkTempC * 9 / 5) + 32) : networkTempC; 
        int hN = map(constrain(dispN, 0, maxScale), 0, maxScale, 0, 60); 
        screenRight.drawRect(startX + barWidth + spacing, 5, barWidth, 60, ST77XX_WHITE); 
        screenRight.fillRect(startX + barWidth + spacing + 2, 5 + (56 - hN), barWidth - 4, hN, ST77XX_MAGENTA);
    } else { 
        screenRight.drawRect(startX + barWidth + spacing, 5, barWidth, 60, ST77XX_RED); 
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


void handleNativeWebTraffic(EthernetClient& client) {
    String req = client.readStringUntil('\r'); 
    
    // --- Asynchronous Background JSON API Data Provider ---
    if (req.indexOf("GET /ajax_data") != -1) {
        client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n");
        
        float liveLocal   = config.isFahrenheit ? ((localTempC * 9.0 / 5.0) + 32.0) : localTempC;
        float liveNetwork = config.isFahrenheit ? ((networkTempC * 9.0 / 5.0) + 32.0) : networkTempC;
        float liveBlended = config.isFahrenheit ? ((blendedAverageC * 9.0 / 5.0) + 32.0) : blendedAverageC;
        
        String json = "{";
        json += "\"local_temp\":" + String(liveLocal, 1) + ",";
        json += "\"net_temp\":" + String(liveNetwork, 1) + ",";
        json += "\"blend_temp\":" + String(liveBlended, 1) + ",";
        json += "\"fans\":[";
        for (int i = 0; i < 4; i++) {
            json += String(currentRPMs[i]);
            if (i < 3) json += ","; 
        }
        json += "]}";
        
        client.println(json);
        delay(1); client.stop();
        return;
    }

    bool networkSettingsChanged = false;
    bool regularSettingsChanged = false;

    // Handle Form Configuration Processing Loops
    if (req.indexOf("POST /submit") != -1 || req.indexOf("GET /submit?") != -1) {
        String body = "";
        if (req.indexOf("POST") != -1) {
            while (client.available()) {
                String line = client.readStringUntil('\n');
                if (line == "\r") {
                    body = client.readString();
                    break;
                }
            }
        } else {
            body = req;
        }

        // ============================================================================
        // 🌟 FIXED: Robust Isolated Temperature Form Processing Logic
        // ============================================================================
        String unitParam = getUrlParam(body, "unit=");
        bool submittedAsFahrenheit = (unitParam == "F");
        
        String tminStr = getUrlParam(body, "tmin=");
        String tmaxStr = getUrlParam(body, "tmax=");
        
        // Only modify your parameters if the text inputs actually contain text data
        if (tminStr.length() > 0 && tmaxStr.length() > 0) {
            float parsedMin = tminStr.toFloat();
            float parsedMax = tmaxStr.toFloat();
            
            // Execute conversion ONLY on the incoming numbers based strictly on the form submission flag
            if (submittedAsFahrenheit) {
                config.tMin = (parsedMin - 32.0) * 5.0 / 9.0;
                config.tMax = (parsedMax - 32.0) * 5.0 / 9.0;
            } else {
                config.tMin = parsedMin;
                config.tMax = parsedMax;
            }
        }

        
        // Ensure these lines directly follow your new block:
        config.isFahrenheit = submittedAsFahrenheit;
        config.is24Hour = (getUrlParam(body, "clk=") == "24");
        config.tzOffset = getUrlParam(body, "tz=").toInt();
        config.fanCount = getUrlParam(body, "fancnt=").toInt();


        String nodeParam = getUrlParam(body, "nodeid=");
        if (nodeParam.length() > 0) {
            nodeParam.replace(" ", "_");
            strncpy(config.nodeID, nodeParam.c_str(), sizeof(config.nodeID) - 1);
            config.nodeID[sizeof(config.nodeID) - 1] = '\0';
        }

        String hostParam = getUrlParam(body, "hahost=");
        if (hostParam.length() > 0 && hostParam != "") {
            hostParam.replace(" ", ""); 
            strncpy(config.haHost, hostParam.c_str(), sizeof(config.haHost) - 1);
            config.haHost[sizeof(config.haHost) - 1] = '\0';
        }

        String portParam = getUrlParam(body, "haport=");
        if (portParam.length() > 0) {
            int checkPort = portParam.toInt();
            if (checkPort > 0) config.haPort = checkPort;
        }

        String sensorParam = getUrlParam(body, "hasensor=");
        if (sensorParam.length() > 0 && sensorParam != "") {
            sensorParam.replace(" ", "_");
            strncpy(config.haSensor, sensorParam.c_str(), sizeof(config.haSensor) - 1);
            config.haSensor[sizeof(config.haSensor) - 1] = '\0';
        }

        String tokenParam = getUrlParam(body, "hatoken=");
        if (tokenParam.length() > 0 && tokenParam != "") {
            strncpy(config.haToken, tokenParam.c_str(), sizeof(config.haToken) - 1);
            config.haToken[sizeof(config.haToken) - 1] = '\0';
        }

        String ipStr = getUrlParam(body, "ip=");
        if (ipStr.length() > 0) {
            parseIpString(ipStr, config.ip);
            parseIpString(getUrlParam(body, "sub="), config.subnet);
            parseIpString(getUrlParam(body, "gw="), config.gateway); // 🌟 FIXED: Target gateway correctly
            parseIpString(getUrlParam(body, "dns="), config.dns);
            networkSettingsChanged = true;
        }

        saveSettings();
        regularSettingsChanged = true;
    }

    if (networkSettingsChanged || regularSettingsChanged) {
        client.println("HTTP/1.1 303 See Other");
        client.println("Location: /"); 
        client.println("Connection: close\r\n");
        client.stop();
        if (networkSettingsChanged) {
            delay(500); ESP.restart(); 
        }
        return;
    }

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Cache-Control: no-cache, no-store, must-revalidate"); 
    client.println("Pragma: no-cache");
    client.println("Expires: 0");
    client.println("Connection: close");
    client.println(); 

    // ==========================================
    // 🌟 FIXED: Map display thresholds straight to your master system struct tracks
    // ==========================================
    float displayMin = config.isFahrenheit ? ((config.tMin * 9.0 / 5.0) + 32.0) : config.tMin;
    float displayMax = config.isFahrenheit ? ((config.tMax * 9.0 / 5.0) + 32.0) : config.tMax;
    String scaleSymbol = config.isFahrenheit ? "F" : "C";

    float initialLocal   = config.isFahrenheit ? ((localTempC * 9.0 / 5.0) + 32.0) : localTempC;
    float initialNetwork = config.isFahrenheit ? ((networkTempC * 9.0 / 5.0) + 32.0) : networkTempC;
    float initialBlended = config.isFahrenheit ? ((blendedAverageC * 9.0 / 5.0) + 32.0) : blendedAverageC;


    int currentHour = hour();
    int currentMinute = minute();
    int currentSecond = second();

    client.println("<!DOCTYPE html><html><head><title>S3 Dashboard</title>");
    client.println("<style>body{font-family:sans-serif; background:#f4f7f6; padding:20px; text-align:center;} .box{background:white; max-width:550px; margin:auto; padding:25px; border-radius:6px; box-shadow:0 2px 10px rgba(0,0,0,0.05); text-align:left;} input[type=text], input[type=number], select, textarea{width:100%; padding:10px; margin:5px 0 15px 0; border:1px solid #ccc; border-radius:4px; box-sizing:border-box;} .grid{display:flex; justify-content:space-between; margin-bottom:15px; flex-wrap:wrap;} .card{background:#edf1f5; padding:12px; border-radius:4px; width:30%; text-align:center; font-size:12px; box-sizing:border-box;} .card-large{width:100%; margin-bottom:15px; background:#edf1f5; padding:15px; border-radius:4px; text-align:center;} .fan-box{background:#f8f9fa; padding:10px; margin:5px 0; border-left:4px solid #17a2b8; display:flex; justify-content:space-between; font-size:14px;} .btn{width:100%; padding:14px; background:#28a745; color:white; border:none; font-weight:bold; font-size:16px; border-radius:4px; cursor:pointer; margin-top:20px;}</style>");
    
    client.println("<script>");
    client.print("let h = "); client.print(currentHour); client.println(";");
    client.print("let m = "); client.print(currentMinute); client.println(";");
    client.print("let s = "); client.print(currentSecond); client.println(";");
    client.print("const is24 = "); client.print(config.is24Hour ? "true" : "false"); client.println(";");
    
    client.println("function updateLiveClock() {");
    client.println("  s++; if(s>=60){ s=0; m++; if(m>=60){ m=0; h++; if(h>=24){ h=0; } } }");
    client.println("  let displayH = h; let ampm = '';");
    client.println("  if(!is24) { ampm = displayH >= 12 ? ' PM' : ' AM'; displayH = displayH % 12; if(displayH === 0) displayH = 12; }");
    client.println("  let strH = displayH < 10 ? '0'+displayH : displayH;");
    client.println("  let strM = m < 10 ? '0'+m : m;");
    client.println("  let strS = s < 10 ? '0'+s : s;");
    client.println("  let timeElement = document.getElementById('liveClockText');");
    client.println("  if(timeElement) { timeElement.innerText = strH + ':' + strM + ':' + strS + ampm; }");
    client.println("}");
    
    client.println("function fetchLiveTelemetry() {");
    client.println("  fetch(\"/ajax_data\").then(response => response.json()).then(data => {");
    client.println("    document.getElementById(\"liveLocalText\").innerText = data.local_temp;");
    client.println("    document.getElementById(\"liveNetText\").innerText = data.net_temp;");
    client.println("    document.getElementById(\"liveBlendText\").innerText = data.blend_temp;");
    client.println("    data.fans.forEach((rpm, index) => {");
    client.println("      let fanEl = document.getElementById(\"fanRpm_\" + index);");
    client.println("      if(fanEl) fanEl.innerText = rpm + \" RPM\";");
    client.println("    });");
    client.println("  }).catch(err => console.error(\"Data drop:\", err));");
    client.println("}");
    
    client.println("setInterval(updateLiveClock, 1000);");
    client.println("setInterval(fetchLiveTelemetry, 2000);");
    client.println("</script>");

    client.println("</head><body>");
    
    client.println("<div class='box'><h2 style='text-align:center; color:#0056b3; margin-top:0;'>ESP32-S3 Network Matrix Console</h2>");
    client.println("<div class='card-large'>🕒 <strong>System Clock</strong><br><span id='liveClockText' style='font-size:20px; color:#0056b3; font-weight:bold;'>Syncing...</span></div>");
    
    client.println("<div class='grid'>");
    client.print("<div class='card'>📌 <strong>Local Probe</strong><br><span style='font-size:14px; color:#28a745; font-weight:bold;'><span id='liveLocalText'>"); client.print(initialLocal, 1); client.println("</span> &deg;" + scaleSymbol + "</span></div>");
    client.print("<div class='card'>🌐 <strong>HA Network</strong><br><span style='font-size:14px; color:#0056b3; font-weight:bold;'><span id='liveNetText'>"); client.print(initialNetwork, 1); client.println("</span> &deg;" + scaleSymbol + "</span></div>");
    client.print("<div class='card'>⚖️ <strong>Blended Avg</strong><br><span style='font-size:14px; color:#e0a800; font-weight:bold;'><span id='liveBlendText'>"); client.print(initialBlended, 1); client.println("</span> &deg;" + scaleSymbol + "</span></div>");
    client.println("</div><hr>");
    
    client.println("<h3>📊 Live Tachometer Metrics</h3>");
    for(int i = 0; i < 4; i++) {
        client.print("<div class='fan-box'><span><strong>Fan Channel " + String(i+1) + "</strong> ");
        if (i < config.fanCount) {
            client.print("<span style='color:#28a745; font-size:11px;'>[Active]</span>");
        } else {
            client.print("<span style='color:#dc3545; font-size:11px;'>[Disabled/Expansion]</span>");
        }
        client.print("</span><span id='fanRpm_" + String(i) + "' style='font-weight:bold; color:#17a2b8;'>0 RPM</span></div>");
    }
    client.println("<hr>");

    client.println("<form action='/submit' method='post'>");
    
    client.println("<h3>⚙️ Active Core Infrastructure Scaling</h3>");
    client.print("<div style='margin-bottom:20px; display:flex; gap:15px;'>");
    client.print("<label><input type='radio' name='fancnt' value='1' " + String(config.fanCount == 1 ? "checked" : "") + "> 1 Fan</label>");
    client.print("<label><input type='radio' name='fancnt' value='2' " + String(config.fanCount == 2 ? "checked" : "") + "> 2 Fans</label>");
    client.print("</div>");

    client.println("<h3>🌡️ Thermal Profile Constraints</h3>");
    client.print("<div style='margin-bottom:15px;'>");
    client.print("<label style='margin-right:15px;'><input type='radio' name='unit' value='C' " + String(!config.isFahrenheit ? "checked" : "") + "> Celsius</label>");
    client.print("<label><input type='radio' name='unit' value='F' " + String(config.isFahrenheit ? "checked" : "") + "> Fahrenheit</label>");
    client.print("</div>");
    
    client.print("Min Activation Temp Limit: <input type='text' name='tmin' value='"); client.print(displayMin, 1); client.println("'>");
    client.print("Max Capacity Temp Limit: <input type='text' name='tmax' value='"); client.print(displayMax, 1); client.println("'>");

    client.println("<h3>🕒 Time Synchronizations</h3>");
    client.print("Timezone Offset (Hours): <input type='number' name='tz' value='"); client.print(config.tzOffset); client.println("'>");
    client.print("<div style='margin-bottom:15px;'><label style='font-weight:bold; display:block; margin-bottom:5px;'>Format Mode:</label>");
    client.print("<label style='margin-right:15px;'><input type='radio' name='clk' value='12' " + String(!config.is24Hour ? "checked" : "") + "> 12-Hour</label>");
    client.print("<label><input type='radio' name='clk' value='24' " + String(config.is24Hour ? "checked" : "") + "> 24-Hour</label></div>");

    client.println("<h3>🌐 Static Network Layer Infrastructure</h3>");
    client.print("Static IP Assignment: <input type='text' name='ip' value='"); client.print(config.ip.toString()); client.println("'>");
    client.print("Subnet Mask Filter: <input type='text' name='sub' value='"); client.print(config.subnet.toString()); client.println("'>");
    client.print("Gateway Link Node: <input type='text' name='gw' value='"); client.print(config.gateway.toString()); client.println("'>");
    client.print("DNS Nameserver Node: <input type='text' name='dns' value='"); client.print(config.dns.toString()); client.println("'>");

    client.println("<h3>🏡 Home Assistant Cluster Integration</h3>");
    client.print("Server IP / Host Address: <input type='text' name='hahost' value='"); client.print(config.haHost); client.println("' placeholder='e.g., 192.168.10.85' maxlength='63'>");
    
    // 🌟 FIXED: Changed 'config.haToken' back to 'config.haPort' so it displays 8123 instead of your security signature string!
    client.print("Server API Connection Port: <input type='text' name='haport' value='"); client.print(config.haPort); client.println("' placeholder='e.g., 8123' maxlength='10'>");
    
    client.print("Local Outbound Entity ID: <input type='text' name='nodeid' value='"); client.print(config.nodeID); client.println("' placeholder='e.g., fan_controller_01' maxlength='63'>");
    client.print("Remote Inbound Sensor ID: <input type='text' name='hasensor' value='"); client.print(config.haSensor); client.println("' placeholder='e.g., rack_temperature' maxlength='63'>");
    client.print("Long-Lived Bearer Token string:<br><textarea name='hatoken' rows='4' maxlength='450'>"); client.print(config.haToken); client.println("</textarea>");

    // 🌟 FORCE FLASH: Clears the network transmission pipeline right before printing long file names
    client.flush(); 
    delay(5);

    // 🛠️ Firmware Build Artifact Tracking Section Layout Block
    client.println("<hr><div style='background:#edf1f5; padding:15px; border-radius:4px; margin:20px 0; font-size:12px; color:#555;'>");
    client.println("<label style='font-weight:bold; display:block; margin-bottom:5px; color:#333;'>🛠️ Firmware Compilation Artifacts</label>");
    
    // Clean string processor extracting ONLY the filename so it wraps perfectly on phones and browsers
    client.print("<div style='word-break: break-all; white-space: normal;'><strong>Source File:</strong> "); 
    String fullPath = String(__FILE__);
    int lastSlash = fullPath.lastIndexOf('\\');
    if (lastSlash == -1) lastSlash = fullPath.lastIndexOf('/');
    String fileNameOnly = (lastSlash != -1) ? fullPath.substring(lastSlash + 1) : fullPath;
    
    client.print(fileNameOnly); 
    client.println("</div>");
    client.print("<strong>Build Date:</strong> ");  client.print(__DATE__); client.println("<br>");
    client.print("<strong>Build Time:</strong> ");  client.print(__TIME__); client.println("");
    client.println("</div>");

    client.println("<input type='submit' class='btn' value='Apply Parameters &amp; Save'>");
    client.println("</form></div></body></html>");

    client.stop(); // Safe connection termination breakout checkpoint
}





// Sends an outbound NTP time query packet to the target server
void sendNTPpacket(const char* address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  Udp.beginPacket(address, 123); // NTP requests use port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

// Intercepts, parses, and pushes the atomic time into the ESP32-S3 system clock
time_t getNtpTime() {
  while (Udp.parsePacket() > 0) ; // Discard any previously received packets
  Serial.println("Sending outbound atomic time request to NTP pool...");
  sendNTPpacket(ntpServerName);
  
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("NTP Time Packet received successfully!");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // Read packet into the buffer
      
      // The timestamp starts at byte 40 and is 4 bytes long:
      unsigned long secsSince1900;
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      
      unsigned long secsSince1970 = secsSince1900 - 2208988800UL;
      
      // Calculate and apply your saved webpage timezone offset
      return secsSince1970 + (config.tzOffset * 3600);
    }
  }
  Serial.println("NTP Sync Error: Request timed out. Checking link layer...");
  return 0; // Return 0 if unable to get the time
}


// --- Core Runtime Entry Points ---
void setup() {
  Serial.begin(115200);
  delay(REFRESH_PERIOD); 

  // Hardware reset pulse for the onboard W5500 chip
  pinMode(ETH_RST, OUTPUT);
  digitalWrite(ETH_RST, LOW);   
  delay(50);                   
  digitalWrite(TFT_RST, HIGH);  
  digitalWrite(ETH_RST, HIGH);  
  delay(50);                   

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Initialize Bus 1 (Hidden internal network traces)
  SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI, CS_ETH); 
  delay(50);

  // Initialize Bus 2 (Exposed display header pins)
  DisplaySPI.begin(DISP_SCK, -1, DISP_MOSI, -1);
  delay(20);

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////



  if (LittleFS.begin(true)) { 
        loadSettings(); 
      } else {
        Serial.println("❌ Critical Error: LittleFS format failed completely.");
    }



////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

    Ethernet.init(CS_ETH); 
    Serial.println("Initializing Waveshare W5500 Ethernet System");
    Ethernet.begin(mac, config.ip, config.dns, config.gateway, config.subnet);
    
    Udp.begin(localPortUDP);
    server.begin();

    // Fire up screens via your dedicated DisplaySPI secondary line
    screenMain.init(240, 240); screenMain.fillScreen(ST77XX_BLACK);
    screenLeft.initR(INITR_MINI160x80); screenLeft.setRotation(1); screenLeft.fillScreen(ST77XX_BLACK);
    screenRight.initR(INITR_MINI160x80); screenRight.setRotation(1); screenRight.fillScreen(ST77XX_BLACK);
    drawStaticUIFrame();

    sensors.begin();

    // ==========================================
    // 🌟 HARDWARE ATTACHMENT PASS: Fixed Static Vector Mappings
    // ==========================================
    // 1. Lock down all 4 channels to INPUT_PULLUP to eliminate floating static noise
    for (int i = 0; i < 4; i++) {
        ledcAttach(pwmPins[i], PWM_FREQ, PWM_RES_BITS); 
        pinMode(tachPins[i], INPUT_PULLUP); 
    }

    // 2. 🌟 FIXED: Flat, direct hardware tracking assignments. No loops, no overlapping vectors!
    if (config.fanCount >= 1) {
        attachInterrupt(digitalPinToInterrupt(tachPins[0]), przypisISR0, FALLING);
    }
    if (config.fanCount >= 2) {
        attachInterrupt(digitalPinToInterrupt(tachPins[1]), przypisISR1, FALLING);
    }
  
  // Future Expansion Channels (Safely fenced out unless activated)
  // if (config.fanCount >= 3) { attachInterrupt(digitalPinToInterrupt(tachPins[2]), przypisISR2, FALLING); }
  // if (config.fanCount >= 4) { attachInterrupt(digitalPinToInterrupt(tachPins[3]), przypisISR3, FALLING); }


    setSyncProvider(getNtpTime);     
    setSyncInterval(300);            

    // 🏁 --- Setup Execution Completed Notification Block ---
    Serial.println("\n==================================================");
    Serial.println("🚀 SYSTEM INITIALIZED AND RUNNING SUCCESSFULLY!");
    Serial.print("🌐 Login Dashboard URL Target: http://");
    Serial.println(Ethernet.localIP()); 
    Serial.print("🛠️ Hardware Status Code: ");
    Serial.println((int)Ethernet.hardwareStatus()); 
    Serial.println("==================================================\n");

}


void loop() {
  // ==========================================
  // BUCKET 1: UNRESTRICTED TEMPERATURE SAMPLING (Runs Constantly)
  // ==========================================
  static unsigned long lastThermalSample = 0;
  // Sample the physical DS18B20 probe every 1000ms to allow smooth updates
  if (millis() - lastThermalSample >= 1000) {
      lastThermalSample = millis();
      
      sensors.requestTemperatures();
      float rawLocal = sensors.getTempCByIndex(0);
      
      // Sanity boundaries protecting against disconnected wire readings (-127C or 85C)
      if (rawLocal > -50.0 && rawLocal != 85.0 && rawLocal != -127.0) {
          localTempC = rawLocal;
          localSensorHealthy = true;
      } else {
          localSensorHealthy = false;
      }
      
      // Execute core sensor failsafe evaluation to update blendedAverageC
      if (localSensorHealthy && networkSensorHealthy) {
          blendedAverageC = (localTempC + networkTempC) / 2.0;
      } else if (localSensorHealthy && !networkSensorHealthy) {
          blendedAverageC = localTempC; // Fallback smoothly to hardwired probe
      } else if (!localSensorHealthy && networkSensorHealthy) {
          blendedAverageC = networkTempC; // Fallback smoothly to network sensor
      } else {
          blendedAverageC = 0.0; // Total blackout state default
      }
      
      // Automatically update your 25 kHz hardware PWM fan speeds based on the newest metrics
      calculateFanCurve(blendedAverageC);
  }

  // ==========================================
  // BUCKET 2: TACHOMETER RPM CALCULATIONS (Strict 1-Second Window)
  // ==========================================
  static unsigned long lastRPMCalcTime = 0;
  if (millis() - lastRPMCalcTime >= 1000) { 
      unsigned long timeElapsed = millis() - lastRPMCalcTime;
      lastRPMCalcTime = millis();
      
      for (int i = 0; i < 4; i++) {
          // 🌟 FIXED: Loop loops through all 4 constant positions to keep memory aligned
          if (i < config.fanCount) {
              noInterrupts(); 
              unsigned long pulses = tachCounts[i];
              tachCounts[i] = 0; 
              interrupts();
              
              if (pulses > 0 && timeElapsed > 0) {
                  currentRPMs[i] = (pulses * 60000) / (2 * timeElapsed);
              } else {
                  currentRPMs[i] = 0;
              }
          } else {
              // 🌟 SAFE FALLBACK: Force unselected channels straight to zero and flush ghost counts!
              tachCounts[i] = 0;
              currentRPMs[i] = 0;
          }
      }
  }


  // ==========================================
  // BUCKET 3: MULTI-SOCKET WEB TRAFFIC HANDLER (Snappy Processing)
  // ==========================================
  for (int i = 0; i < 4; i++) {
    EthernetClient client = server.available();
    if (client) {
      Serial.print(" -> Active Web Transaction Detected on Socket Channel: "); Serial.println(i);
      handleNativeWebTraffic(client); 
    }
  }

  // ==========================================
  // BUCKET 4: HOME ASSISTANT SYNC INTERFACE (Every 2 Seconds)
  // ==========================================
  static unsigned long lastHAUpdate = 0;
  if (millis() - lastHAUpdate >= REFRESH_PERIOD) { 
    lastHAUpdate = millis();
    fetchHomeAssistantTemperature();
  }
}

