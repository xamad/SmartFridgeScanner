#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

#include "esp_camera.h"

// ============ CAMERA INITIALIZATION ============
bool initCamera() {
    camera_config_t config;
    
    config.ledc_channel = LEDC_CHANNEL_1;
    config.ledc_timer = LEDC_TIMER_1;
    config.pin_pwdn = -1;
    config.pin_reset = -1;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE; // Grayscale for better barcode detection
    config.grab_mode = CAMERA_GRAB_LATEST;
    
    #if defined(BOARD_ESP32S3)
        // ESP32-S3 Pinout (Freenove/Generic S3-CAM)
        config.pin_d0 = 11;
        config.pin_d1 = 9;
        config.pin_d2 = 8;
        config.pin_d3 = 10;
        config.pin_d4 = 12;
        config.pin_d5 = 18;
        config.pin_d6 = 17;
        config.pin_d7 = 16;
        config.pin_xclk = 15;
        config.pin_pclk = 13;
        config.pin_vsync = 6;
        config.pin_href = 7;
        config.pin_sccb_sda = 4;  // SCCB = I2C per camera
        config.pin_sccb_scl = 5;
        
        // Higher quality for S3
        config.frame_size = FRAMESIZE_SVGA;  // 800x600
        config.jpeg_quality = 8;
        config.fb_count = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        
    #elif defined(BOARD_ESP32CAM)
        // ESP32-CAM AI-Thinker Pinout
        config.pin_d0 = 5;
        config.pin_d1 = 18;
        config.pin_d2 = 19;
        config.pin_d3 = 21;
        config.pin_d4 = 36;
        config.pin_d5 = 39;
        config.pin_d6 = 34;
        config.pin_d7 = 35;
        config.pin_xclk = 0;
        config.pin_pclk = 22;
        config.pin_vsync = 25;
        config.pin_href = 23;
        config.pin_sccb_sda = 26;  // Era pin_sda in Core 2.x
        config.pin_sccb_scl = 27;  // Era pin_scl in Core 2.x
        config.pin_pwdn = 32;
        
        // Optimized for ESP32-CAM
        config.frame_size = FRAMESIZE_VGA;   // 640x480
        config.jpeg_quality = 10;
        config.fb_count = 1;
    #endif
    
    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if(err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        return false;
    }
    
    // Optimize sensor for barcode scanning
    sensor_t *s = esp_camera_sensor_get();
    s->set_brightness(s, 0);       // Neutral
    s->set_contrast(s, 2);         // High contrast
    s->set_saturation(s, -2);      // Low saturation (B&W)
    s->set_sharpness(s, 2);        // Max sharpness
    s->set_denoise(s, 0);          // No denoise
    s->set_special_effect(s, 2);   // Grayscale
    s->set_wb_mode(s, 0);          // Manual WB
    s->set_awb_gain(s, 0);         // No auto gain
    s->set_aec_value(s, 300);      // Fixed exposure
    s->set_gain_ctrl(s, 0);        // Manual gain
    s->set_agc_gain(s, 5);         // Low gain
    
    return true;
}

#endif
