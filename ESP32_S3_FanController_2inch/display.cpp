#include "display.h"
#include "pins.h"
#include "config.h"
#include "sensors.h"
#include "touch.h"
#include "home_assistant.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Ethernet.h>
#include <TimeLib.h> // same clock source as web_server.cpp - see note in updateMainDashboardUI()

#ifndef ST77XX_DARKGRAY
#define ST77XX_DARKGRAY 0x7BEF
#endif

// Panel: 240x320 native (portrait) - rotated to 320x240 landscape below.
static const int PANEL_NATIVE_W = 240;
static const int PANEL_NATIVE_H = 320;
static const int LCD_WIDTH  = 320;  // landscape: width/height swapped after rotation
static const int LCD_HEIGHT = 240;

// Dedicated SPI bus for the onboard LCD, separate from the W5500 SPI bus.
// Note: this bus is physically shared with the SD card (same MOSI/SCLK,
// different CS) - only one device should be selected/active at a time.
static SPIClass LcdSPI(HSPI);
static Adafruit_ST7789 screenMain(&LcdSPI, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);

// LCD and the SD card physically share this SPI bus (same MOSI/SCLK,
// different CS - see pins.h). sd_logger.cpp needs the same SPIClass
// instance, not a second independent one, to avoid bus contention.
SPIClass& getDisplaySPI() { return LcdSPI; }

// Layout (320x240 landscape): tall bar gauges on the left (fans) and right
// (probes, mirrored), text readouts in the center column.
static const int TITLE_H     = 26;
static const int LEFT_ZONE_X = 4;
static const int LEFT_ZONE_W = 88;
static const int RIGHT_ZONE_W = 88;
static const int RIGHT_ZONE_X = LCD_WIDTH - RIGHT_ZONE_W - 4;
static const int CENTER_X    = LEFT_ZONE_X + LEFT_ZONE_W + 8;
static const int CENTER_W    = RIGHT_ZONE_X - 8 - CENTER_X;

static void drawFanRpmBars(int x0, int y0, int zoneWidth, int zoneHeight);
static void drawTempProbeBars(int x0, int y0, int zoneWidth, int zoneHeight);

// Vertical extent of the bar-gauge zones, shared between updateMainDashboardUI()
// (full redraw, 2s cadence) and refreshBarsOnly() (fast redraw, for flashing).
static const int BARS_TOP = TITLE_H + 18;
static int barsHeight() { return LCD_HEIGHT - BARS_TOP - 6; }

// True while the Manual Control override overlay is showing full-screen.
// See handleTouchInput() for the state machine; updateMainDashboardUI()
// and refreshBarsOnly() no-op while this is true.
static bool overlayOpen = false;

// Gauge scales now live in config (config.fanRpmGaugeMin/Max, config.tempGaugeMinF/MaxF)
// so they're adjustable from the LCD settings menu, web page, and Home Assistant
// rather than fixed at compile time.

// Bar geometry, shared between the gauge-drawing functions and the column
// header labels so the labels center over the actual bars, not the zone.
static const int BAR_WIDTH   = 28;
static const int BAR_SPACING = 20;
static const int BAR_SPAN_W  = BAR_WIDTH * 2 + BAR_SPACING; // total width of the 2-bar cluster

// Settings gear icon (Lucide "settings", 24x24, 1-bit), drawn top-right of the title bar.
static const int ICON_SETTINGS_SIZE = 24;
static const unsigned char PROGMEM icon_settings_24x24[] = {
    0x00, 0x00, 0x00,
    0x00, 0x18, 0x00,
    0x00, 0x18, 0x00,
    0x03, 0x7E, 0xC0,
    0x03, 0xFF, 0xC0,
    0x03, 0xC1, 0xC0,
    0x1F, 0xC0, 0xF8,
    0x1E, 0xE0, 0x78,
    0x0C, 0x60, 0x30,
    0x18, 0x7C, 0x18,
    0x18, 0x7E, 0x18,
    0x78, 0x67, 0xFE,
    0x78, 0x67, 0xFE,
    0x18, 0x7E, 0x18,
    0x18, 0x7C, 0x18,
    0x0C, 0x60, 0x30,
    0x1E, 0xE0, 0x78,
    0x1F, 0xC0, 0xF8,
    0x03, 0xC1, 0xC0,
    0x03, 0xFF, 0xC0,
    0x03, 0x7E, 0xC0,
    0x00, 0x18, 0x00,
    0x00, 0x18, 0x00,
    0x00, 0x00, 0x00,
};

// Flash timing for near-limit gauge bars (independent of the 2s dashboard
// refresh - see refreshBarsOnly()).
static const unsigned long BLINK_PERIOD_MS = 500; // ~1Hz flash
static bool blinkPhaseOn() { return (millis() % BLINK_PERIOD_MS) < (BLINK_PERIOD_MS / 2); }

// Bar alert thresholds, as a fraction of the gauge's configured range.
// Firmware-defined, not user-configurable.
static const float BAR_FLASH_THRESHOLD = 0.90; // starts flashing in its normal (identity) color
static const float BAR_RED_THRESHOLD   = 0.95; // flashes red instead

// Decides the fill color and whether this bar should be flashing right now,
// given its identity color (e.g. cyan for fans, orange/magenta for probes).
// Returns true if the fill should be drawn this instant (false = blank/off
// phase of a flash, so the caller skips drawing to create the blink).
static bool resolveBarFill(float value, float minV, float maxV, uint16_t identityColor, uint16_t &outColor) {
    if (maxV <= minV) { outColor = identityColor; return true; }

    float pct = (value - minV) / (maxV - minV);
    if (pct < 0.0) pct = 0.0;

    if (pct >= BAR_RED_THRESHOLD) {
        outColor = ST77XX_RED;
        return blinkPhaseOn();
    }
    if (pct >= BAR_FLASH_THRESHOLD) {
        outColor = identityColor;
        return blinkPhaseOn();
    }
    outColor = identityColor;
    return true;
}

// Small inset between the outline and the fill (keeps fill from touching
// the outline stroke). Outer bar footprint is unchanged either way.
static const int BAR_OUTLINE_INSET = 2;
static const int BAR_CORNER_RADIUS = 10; // rounded pill-style outline

// Draws one gauge bar: thin rounded outline (identityColor), black interior,
// and - if hasValue is true - a single-color fill (identityColor normally;
// flashes per resolveBarFill() near/at the configured max). Fill itself is
// drawn square-edged (only the outer capsule is rounded) - simpler and reads fine.
static void drawGaugeBar(int x, int y, int w, int h, uint16_t identityColor, bool hasValue, float value, float minV, float maxV) {
    screenMain.fillRoundRect(x, y, w, h, BAR_CORNER_RADIUS, ST77XX_BLACK);
    screenMain.drawRoundRect(x, y, w, h, BAR_CORNER_RADIUS, identityColor);

    int innerX = x + BAR_OUTLINE_INSET;
    int innerY = y + BAR_OUTLINE_INSET;
    int innerW = w - 2 * BAR_OUTLINE_INSET;
    int innerH = h - 2 * BAR_OUTLINE_INSET;

    if (!hasValue) return;

    uint16_t fillColor;
    bool drawFillNow = resolveBarFill(value, minV, maxV, identityColor, fillColor);
    if (!drawFillNow) return; // blink "off" phase - leave interior black

    float clampedVal = constrain(value, minV, maxV);
    int fillH = (maxV > minV) ? (int)map((long)(clampedVal * 100), (long)(minV * 100), (long)(maxV * 100), 0, innerH) : 0;

    screenMain.fillRect(innerX, innerY + (innerH - fillH), innerW, fillH, fillColor);
}

// Prints text centered within [x0, x0+w) at the given baseline y, at the given text size.
static void printCentered(int x0, int w, int y, const String &text, uint8_t size = 1) {
    int16_t bx, by;
    uint16_t bw, bh;
    screenMain.setTextSize(size);
    screenMain.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    screenMain.setCursor(x0 + (w - (int)bw) / 2, y);
    screenMain.print(text);
}

// Clears the row, prints label left-aligned and value right-aligned within [x0, x0+w), size 1.
static void printLabelValueRow(int x0, int w, int y, const String &label, const String &value, uint16_t color) {
    screenMain.fillRect(x0, y, w, 10, ST77XX_BLACK);
    screenMain.setTextSize(1);
    screenMain.setTextColor(color, ST77XX_BLACK);

    screenMain.setCursor(x0, y);
    screenMain.print(label);

    int16_t bx, by;
    uint16_t bw, bh;
    screenMain.getTextBounds(value, 0, 0, &bx, &by, &bw, &bh);
    screenMain.setCursor(x0 + w - (int)bw, y);
    screenMain.print(value);
}

// Bottom-of-center-column layout: IP line, and the Manual Control button
// sitting just above it (per spec). Anchored from the bottom so neither
// shifts if the readout rows above grow/shrink.
static const int IP_Y = LCD_HEIGHT - 14;
static const int MANUAL_BTN_H = 14;
static const int MANUAL_BTN_GAP_ABOVE_IP = 4;
static const int MANUAL_BTN_Y = IP_Y - MANUAL_BTN_H - MANUAL_BTN_GAP_ABOVE_IP;

// Draws the Manual Control button: red outline when idle, flashing solid
// red/black when override is active (touch handling to actually press this
// is still pending the touch UI framework - this wires up the visual state
// and reuses the same fast-timer flash as the bar gauges).
static void drawManualControlButton(int x, int y, int w, int h) {
    const int radius = 4;
    if (!manualOverrideActive) {
        screenMain.fillRoundRect(x, y, w, h, radius, ST77XX_BLACK);
        screenMain.drawRoundRect(x, y, w, h, radius, ST77XX_RED);
        screenMain.setTextColor(ST77XX_RED, ST77XX_BLACK);
    } else {
        bool on = blinkPhaseOn();
        uint16_t bg = on ? ST77XX_RED : ST77XX_BLACK;
        uint16_t fg = on ? ST77XX_BLACK : ST77XX_RED;
        screenMain.fillRoundRect(x, y, w, h, radius, bg);
        screenMain.drawRoundRect(x, y, w, h, radius, ST77XX_RED);
        screenMain.setTextColor(fg, bg);
    }
    printCentered(x, w, y + 3, "Manual Control", 1);
}

// Converts a snake_case node ID (e.g. "fan_controller_01") into Title Case
// with spaces (e.g. "Fan Controller 01") for display in the title bar.
static String titleCaseFromNodeId(const char* nodeId) {
    String s(nodeId);
    s.replace('_', ' ');
    bool startOfWord = true;
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] == ' ') { startOfWord = true; continue; }
        s[i] = startOfWord ? toupper(s[i]) : tolower(s[i]);
        startOfWord = false;
    }
    return s;
}

// Redraws the blue title bar with the current Home Assistant node name
// and the settings gear icon (top-right).
static void drawTitleBar() {
    screenMain.fillRect(0, 0, LCD_WIDTH, TITLE_H, ST77XX_BLUE);
    screenMain.setTextColor(ST77XX_WHITE);
    printCentered(0, LCD_WIDTH, 9, titleCaseFromNodeId(config.nodeID));

    int iconX = LCD_WIDTH - ICON_SETTINGS_SIZE - 6;
    int iconY = (TITLE_H - ICON_SETTINGS_SIZE) / 2;
    screenMain.drawBitmap(iconX, iconY, icon_settings_24x24, ICON_SETTINGS_SIZE, ICON_SETTINGS_SIZE, ST77XX_WHITE);
}

// Static labels for the bar gauge zones, centered over the actual bar span
// (not the full zone) so they line up with the bars drawn below. Called
// both at init and every dashboard redraw - the latter matters because the
// override overlay does a full-screen wipe, and this makes the dashboard
// self-healing on close rather than needing special-case redraw logic there.
static void drawColumnHeaders() {
    screenMain.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    printCentered(LEFT_ZONE_X, BAR_SPAN_W, TITLE_H + 4, "Fan RPM");
    printCentered(RIGHT_ZONE_X, BAR_SPAN_W, TITLE_H + 4, "Temperature");
}

void displayInit() {
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);

    // MISO (PIN_SD_MISO) is wired even though the LCD itself never reads
    // data back - the SD card shares this bus and needs it. Omitting it
    // here would leave the bus's MISO line unconfigured, and SD reads
    // would silently fail even though writes might appear to work.
    LcdSPI.begin(PIN_LCD_SCLK, PIN_SD_MISO, PIN_LCD_MOSI, -1);
    delay(20);

    screenMain.init(PANEL_NATIVE_W, PANEL_NATIVE_H);
    screenMain.setRotation(1); // landscape: 320 wide x 240 tall
    screenMain.fillScreen(ST77XX_BLACK);

    drawTitleBar();
    drawColumnHeaders();
}

void updateMainDashboardUI() {
    if (overlayOpen) return; // overlay owns the screen while open - see handleTouchInput()

    int cy = TITLE_H + 6;

    drawTitleBar();
    drawColumnHeaders();

    // --- Date line (centered, size 2) ---
    // Uses TimeLib (same clock as web_server.cpp's timestamps), not
    // getLocalTime()/time.h - that reads the ESP32 core's separate built-in
    // SNTP client, which we never feed via configTime(). This was the cause
    // of the LCD clock showing "syncing..." forever even after TimeLib's
    // own NTP sync (network.cpp's getNtpTime()) was working correctly.
    screenMain.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    screenMain.fillRect(CENTER_X, cy, CENTER_W, 16, ST77XX_BLACK);
    char dStr[16], tStr[16];
    bool haveTime = (timeStatus() != timeNotSet);
    if (haveTime) {
        snprintf(dStr, sizeof(dStr), "%02d/%02d/%02d", month(), day(), year() % 100);
        printCentered(CENTER_X, CENTER_W, cy, dStr, 2);
    } else {
        printCentered(CENTER_X, CENTER_W, cy, "--/--/--", 2);
    }
    cy += 20;

    // --- Time line (centered, size 2) ---
    screenMain.fillRect(CENTER_X, cy, CENTER_W, 16, ST77XX_BLACK);
    if (haveTime) {
        if (config.is24Hour) {
            snprintf(tStr, sizeof(tStr), "%02d:%02d", hour(), minute());
        } else {
            snprintf(tStr, sizeof(tStr), "%d:%02d %s", hourFormat12(), minute(), isAM() ? "AM" : "PM");
        }
        printCentered(CENTER_X, CENTER_W, cy, tStr, 2);
    } else {
        printCentered(CENTER_X, CENTER_W, cy, "syncing...", 2);
    }
    cy += 24;

    // --- Big blended-average temperature (centered) ---
    screenMain.fillRect(CENTER_X, cy, CENTER_W, 26, ST77XX_BLACK);
    if (!localSensorHealthy && !networkSensorHealthy) {
        screenMain.setTextColor(ST77XX_RED, ST77XX_BLACK);
        printCentered(CENTER_X, CENTER_W, cy, "CRIT!", 3);
    } else {
        float dispAvg = config.isFahrenheit ? ((blendedAverageC * 9.0 / 5.0) + 32.0) : blendedAverageC;
        uint16_t tempColor = (blendedAverageC > (config.tMax - 5.0)) ? ST77XX_RED : ST77XX_GREEN;
        screenMain.setTextColor(tempColor, ST77XX_BLACK);
        String tempStr = String(dispAvg, 1) + (config.isFahrenheit ? " F" : " C");
        printCentered(CENTER_X, CENTER_W, cy, tempStr, 3);
    }
    cy += 34;

    screenMain.drawFastHLine(CENTER_X, cy, CENTER_W, ST77XX_DARKGRAY);
    cy += 10;

    // --- RPM + temperature readouts: label left-aligned, value right-aligned ---
    printLabelValueRow(CENTER_X, CENTER_W, cy, "Fan 1:", String(currentRPMs[0]) + " RPM", ST77XX_WHITE);
    cy += 18;

    printLabelValueRow(CENTER_X, CENTER_W, cy, "Fan 2:", String(currentRPMs[1]) + " RPM", ST77XX_WHITE);
    cy += 24;

    float dispLocal = config.isFahrenheit ? ((localTempC * 9.0 / 5.0) + 32.0) : localTempC;
    float dispNet    = config.isFahrenheit ? ((networkTempC * 9.0 / 5.0) + 32.0) : networkTempC;
    const char* unitStr = config.isFahrenheit ? "F" : "C";

    String localVal = localSensorHealthy ? (String(dispLocal, 1) + " " + unitStr) : "---";
    printLabelValueRow(CENTER_X, CENTER_W, cy, "Local:", localVal, localSensorHealthy ? ST77XX_ORANGE : ST77XX_RED);
    cy += 18;

    String netVal = networkSensorHealthy ? (String(dispNet, 1) + " " + unitStr) : "---";
    printLabelValueRow(CENTER_X, CENTER_W, cy, "Net:", netVal, networkSensorHealthy ? ST77XX_MAGENTA : ST77XX_RED);
    cy += 18;

    // --- Average (blended) temperature, yellow - below the two sensor
    // readouts, above where the Manual Control button will sit (bookmarked).
    float dispAvgRow = config.isFahrenheit ? ((blendedAverageC * 9.0 / 5.0) + 32.0) : blendedAverageC;
    String avgLabel = (CENTER_W >= 100) ? "Average:" : "Avg:";
    String avgVal = (localSensorHealthy || networkSensorHealthy) ? (String(dispAvgRow, 1) + " " + unitStr) : "---";
    printLabelValueRow(CENTER_X, CENTER_W, cy, avgLabel, avgVal, ST77XX_YELLOW);

    // --- Manual Control button, just above the IP line ---
    drawManualControlButton(CENTER_X, MANUAL_BTN_Y, CENTER_W, MANUAL_BTN_H);

    // --- IP address, bottom-center of the main section ---
    screenMain.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    screenMain.fillRect(CENTER_X, IP_Y, CENTER_W, 10, ST77XX_BLACK);
    printCentered(CENTER_X, CENTER_W, IP_Y, "IP: " + Ethernet.localIP().toString(), 1);

    // --- Tall bar gauges: fans on the left, probes on the right (mirrored) ---
    drawFanRpmBars(LEFT_ZONE_X, BARS_TOP, LEFT_ZONE_W, barsHeight());
    drawTempProbeBars(RIGHT_ZONE_X, BARS_TOP, RIGHT_ZONE_W, barsHeight());
}

// Fast, bars-only redraw - call on an independent ~150-250ms timer so the
// near-limit flash (see resolveBarFill()) and the Manual Control button's
// active-state flash actually read as a flash rather than crawling along
// at the main dashboard's 2s refresh rate.
void refreshBarsOnly() {
    if (overlayOpen) return; // overlay owns the screen while open

    drawFanRpmBars(LEFT_ZONE_X, BARS_TOP, LEFT_ZONE_W, barsHeight());
    drawTempProbeBars(RIGHT_ZONE_X, BARS_TOP, RIGHT_ZONE_W, barsHeight());
    if (manualOverrideActive) {
        drawManualControlButton(CENTER_X, MANUAL_BTN_Y, CENTER_W, MANUAL_BTN_H);
    }
}

static void drawFanRpmBars(int x0, int y0, int zoneWidth, int zoneHeight) {
    int barsStartX = x0 + (zoneWidth - BAR_SPAN_W) / 2;
    long gaugeMin = config.fanRpmGaugeMin;
    long gaugeMax = config.fanRpmGaugeMax;

    screenMain.fillRect(x0, y0, zoneWidth, zoneHeight, ST77XX_BLACK);

    for (int i = 0; i < 2; i++) {
        int cx = barsStartX + i * (BAR_WIDTH + BAR_SPACING);
        drawGaugeBar(cx, y0, BAR_WIDTH, zoneHeight, ST77XX_CYAN, true,
                     (float)currentRPMs[i], (float)gaugeMin, (float)gaugeMax);
    }
}

static void drawTempProbeBars(int x0, int y0, int zoneWidth, int zoneHeight) {
    int barsStartX = x0 + (zoneWidth - BAR_SPAN_W) / 2;

    // Gauge scale is stored in F; convert to the active display unit.
    float minDispF = config.isFahrenheit ? config.tempGaugeMinF : (config.tempGaugeMinF - 32.0) * 5.0 / 9.0;
    float maxDispF = config.isFahrenheit ? config.tempGaugeMaxF : (config.tempGaugeMaxF - 32.0) * 5.0 / 9.0;

    screenMain.fillRect(x0, y0, zoneWidth, zoneHeight, ST77XX_BLACK);

    float dispLocalF = config.isFahrenheit ? ((localTempC * 9.0 / 5.0) + 32.0) : localTempC;
    drawGaugeBar(barsStartX, y0, BAR_WIDTH, zoneHeight,
                 localSensorHealthy ? ST77XX_ORANGE : ST77XX_RED,
                 localSensorHealthy, dispLocalF, minDispF, maxDispF);

    int x1 = barsStartX + BAR_WIDTH + BAR_SPACING;
    float dispNetF = config.isFahrenheit ? ((networkTempC * 9.0 / 5.0) + 32.0) : networkTempC;
    drawGaugeBar(x1, y0, BAR_WIDTH, zoneHeight,
                 networkSensorHealthy ? ST77XX_MAGENTA : ST77XX_RED,
                 networkSensorHealthy, dispNetF, minDispF, maxDispF);
}

// ============================================================
// Manual Control override overlay - full-screen touch UI
// ============================================================
// A full-screen takeover rather than a true overlay-on-top-of-dashboard:
// with no framebuffer yet (see the bookmarked screen-flicker item), a
// partial overlay would fight the 2s dashboard refresh and 200ms bar
// refresh timers over the same pixels. Full takeover sidesteps that
// entirely - both those redraw functions no-op while overlayOpen is true.

static const int OVERLAY_BOX_X = 30;
static const int OVERLAY_BOX_Y = 40;
static const int OVERLAY_BOX_W = 260;
static const int OVERLAY_BOX_H = 160;

static const int OVERLAY_SLIDER_X = OVERLAY_BOX_X + 10;
static const int OVERLAY_SLIDER_Y = OVERLAY_BOX_Y + 60;
static const int OVERLAY_SLIDER_W = OVERLAY_BOX_W - 20;
static const int OVERLAY_SLIDER_H = 16;

static const int OVERLAY_BTN_W = (OVERLAY_BOX_W - 30) / 2;
static const int OVERLAY_BTN_H = 26;
static const int OVERLAY_BTN_Y = OVERLAY_BOX_Y + OVERLAY_BOX_H - 36;
static const int OVERLAY_CANCEL_X = OVERLAY_BOX_X + 10;
static const int OVERLAY_KEEPON_X = OVERLAY_CANCEL_X + OVERLAY_BTN_W + 10;

// Redraws just the percentage readout + slider fill - called on every
// touch-drag update, so it's kept cheap (no box/button redraw needed).
static void drawOverlaySliderOnly() {
    int pct = (manualOverrideDutyCycle * 100) / 255;
    String pctStr = "Fan Speed: " + String(pct) + "%";
    screenMain.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    screenMain.fillRect(OVERLAY_BOX_X, OVERLAY_BOX_Y + 28, OVERLAY_BOX_W, 12, ST77XX_BLACK);
    printCentered(OVERLAY_BOX_X, OVERLAY_BOX_W, OVERLAY_BOX_Y + 28, pctStr, 1);

    screenMain.fillRect(OVERLAY_SLIDER_X + 1, OVERLAY_SLIDER_Y + 1, OVERLAY_SLIDER_W - 2, OVERLAY_SLIDER_H - 2, ST77XX_BLACK);
    int fillW = (OVERLAY_SLIDER_W - 2) * manualOverrideDutyCycle / 255;
    screenMain.fillRect(OVERLAY_SLIDER_X + 1, OVERLAY_SLIDER_Y + 1, fillW, OVERLAY_SLIDER_H - 2, ST77XX_CYAN);
}

// Full overlay draw - called once when it opens.
static void drawOverrideOverlay() {
    screenMain.fillScreen(ST77XX_BLACK);
    screenMain.drawRoundRect(OVERLAY_BOX_X, OVERLAY_BOX_Y, OVERLAY_BOX_W, OVERLAY_BOX_H, 8, ST77XX_CYAN);

    screenMain.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    printCentered(OVERLAY_BOX_X, OVERLAY_BOX_W, OVERLAY_BOX_Y + 10, "MANUAL OVERRIDE", 1);

    screenMain.drawRoundRect(OVERLAY_SLIDER_X, OVERLAY_SLIDER_Y, OVERLAY_SLIDER_W, OVERLAY_SLIDER_H, 6, ST77XX_WHITE);
    drawOverlaySliderOnly();

    screenMain.drawRoundRect(OVERLAY_CANCEL_X, OVERLAY_BTN_Y, OVERLAY_BTN_W, OVERLAY_BTN_H, 4, ST77XX_RED);
    screenMain.setTextColor(ST77XX_RED, ST77XX_BLACK);
    printCentered(OVERLAY_CANCEL_X, OVERLAY_BTN_W, OVERLAY_BTN_Y + 8, "CANCEL", 1);

    screenMain.drawRoundRect(OVERLAY_KEEPON_X, OVERLAY_BTN_Y, OVERLAY_BTN_W, OVERLAY_BTN_H, 4, ST77XX_GREEN);
    screenMain.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    printCentered(OVERLAY_KEEPON_X, OVERLAY_BTN_W, OVERLAY_BTN_Y + 8, "KEEP ON", 1);
}

static bool pointInRect(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

bool isOverlayOpen() { return overlayOpen; }

void handleTouchInput() {
    static bool wasPressed = false;

    int tx, ty;
    bool pressed = getTouchPoint(tx, ty);

    if (overlayOpen) {
        if (pressed) {
            // Generous vertical tolerance around the slider track makes it
            // easier to grab with a fingertip than the visual track height alone.
            bool inSliderZone = pointInRect(tx, ty - 10, OVERLAY_SLIDER_X, OVERLAY_SLIDER_Y, OVERLAY_SLIDER_W, OVERLAY_SLIDER_H + 20);

            if (inSliderZone) {
                int rel = constrain(tx - OVERLAY_SLIDER_X, 0, OVERLAY_SLIDER_W);
                manualOverrideDutyCycle = map(rel, 0, OVERLAY_SLIDER_W, 0, 255);
                drawOverlaySliderOnly();
            } else if (!wasPressed) {
                // Buttons only fire on the initial press edge, not every
                // poll while held, so a lingering finger doesn't double-fire.
                if (pointInRect(tx, ty, OVERLAY_CANCEL_X, OVERLAY_BTN_Y, OVERLAY_BTN_W, OVERLAY_BTN_H)) {
                    manualOverrideActive = false; // revert to auto
                    overlayOpen = false;
                    pushOverrideToHA();
                } else if (pointInRect(tx, ty, OVERLAY_KEEPON_X, OVERLAY_BTN_Y, OVERLAY_BTN_W, OVERLAY_BTN_H)) {
                    overlayOpen = false; // stays active; button keeps flashing (see refreshBarsOnly())
                    pushOverrideToHA(); // pushes the final slider value HA hasn't seen yet
                }
            }
        }
    } else {
        // Dashboard idle: only the Manual Control button is touch-active,
        // and only on the initial press edge.
        if (pressed && !wasPressed) {
            if (pointInRect(tx, ty, CENTER_X, MANUAL_BTN_Y, CENTER_W, MANUAL_BTN_H)) {
                if (!manualOverrideActive) {
                    manualOverrideActive = true;
                    manualOverrideDutyCycle = 255; // default full speed per spec
                    overlayOpen = true;
                    drawOverrideOverlay();
                    pushOverrideToHA();
                } else {
                    manualOverrideActive = false; // tap while flashing turns override off directly
                    pushOverrideToHA();
                }
            }
        }
    }

    wasPressed = pressed;
}
