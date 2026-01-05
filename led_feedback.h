#ifndef LED_FEEDBACK_H
#define LED_FEEDBACK_H

#include "driver/ledc.h"
#include "driver/dac.h"

// ============ PIN DEFINITIONS ============
#if defined(BOARD_ESP32S3)
    #define FLASH_LED 48
    #define SPEAKER_PIN 25  // DAC1 (GPIO 25 on S3)
#else
    #define FLASH_LED 4
    #define SPEAKER_PIN 25  // DAC1 (GPIO 25 on ESP32)
#endif

#define PWM_CHANNEL 0
#define SPEAKER_CHANNEL DAC_CHANNEL_1

// ============ LED INITIALIZATION ============
void initLED() {
    // Setup PWM for flash LED
    ledcSetup(PWM_CHANNEL, 5000, 8);  // 5kHz, 8-bit
    ledcAttachPin(FLASH_LED, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);
    
    // Setup DAC for speaker (optional)
    dac_output_enable(SPEAKER_CHANNEL);
    dac_output_voltage(SPEAKER_CHANNEL, 0);
}

// ============ LED COLORS (PWM Simulation) ============
void ledGreen() {
    ledcWrite(PWM_CHANNEL, 77);  // 30% = "green"
}

void ledRed() {
    ledcWrite(PWM_CHANNEL, 255); // 100% = "red"
}

void ledOff() {
    ledcWrite(PWM_CHANNEL, 0);
}

void flashOn() {
    ledcWrite(PWM_CHANNEL, 255);
}

void flashOff() {
    ledcWrite(PWM_CHANNEL, 0);
}

// ============ LED PATTERNS ============
void ledBlink(int times, int onTime, int offTime) {
    for(int i=0; i<times; i++) {
        ledcWrite(PWM_CHANNEL, 255);
        delay(onTime);
        ledcWrite(PWM_CHANNEL, 0);
        delay(offTime);
    }
}

void ledProcessing() {
    for(int i=0; i<10; i++) {
        ledcWrite(PWM_CHANNEL, 255);
        delay(100);
        ledcWrite(PWM_CHANNEL, 0);
        delay(100);
    }
}

void ledSuccess() {
    for(int i=0; i<3; i++) {
        ledcWrite(PWM_CHANNEL, 255);
        delay(200);
        ledcWrite(PWM_CHANNEL, 0);
        delay(200);
    }
}

void ledError() {
    for(int i=0; i<5; i++) {
        ledcWrite(PWM_CHANNEL, 255);
        delay(500);
        ledcWrite(PWM_CHANNEL, 0);
        delay(500);
    }
}

// ============ SPEAKER FUNCTIONS (DAC) ============
void speakerBeep(int frequency, int duration) {
    int halfPeriod = 1000000 / frequency / 2;
    int cycles = (duration * 1000) / (halfPeriod * 2);
    
    for(int i=0; i<cycles; i++) {
        dac_output_voltage(SPEAKER_CHANNEL, 200);
        delayMicroseconds(halfPeriod);
        dac_output_voltage(SPEAKER_CHANNEL, 55);
        delayMicroseconds(halfPeriod);
    }
    
    dac_output_voltage(SPEAKER_CHANNEL, 0);
}

void speakerSuccess() {
    speakerBeep(1000, 100);
    delay(50);
    speakerBeep(1500, 100);
    delay(50);
    speakerBeep(2000, 150);
}

void speakerError() {
    speakerBeep(500, 200);
    delay(100);
    speakerBeep(400, 200);
    delay(100);
    speakerBeep(300, 200);
}

#endif
