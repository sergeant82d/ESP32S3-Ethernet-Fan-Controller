#include "sensors.h"
#include "pins.h"
#include "config.h"
#include <OneWire.h>[cite: 14]
#include <DallasTemperature.h>[cite: 14]
#include <hardware/pwm.h>
#include <hardware/clocks.h>

const int NUM_FANS = 2;
unsigned long currentRPMs[NUM_FANS] = {0, 0};[cite: 14]
volatile unsigned long tachCounts[NUM_FANS] = {0, 0};[cite: 14]

float localTempC = 0.0f;[cite: 14]
float blendedAverageC = 0.0f;[cite: 14]

static OneWire oneWire(ONEWIRE_PIN);[cite: 14]
static DallasTemperature dallasSensors(&oneWire);[cite: 14]

static void tachISR0() { tachCounts[0]++; }
static void tachISR1() { tachCounts[1]++; }

static void initPWM(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(pin);

    // RP2040 sys_clk default is 125 MHz. 
    // 125,000,000 / (25,000 Hz * 1000 wrap) = 5 divider
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 5.0f);
    pwm_config_set_wrap(&cfg, 1000);
    pwm_init(slice_num, &cfg, true);
}

static void writeDuty(uint pin, uint8_t duty255) {
    uint slice_num = pwm_gpio_to_slice_num(pin);
    uint channel = pwm_gpio_to_channel(pin);
    // Scale 0-255 -> 0-1000 wrap
    pwm_set_chan_level(slice_num, channel, (duty255 * 1000) / 255);
}

void sensorsInit() {
    dallasSensors.begin();[cite: 14]
    
    initPWM(FAN1_PWM_PIN);
    initPWM(FAN2_PWM_PIN);

    pinMode(TACH1_PIN, INPUT_PULLUP);[cite: 14]
    pinMode(TACH2_PIN, INPUT_PULLUP);[cite: 14]
    attachInterrupt(digitalPinToInterrupt(TACH1_PIN), tachISR0, FALLING);[cite: 14]
    attachInterrupt(digitalPinToInterrupt(TACH2_PIN), tachISR1, FALLING);[cite: 14]
}

void sampleSensors() {
    dallasSensors.requestTemperatures();[cite: 14]
    float raw = dallasSensors.getTempCByIndex(0);[cite: 14]
    if (raw > -50.0f && raw != 85.0f && raw != -127.0f) {[cite: 14]
        localTempC = raw;[cite: 14]
        blendedAverageC = localTempC;[cite: 14]
    }
}

void calculateFanSpeeds() {
    int duty = 0;
    if (blendedAverageC >= config.tMax) duty = 255;[cite: 14]
    else if (blendedAverageC <= config.tMin) duty = 0;[cite: 14]
    else {
        duty = (int)(51.0f + ((blendedAverageC - config.tMin) / (config.tMax - config.tMin)) * 204.0f);[cite: 14]
    }
    writeDuty(FAN1_PWM_PIN, duty);
    writeDuty(FAN2_PWM_PIN, duty);
}

void calculateRPMs(unsigned long elapsedMs) {
    for (int i = 0; i < NUM_FANS; i++) {
        noInterrupts();[cite: 14]
        unsigned long pulses = tachCounts[i];[cite: 14]
        tachCounts[i] = 0;[cite: 14]
        interrupts();[cite: 14]
        currentRPMs[i] = (pulses * 60000UL) / (2 * elapsedMs);[cite: 14]
    }
}