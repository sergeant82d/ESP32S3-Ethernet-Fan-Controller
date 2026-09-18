#include "display.h"
#include "pins.h"
#include "config.h"
#include "sensors.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Ethernet.h>
#include <TimeLib.h>[cite: 4]
#include <Fonts/FreeSansBold24pt7b.h>[cite: 4]
#include <Fonts/FreeSansBold12pt7b.h>

// Dedicated SPI1 for LCD
static SPIClassRP2040 LcdSPI(spi1, LCD_MISO, LCD_CS, LCD_SCLK, LCD_MOSI);
static Adafruit_ST7789 screenMain(&LcdSPI, LCD_CS, LCD_DC, LCD_RST);

void displayInit() {
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);

    LcdSPI.begin();
    // 1.69" 280x240 initialization
    screenMain.init(240, 280);
    screenMain.setRotation(3); // Landscape: 280 wide x 240 tall
    screenMain.fillScreen(ST77XX_BLACK);
}

void updateDashboardUI() {
    screenMain.fillScreen(ST77XX_BLACK);

    // 1. Clock (Top Center)
    screenMain.setFont(&FreeSansBold12pt7b);
    screenMain.setTextColor(ST77XX_YELLOW);
    char timeStr[16];
    if (timeStatus() != timeNotSet) {
        if (config.is24Hour) {
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", hour(), minute(), second());
        } else {
            snprintf(timeStr, sizeof(timeStr), "%d:%02d %s", hourFormat12(), minute(), isAM() ? "AM" : "PM");[cite: 4]
        }
    } else {
        strcpy(timeStr, "Syncing...");
    }
    screenMain.setCursor(60, 25);
    screenMain.print(timeStr);

    // 2. Large Main Temperature (Hero Number)
    screenMain.setFont(&FreeSansBold24pt7b);
    float dispAvg = config.isFahrenheit ? ((blendedAverageC * 1.8f) + 32.0f) : blendedAverageC;[cite: 4, 21]
    uint16_t tempColor = (blendedAverageC > (config.tMax - 5.0f)) ? ST77XX_RED : ST77XX_GREEN;[cite: 4]
    screenMain.setTextColor(tempColor);
    
    char avgBuf[8];
    snprintf(avgBuf, sizeof(avgBuf), "%d", (int)round(dispAvg));[cite: 21, 22]
    screenMain.setCursor(95, 115);
    screenMain.print(avgBuf);

    screenMain.setFont(&FreeSansBold12pt7b);
    screenMain.setCursor(170, 95);
    screenMain.print(config.isFahrenheit ? "oF" : "oC");[cite: 21]

    // 3. Telemetry Readouts (Labeled Simple Numbers)
    screenMain.setFont(NULL); // System font
    screenMain.setTextSize(2);
    screenMain.setTextColor(ST77XX_CYAN);
    screenMain.setCursor(15, 160);
    screenMain.printf("F1: %lu RPM", currentRPMs[0]);[cite: 21]

    screenMain.setCursor(150, 160);
    screenMain.printf("F2: %lu RPM", currentRPMs[1]);[cite: 21]

    screenMain.setTextColor(ST77XX_ORANGE);
    screenMain.setCursor(15, 185);
    float dispLocal = config.isFahrenheit ? ((localTempC * 1.8f) + 32.0f) : localTempC;[cite: 4, 21]
    screenMain.printf("Probe Temp: %d %c", (int)round(dispLocal), config.isFahrenheit ? 'F' : 'C');[cite: 21]

    // 4. Footer (IP Address and Date)
    screenMain.setTextSize(1);
    screenMain.setTextColor(ST77XX_WHITE);
    screenMain.setCursor(15, 225);
    screenMain.print("IP: ");
    screenMain.print(Ethernet.localIP().toString());

    screenMain.setCursor(190, 225);
    if (timeStatus() != timeNotSet) {
        screenMain.printf("%04d-%02d-%02d", year(), month(), day());
    } else {
        screenMain.print("----/--/--");[cite: 4]
    }
}