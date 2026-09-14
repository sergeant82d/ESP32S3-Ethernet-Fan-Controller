#ifndef A_GLOBALS_H
#define A_GLOBALS_H

// --- Configuration Constants ---
const uint32_t PWM_FREQ = 25000;
const int PWM_RES_BITS = 8;
const unsigned long REFRESH_PERIOD = 2000;

#ifndef ST77XX_DARKGRAY
#define ST77XX_DARKGRAY 0x7BEF 
#endif

// --- Waveshare ESP32-S3-ETH PCB Internal Hardware Map ---
#define SD_CS       4   
#define SD_MISO     5   
#define SD_MOSI     6   
#define SD_CLK      7   
#define ETH_RST      9   
#define ETH_INT      10  
#define W5500_MOSI   11  
#define W5500_MISO   12  
#define W5500_SCK    13  
#define CS_ETH       14  

// --- Fan & Sensor Pins ---
#define ONE_WIRE_BUS 21
const int NUM_FANS = 4; 
const int pwmPins[NUM_FANS]  = {1, 18, 8, 4};     
const int tachPins[NUM_FANS] = {2, 40, 17, 7}; 

// --- Triple LCD Chip Mapping ---
#define TFT_BL       42  
#define TFT_DC       43  
#define CS_MAIN      44  
#define TFT_RST      45  
// #define CS_LEFT      47  
// #define CS_RIGHT     48  
#define DISP_SCK      39  
#define DISP_MOSI     41  

// --- System Configuration Template ---
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
    char haToken[512];
    char haSensor[64];
    char haHost[64];
    int haPort;
};

// Global config instance declaration
extern SystemConfig config;

// User-configurable Minimum Fan PWM Duty Cycle (8-bit scale: 0 - 255)
// Noctua NF-A12x25 PWM can reliably spin down to ~10-15% (duty 25 - 38)
#define FAN_MIN_DUTY 30

// --- Global Telemetry Variables ---
extern byte mac[];
extern volatile unsigned long tachCounts[NUM_FANS];
extern unsigned long currentRPMs[NUM_FANS];
extern int currentDutyCycles[NUM_FANS];
extern float localTempC;
extern float networkTempC;
extern float blendedAverageC;
extern bool localSensorHealthy;
extern bool networkSensorHealthy;
extern bool localAlertSent;
extern bool networkAlertSent;
extern bool totalAlertSent;
extern unsigned int localPortUDP;
extern char masterProjectFileName[64]; // Safe memory boundary container

// --- Forward Declarations for Cross-Tab Visibility ---
// --- Forward Declarations for Cross-Tab Visibility ---
void IRAM_ATTR przypisISR0();
void IRAM_ATTR przypisISR1();
void IRAM_ATTR przypisISR2();
void IRAM_ATTR przypisISR3();

void drawStaticUIFrame();
void updateMainDashboardUI();
void updateLeftGaugesUI();
void updateRightGaugesUI();
void handleNativeWebTraffic(EthernetClient& client);
void fetchHomeAssistantTemperature();
void loadSettings();
void saveSettings();
void initNetworkHardwarePins();
void initFanHardwarePWM();
void runTemperatureSamplingBucket();
void runTachometerCalculationBucket();
void runWebserverProcessingBucket();
void runHomeAssistantSyncBucket();
void evaluateSensorFailsafes();
void parseIpString(String ipStr, IPAddress &ip);  // 🌟 Verified Here
void calculateFanCurve(float targetTemp);          // 🌟 Verified Here
time_t getNtpTime();
void printBootStatusNotification();


#endif


