#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

#include "esp_camera.h"

// ============ AI-THINKER ESP32-CAM PINOUT ============
#if defined(BOARD_ESP32CAM) || defined(BOARD_WROVER)

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ============ ESP32-S3 WROOM CAM PINOUT (Freenove/Generic) ============
#elif defined(BOARD_ESP32S3)

#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM      4
#define SIOC_GPIO_NUM      5

#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM        8
#define Y3_GPIO_NUM        9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM     6
#define HREF_GPIO_NUM      7
#define PCLK_GPIO_NUM     13

#else
#error "No board defined! Define BOARD_ESP32CAM, BOARD_WROVER, or BOARD_ESP32S3"
#endif

// ============ CAMERA INITIALIZATION ============
bool initCamera() {
    // Important: Small delay for camera power stabilization
    delay(100);

    camera_config_t config;

    // LEDC settings (use channel 0 like working firmware)
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    // Pin configuration
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;

    // Clock and format
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;  // Grayscale optimal for QR/barcode detection
    config.grab_mode = CAMERA_GRAB_LATEST;

    // Frame size and buffer based on PSRAM
    // Note: jpeg_quality only applies to JPEG format, ignored for GRAYSCALE
    if (psramFound()) {
        Serial.println("[CAM] PSRAM found - using XGA 1024x768");
        config.frame_size = FRAMESIZE_XGA;      // 1024x768 for better barcode/OCR
        config.jpeg_quality = 10;               // Not used for grayscale
        config.fb_count = 2;                    // Double buffer for smooth scanning
        config.fb_location = CAMERA_FB_IN_PSRAM;
    } else {
        Serial.println("[CAM] No PSRAM - using VGA 640x480");
        config.frame_size = FRAMESIZE_VGA;      // 640x480 fallback
        config.jpeg_quality = 12;               // Not used for grayscale
        config.fb_count = 1;                    // Single buffer (limited RAM)
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[CAM] Init failed: 0x%x\n", err);

        // Retry once after power cycle
        if (PWDN_GPIO_NUM != -1) {
            Serial.println("[CAM] Retrying with power cycle...");
            pinMode(PWDN_GPIO_NUM, OUTPUT);
            digitalWrite(PWDN_GPIO_NUM, HIGH);  // Power off
            delay(100);
            digitalWrite(PWDN_GPIO_NUM, LOW);   // Power on
            delay(100);

            err = esp_camera_init(&config);
            if (err != ESP_OK) {
                Serial.printf("[CAM] Retry failed: 0x%x\n", err);
                return false;
            }
        } else {
            return false;
        }
    }

    // Get sensor and optimize for barcode scanning
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        Serial.println("[CAM] Failed to get sensor");
        return false;
    }

    // Sensor optimization for barcode/QR detection
    s->set_brightness(s, 1);        // Slightly bright
    s->set_contrast(s, 2);          // High contrast (important for barcodes)
    s->set_saturation(s, -2);       // Low saturation (more B&W like)
    s->set_sharpness(s, 2);         // Max sharpness (important for barcodes)
    s->set_denoise(s, 0);           // No denoise (preserve edges)
    s->set_special_effect(s, 2);    // Grayscale effect
    s->set_whitebal(s, 1);          // Auto white balance
    s->set_awb_gain(s, 1);          // AWB gain
    s->set_wb_mode(s, 0);           // Auto WB mode
    s->set_exposure_ctrl(s, 1);     // Auto exposure
    s->set_aec2(s, 0);              // Disable AEC DSP
    s->set_gain_ctrl(s, 1);         // Auto gain
    s->set_agc_gain(s, 0);          // AGC gain
    s->set_bpc(s, 1);               // Black pixel correction
    s->set_wpc(s, 1);               // White pixel correction
    s->set_raw_gma(s, 1);           // Gamma correction
    s->set_lenc(s, 1);              // Lens correction
    s->set_hmirror(s, 0);           // No horizontal mirror
    s->set_vflip(s, 0);             // No vertical flip
    s->set_dcw(s, 1);               // Downsize enable

    Serial.printf("[CAM] Sensor: %s\n", s->id.PID == OV2640_PID ? "OV2640" :
                                        s->id.PID == OV5640_PID ? "OV5640" : "Unknown");
    Serial.printf("[CAM] Resolution: %dx%d\n",
                  config.frame_size == FRAMESIZE_XGA ? 1024 : 640,
                  config.frame_size == FRAMESIZE_XGA ? 768 : 480);

    return true;
}

#endif
