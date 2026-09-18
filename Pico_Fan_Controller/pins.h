#ifndef PINS_H
#define PINS_H

// =========================================================================
// WIZnet W5500-EVB-Pico / W6100-EVB-Pico2 Pin Mapping (RP2040 / RP2350)
// =========================================================================

// ---- Hardwired Onboard Ethernet (SPI0) ----
#define WIZNET_SCK      18
#define WIZNET_MOSI     19
#define WIZNET_MISO     16
#define WIZNET_CS       17
#define WIZNET_RST      20
#define WIZNET_INT      21

// ---- 1.69" ST7789 LCD Display (SPI1) ----
#define LCD_SCLK        10  // SPI1 SCK
#define LCD_MOSI        11  // SPI1 TX
#define LCD_DC           8  // Data / Command
#define LCD_CS           9  // SPI1 CSn
#define LCD_RST         12  // Display Hardware Reset
#define LCD_BL          13  // Backlight PWM/Switch

// Panel Dimensions: 1.69-inch ST7789 is typically 280x240
#define LCD_WIDTH       280
#define LCD_HEIGHT      240

// ---- PWM Fan Outputs (RP2040 PWM Slices) ----
#define FAN1_PWM_PIN    2
#define FAN2_PWM_PIN    3
#define PWM_FREQ_HZ     25000   // Noctua/Intel 25 kHz specification[cite: 20]

// ---- Tachometer Interrupt Pins ----
#define TACH1_PIN       4
#define TACH2_PIN       5

// ---- DS18B20 OneWire Bus ----
#define ONEWIRE_PIN     6

#define REFRESH_PERIOD_MS 2000

#endif // PINS_H