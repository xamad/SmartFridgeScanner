#ifndef BARCODE_SCANNER_H
#define BARCODE_SCANNER_H

#include "esp_camera.h"
#include "quirc.h"

// Barcode result structure
struct BarcodeResult {
    bool found;
    String type;
    String data;
};

// Quirc instance for QR decoding
struct quirc *qr = NULL;

// ============ INITIALIZE BARCODE SCANNER ============
void initBarcodeScanner() {
    // Create quirc instance for QR code decoding
    // Camera is already initialized by camera_config.h
    qr = quirc_new();
    if (qr == NULL) {
        Serial.println("[QR] Failed to allocate quirc");
        return;
    }

    // Resize for expected frame size (will be adjusted on first scan)
    if (quirc_resize(qr, 640, 480) < 0) {
        Serial.println("[QR] Failed to resize quirc buffer");
        quirc_destroy(qr);
        qr = NULL;
        return;
    }

    Serial.println("[QR] Scanner initialized (quirc)");
}

// ============ CONVERT JPEG TO GRAYSCALE ============
// ESP32 camera returns JPEG, quirc needs grayscale
bool jpegToGrayscale(camera_fb_t *fb, uint8_t *gray_buf, int width, int height) {
    // For JPEG format, we need to decode first
    // Quirc expects raw grayscale pixels

    if (fb->format == PIXFORMAT_GRAYSCALE) {
        // Already grayscale, just copy
        memcpy(gray_buf, fb->buf, width * height);
        return true;
    }

    // JPEG format - use esp_jpg_decode or simple approximation
    // For now, skip JPEG frames (we configured camera for grayscale effect)
    Serial.println("[QR] Frame is JPEG, skipping decode");
    return false;
}

// ============ SCAN QR CODE ============
BarcodeResult scanQRCode(camera_fb_t *fb) {
    BarcodeResult result;
    result.found = false;

    if (qr == NULL) {
        Serial.println("[QR] Scanner not initialized");
        return result;
    }

    // Get quirc buffer for this frame size
    int w = fb->width;
    int h = fb->height;

    // Resize quirc if needed
    if (quirc_resize(qr, w, h) < 0) {
        Serial.println("[QR] Resize failed");
        return result;
    }

    // Get quirc image buffer
    uint8_t *image = quirc_begin(qr, NULL, NULL);
    if (image == NULL) {
        Serial.println("[QR] Failed to get image buffer");
        return result;
    }

    // Copy frame data (assuming grayscale or convert JPEG)
    if (fb->format == PIXFORMAT_JPEG) {
        // JPEG needs decoding - use ESP32 JPEG decoder
        // For simplicity, we'll use a grayscale approximation from JPEG
        // In production, use esp_jpg_decode()

        // Simple: assume frame is mostly grayscale from camera settings
        // Copy first bytes as-is (won't work well, but shows structure)
        Serial.println("[QR] Warning: JPEG frame, QR detection may fail");
        Serial.println("[QR] Configure camera to PIXFORMAT_GRAYSCALE for better results");

        // For JPEG, we need to decode. Use frame2bmp or similar
        // Skip for now - just fill with zeros
        memset(image, 128, w * h);
    } else if (fb->format == PIXFORMAT_GRAYSCALE) {
        // Direct copy for grayscale
        memcpy(image, fb->buf, w * h);
    } else if (fb->format == PIXFORMAT_RGB565) {
        // Convert RGB565 to grayscale
        uint16_t *rgb = (uint16_t *)fb->buf;
        for (int i = 0; i < w * h; i++) {
            uint16_t pixel = rgb[i];
            uint8_t r = (pixel >> 11) & 0x1F;
            uint8_t g = (pixel >> 5) & 0x3F;
            uint8_t b = pixel & 0x1F;
            // Convert to grayscale (approximate)
            image[i] = (r * 8 + g * 4 + b * 8) / 3;
        }
    } else {
        Serial.printf("[QR] Unsupported format: %d\n", fb->format);
        quirc_end(qr);
        return result;
    }

    // Finish and identify QR codes
    quirc_end(qr);

    int count = quirc_count(qr);
    Serial.printf("[QR] Found %d potential QR regions\n", count);

    for (int i = 0; i < count; i++) {
        struct quirc_code code;
        struct quirc_data data;

        quirc_extract(qr, i, &code);

        quirc_decode_error_t err = quirc_decode(&code, &data);
        if (err == QUIRC_SUCCESS) {
            result.found = true;
            result.type = "QR";
            result.data = String((const char *)data.payload);

            Serial.printf("[QR] Decoded: %s\n", data.payload);
            Serial.printf("[QR] Version: %d, ECC: %c\n", data.version,
                         "MLHQ"[data.ecc_level]);
            break;  // Found one, stop
        } else {
            Serial.printf("[QR] Decode error %d: %s\n", i, quirc_strerror(err));
        }
    }

    return result;
}

// ============ SCAN BARCODE (wrapper) ============
BarcodeResult scanBarcode(camera_fb_t *fb) {
    BarcodeResult result;
    result.found = false;

    Serial.println("[SCAN] Analyzing frame...");
    Serial.printf("[SCAN] Size: %dx%d, Format: %d, Len: %d\n",
                  fb->width, fb->height, fb->format, fb->len);

    // Try QR code first
    result = scanQRCode(fb);

    if (result.found) {
        return result;
    }

    // No QR found - analyze image quality
    Serial.println("[SCAN] No QR found, checking image quality...");

    // For JPEG, we can't easily analyze pixels
    if (fb->format != PIXFORMAT_JPEG) {
        unsigned long brightness = 0;
        int samples = min(1000, (int)fb->len);

        for (int i = 0; i < samples; i++) {
            brightness += fb->buf[i * fb->len / samples];
        }
        brightness /= samples;

        Serial.printf("[SCAN] Brightness: %lu/255\n", brightness);

        if (brightness < 50) {
            Serial.println("[SCAN] Too dark! Enable flash LED");
        } else if (brightness > 200) {
            Serial.println("[SCAN] Too bright! Reduce exposure");
        }
    }

    // For 1D barcodes (EAN, UPC, Code128), need different library
    // TODO: Integrate ZBar or similar for 1D barcode support
    Serial.println("[SCAN] 1D barcode support not implemented");
    Serial.println("[SCAN] For EAN/UPC barcodes, integrate ZBar library");

    return result;
}

// ============ CLEANUP ============
void cleanupBarcodeScanner() {
    if (qr != NULL) {
        quirc_destroy(qr);
        qr = NULL;
    }
}

#endif
