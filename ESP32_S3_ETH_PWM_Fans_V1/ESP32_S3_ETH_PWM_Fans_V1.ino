#include <SPI.h>
#include <Ethernet.h>  
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
#include <EthernetUdp.h> 
#include <TimeLib.h>     

// Include our structural variable definitions first
#include "A_Globals.h"


// ============================================================================
// 🌟 GLOBAL INSTANTIATIONS: Allocates physical memory for your extern trackers
// ============================================================================
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
unsigned int localPortUDP = 8888;

volatile unsigned long tachCounts[NUM_FANS] = {0, 0, 0, 0};
unsigned long currentRPMs[NUM_FANS] = {0, 0, 0, 0};
int currentDutyCycles[NUM_FANS] = {FAN_MIN_DUTY, FAN_MIN_DUTY, FAN_MIN_DUTY, FAN_MIN_DUTY};

float localTempC = 0.0;
float networkTempC = 0.0;
float blendedAverageC = 0.0;
bool localSensorHealthy = false;
bool networkSensorHealthy = false;

bool localAlertSent = false;
bool networkAlertSent = false;
bool totalAlertSent = false;

// Inside your master tab global variables zone:
char masterProjectFileName[64] = ""; // Allocate memory cells


// Master Hardware Instances
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
EthernetServer server(80);
WiFiServer wifiserver(80);
EthernetUDP Udp;

SPIClass DisplaySPI(HSPI); 
Adafruit_ST7789 screenMain = Adafruit_ST7789(&DisplaySPI, CS_MAIN, TFT_DC, TFT_RST);
Adafruit_ST7735 screenLeft = Adafruit_ST7735(&DisplaySPI, CS_LEFT, TFT_DC, TFT_RST);
Adafruit_ST7735 screenRight = Adafruit_ST7735(&DisplaySPI, CS_RIGHT, TFT_DC, TFT_RST);

void setup() {
  String fullPath = String(__FILE__);
  int lastSlash = fullPath.lastIndexOf('\\');
  if (lastSlash == -1) lastSlash = fullPath.lastIndexOf('/');
  String cleanName = (lastSlash != -1) ? fullPath.substring(lastSlash + 1) : fullPath;
  strncpy(masterProjectFileName, cleanName.c_str(), sizeof(masterProjectFileName) - 1);
  masterProjectFileName[sizeof(masterProjectFileName) - 1] = '\0';

  Serial.begin(115200);
  delay(500); 

  // STEP 1: MOUNT FILESYSTEM FIRST (Before peripheral bus activity)
  // Passing "spiffs" explicitly ensures correct partition binding across core versions
  if (LittleFS.begin(true, "/littlefs", 10, "spiffs")) { 
      loadSettings();
      Serial.println(" LittleFS mounted and configuration loaded.");
  } else {
      Serial.println(" Critical Error: LittleFS failed to mount partition.");
  }

  // STEP 2: Initialize hardware pins and reset states
  initNetworkHardwarePins();
  delay(10);

  // STEP 3: Initialize network SPI bus
  SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI, CS_ETH); 
  delay(50);

  // STEP 4: Initialize display SPI bus
  DisplaySPI.begin(DISP_SCK, -1, DISP_MOSI, -1);
  delay(20);

  // STEP 5: Launch Ethernet and UDP sockets
  Ethernet.init(CS_ETH); 
  Ethernet.begin(mac, config.ip, config.dns, config.gateway, config.subnet);
  Udp.begin(localPortUDP);
  server.begin();

  // Screens and sensor initialization
  screenMain.init(240, 240); screenMain.fillScreen(ST77XX_BLACK);
  screenLeft.initR(INITR_MINI160x80); screenLeft.setRotation(1); screenLeft.fillScreen(ST77XX_BLACK);
  screenRight.initR(INITR_MINI160x80); screenRight.setRotation(1); screenRight.fillScreen(ST77XX_BLACK);
  drawStaticUIFrame();

  sensors.begin();
  initFanHardwarePWM();

  setSyncProvider(getNtpTime);     
  setSyncInterval(300);            

  printBootStatusNotification();
}


void loop() {
  // BUCKET 1: Temperature Sampling (Every 1 Second)
  runTemperatureSamplingBucket();

  // BUCKET 2: Tachometer RPM Calculations (Every 1 Second)
  runTachometerCalculationBucket();

  // BUCKET 3: Multi-Socket Web Traffic Handler (Snappy Processing)
  runWebserverProcessingBucket();

  // BUCKET 4: Home Assistant Sync Interface (Every 2 Seconds)
  runHomeAssistantSyncBucket();
}
