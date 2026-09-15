/*
  Name:    ESP32_S3_FanController_2inch.ino
  Board:   Waveshare ESP32-S3-Touch-LCD-2 (240x320 landscape, ST7789T3 + CST816D)
  Ethernet: external W5500 module on GPIO 9-14 (see pins.h)

  2-channel PWM fan controller with:
    - DS18B20 local temperature probe (OneWire)
    - Tachometer RPM sensing on both fan channels
    - Onboard LCD dashboard (title bar shows the HA node name, tall RPM/probe
      bar gauges left+right, clock/IP/temp/readouts centered)
    - Built-in web server + config page
    - Two-way Home Assistant sync (push local telemetry, pull a network
      temperature sensor for failsafe blending)

  Code is split into modules for maintainability:
    pins.h            - all board/peripheral pin definitions
    config.h/.cpp     - persistent settings struct (LittleFS load/save)
    sensors.h/.cpp    - DS18B20, fan PWM curve, tach ISRs/RPM calc
    network.h/.cpp    - W5500 bring-up, NTP time sync
    display.h/.cpp    - LCD dashboard rendering
    home_assistant.h/.cpp - HA push/pull + alert notifications
    web_server.h/.cpp - HTTP config page + AJAX telemetry endpoint
*/

#include "pins.h"
#include "config.h"
#include "sensors.h"
#include "network.h"
#include "display.h"
#include "home_assistant.h"
#include "web_server.h"
#include "touch.h"
#include "sd_logger.h"

#include <LittleFS.h>
#include <TimeLib.h>

// --- Diagnostic toggle: overnight lockup investigation ---
// Set to 0 to disable touch entirely (touchInit()/handleTouchInput() both
// skipped) for an isolation test - touch/I2C is the current lead suspect,
// since it's the newest, most hardware-unverified, continuously-running
// code path, and the original monolithic sketch (which never locks up)
// never touches I2C at all. Set back to 1 once touch is cleared or fixed.
#define TOUCH_ENABLED 0

// __FILE__ only reports each module's own filename when used inside a .cpp
// file - it can't see the main sketch's name from web_server.cpp or anywhere
// else. Defined once, here, at the actual source of truth; web_server.cpp
// displays this instead of trying to derive it via __FILE__.
// Update this string if the sketch is ever renamed.
const char* SKETCH_FILENAME = "ESP32_S3_FanController_2inch.ino";

void setup() {
    Serial.begin(115200);
    delay(2000);

    // NOTE: this partition scheme (app3M_fat9M_16MB, chosen for OTA support)
    // names its data partition "ffat", not the Arduino default "spiffs" -
    // must be passed explicitly or LittleFS.begin() fails to find it.
    // mountLittleFSWithRecovery() also force-reformats on genuine corruption,
    // since formatOnFail alone doesn't reliably catch that failure class.
    if (mountLittleFSWithRecovery()) {
        loadSettings();
    } else {
        Serial.println("Critical error: LittleFS mount/format failed.");
    }

    displayInit();
    networkInit();
    sensorsInit();
#if TOUCH_ENABLED
    touchInit();
#else
    Serial.println("DIAGNOSTIC: touch disabled for this run (TOUCH_ENABLED=0).");
#endif

    setSyncProvider(getNtpTime);
    setSyncInterval(300);

    // Must come after displayInit() (shared SPI bus with MISO configured)
    // and after setSyncProvider() (so the boot-event log's timestamp can
    // benefit from a fresh sync attempt instead of always logging "notime").
    sdLoggerInit();

    Serial.print("DIAGNOSTIC: initial free heap = "); Serial.println(ESP.getFreeHeap());

    Serial.println("\n==================================================");
    Serial.println("System initialized and running.");
    Serial.println("==================================================\n");
}

void loop() {
    // --- Local temperature sampling + fan curve (every 1s) ---
    static unsigned long lastThermalSample = 0;
    if (millis() - lastThermalSample >= 1000) {
        lastThermalSample = millis();

        sampleLocalTemperature();
        evaluateSensorFailsafes();
        calculateFanCurve(blendedAverageC);
        sdLoggerUpdateSnapshot(); // keep the RTC last-known-state snapshot current
    }

    // --- Tachometer RPM calculation (strict 1s window) ---
    static unsigned long lastRPMCalcTime = 0;
    if (millis() - lastRPMCalcTime >= 1000) {
        unsigned long timeElapsed = millis() - lastRPMCalcTime;
        lastRPMCalcTime = millis();
        calculateRPMs(timeElapsed);
    }

    // --- Display refresh (full dashboard, every 2s) ---
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate >= REFRESH_PERIOD_MS) {
        lastDisplayUpdate = millis();
        updateMainDashboardUI();
    }

    // --- Bar gauge fast refresh (every 200ms) ---
    // Independent of the 2s full refresh above - this is what makes the
    // near-limit bar flash (see display.cpp: resolveBarFill()) actually
    // read as a flash instead of crawling along at the slower cadence.
    static unsigned long lastBarRefresh = 0;
    const unsigned long BAR_REFRESH_PERIOD_MS = 200;
    if (millis() - lastBarRefresh >= BAR_REFRESH_PERIOD_MS) {
        lastBarRefresh = millis();
        refreshBarsOnly();
    }

    // --- Web traffic handling ---
    EthernetClient client = server.available();
    if (client) {
        handleNativeWebTraffic(client);
    }

    // --- Home Assistant sync (every 2s) ---
    static unsigned long lastHAUpdate = 0;
    if (millis() - lastHAUpdate >= REFRESH_PERIOD_MS) {
        lastHAUpdate = millis();
        fetchHomeAssistantTemperature();
    }

    // --- HA-editable tMin/tMax poll (every 60s) ---
    // Deliberately slow - these change rarely, and every poll costs 2 more
    // W5500 socket open/close cycles on top of the 2s telemetry cycle's own
    // socket usage. A shorter interval here (originally 15s) is suspected
    // to have reintroduced the same W5500 socket-contention issue that
    // caused the original HA-outage bug - intermittent failures of the
    // network-temp GET specifically, plus general loop stutter.
    static unsigned long lastThresholdPoll = 0;
    const unsigned long HA_THRESHOLD_POLL_MS = 60000;
    if (millis() - lastThresholdPoll >= HA_THRESHOLD_POLL_MS) {
        lastThresholdPoll = millis();
        fetchThresholdsFromHA();
    }

    // --- HA manual-override poll (every 5 min - safety-net fallback only) ---
    // HA now pushes override changes to /override_set immediately via a
    // rest_command automation (near-zero latency, unlike polling). This
    // slow poll just catches the rare case where a push got missed (device
    // briefly offline, network hiccup) - not the primary sync path anymore,
    // so it can afford to be infrequent and cost almost nothing in sockets.
    static unsigned long lastOverridePoll = 30000; // starts offset from boot
    const unsigned long HA_OVERRIDE_POLL_MS = 300000;
    if (millis() - lastOverridePoll >= HA_OVERRIDE_POLL_MS) {
        lastOverridePoll = millis();
        fetchOverrideFromHA();
    }

    // --- Touch input (every 30ms) ---
    // Fast enough to feel responsive for slider dragging, without hammering
    // the I2C bus on every single loop() pass. Disabled entirely for this
    // diagnostic run - see TOUCH_ENABLED above.
#if TOUCH_ENABLED
    static unsigned long lastTouchPoll = 0;
    const unsigned long TOUCH_POLL_MS = 30;
    if (millis() - lastTouchPoll >= TOUCH_POLL_MS) {
        lastTouchPoll = millis();
        handleTouchInput();
    }
#endif

    // --- SD card data logging ---
    // Internally rate-limited (per-minute rows, daily rollup on day change,
    // SD-absent retry every 60s) - safe and cheap to call every iteration.
    sdLoggerLoop();

    // --- DIAGNOSTIC: heap logging (every 60s) ---
    // Testing the memory-overflow theory directly: a steadily declining
    // free-heap number that bottoms out around the same time as a lockup
    // would confirm a leak/fragmentation issue; a flat number right up
    // until a sudden freeze would rule memory out and point back to some
    // blocking call instead. Remove once the overnight lockup is resolved.
    static unsigned long lastHeapLog = 0;
    const unsigned long HEAP_LOG_PERIOD_MS = 60000;
    if (millis() - lastHeapLog >= HEAP_LOG_PERIOD_MS) {
        lastHeapLog = millis();
        Serial.print("DIAGNOSTIC: uptime=");
        Serial.print(millis() / 1000);
        Serial.print("s  freeHeap=");
        Serial.print(ESP.getFreeHeap());
        Serial.print("  minFreeHeap=");
        Serial.println(ESP.getMinFreeHeap());
    }
}
