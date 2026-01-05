#ifndef LED_FEEDBACK_H
#define LED_FEEDBACK_H

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    #define USE_NEW_LEDC_API
#endif

#include "driver/dac.h"

#if defined(BOARD_ESP32S3)
    #define FLASH_LED 48
    #define LED_GREEN 48
    #define LED_RED 48
    #define SPEAKER_PIN 17
#elif defined(BOARD_WROVER)
    #define FLASH_LED 4
    #define LED_GREEN 2
    #define LED_RED 4
    #define SPEAKER_PIN 25
#else
    #define FLASH_LED 4
    #define LED_GREEN 33
    #define LED_RED 4
    #define SPEAKER_PIN 25
#endif

#define PWM_CHANNEL 2  // Use channel 2 to avoid conflict with camera (uses 0)
#define SPEAKER_CHANNEL DAC_CHANNEL_1

void initLED() {
    #ifdef USE_NEW_LEDC_API
        ledcAttach(FLASH_LED, 5000, 8);
    #else
        ledcSetup(PWM_CHANNEL, 5000, 8);
        ledcAttachPin(FLASH_LED, PWM_CHANNEL);
    #endif
    ledcWrite(FLASH_LED, 0);
    
    #if SOC_DAC_SUPPORTED
        dac_output_enable(SPEAKER_CHANNEL);
        dac_output_voltage(SPEAKER_CHANNEL, 0);
    #endif
}

void ledModeIn() {
    ledcWrite(FLASH_LED, 50);
}

void ledModeOut() {
    ledcWrite(FLASH_LED, 200);
}

void ledGreen() {
    ledcWrite(FLASH_LED, 50);
}

void ledRed() {
    ledcWrite(FLASH_LED, 200);
}

void ledOff() {
    ledcWrite(FLASH_LED, 0);
}

void flashOn() {
    ledcWrite(FLASH_LED, 255);
}

void flashOff() {
    ledcWrite(FLASH_LED, 0);
}

void ledBlink(int times, int onTime, int offTime) {
    for(int i=0; i<times; i++) {
        ledcWrite(FLASH_LED, 255);
        delay(onTime);
        ledcWrite(FLASH_LED, 0);
        delay(offTime);
    }
}

void ledProcessing() {
    for(int i=0; i<5; i++) {
        ledcWrite(FLASH_LED, 255);
        delay(100);
        ledcWrite(FLASH_LED, 0);
        delay(100);
    }
}

void ledSuccess() {
    for(int i=0; i<3; i++) {
        ledcWrite(FLASH_LED, 255);
        delay(200);
        ledcWrite(FLASH_LED, 0);
        delay(200);
    }
}

void ledError() {
    for(int i=0; i<5; i++) {
        ledcWrite(FLASH_LED, 255);
        delay(100);
        ledcWrite(FLASH_LED, 0);
        delay(100);
    }
}

void speakerBeep(int frequency, int duration) {
    #if SOC_DAC_SUPPORTED
        int halfPeriod = 1000000 / frequency / 2;
        int cycles = (duration * 1000) / (halfPeriod * 2);
        for(int i=0; i<cycles; i++) {
            dac_output_voltage(SPEAKER_CHANNEL, 200);
            delayMicroseconds(halfPeriod);
            dac_output_voltage(SPEAKER_CHANNEL, 55);
            delayMicroseconds(halfPeriod);
        }
        dac_output_voltage(SPEAKER_CHANNEL, 0);
    #else
        Serial.printf("Beep: %dHz, %dms\n", frequency, duration);
    #endif
}

void speakerSuccess() {
    speakerBeep(1000, 100); delay(50);
    speakerBeep(1500, 100); delay(50);
    speakerBeep(2000, 150);
}

void speakerError() {
    speakerBeep(500, 200); delay(100);
    speakerBeep(400, 200); delay(100);
    speakerBeep(300, 200);
}

#endif
