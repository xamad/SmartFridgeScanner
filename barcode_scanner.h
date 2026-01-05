#ifndef BARCODE_SCANNER_H
#define BARCODE_SCANNER_H

#include "quirc.h"
#include "esp_camera.h"

// Barcode result structure
struct BarcodeResult {
    bool found;
    String type;    // "QR", "EAN13", "CODE128", etc.
    String data;
};

// Global quirc instance
struct quirc *qr = NULL;

// ============ INITIALIZE BARCODE SCANNER ============
void initBarcodeScanner() {
    qr = quirc_new();
    if(!qr) {
        Serial.println("Errore: impossibile creare decoder Quirc");
        return;
    }
    
    // Resize for VGA
    if(quirc_resize(qr, 640, 480) < 0) {
        Serial.println("Errore: quirc resize fallito");
        quirc_destroy(qr);
        qr = NULL;
    }
}

// ============ SCAN BARCODE/QR ============
BarcodeResult scanBarcode(camera_fb_t *fb) {
    BarcodeResult result;
    result.found = false;
    
    if(!qr) {
        Serial.println("Scanner non inizializzato!");
        return result;
    }
    
    // Get quirc image buffer
    uint8_t *image = quirc_begin(qr, NULL, NULL);
    
    // Copy grayscale frame to quirc
    if(fb->format == PIXFORMAT_GRAYSCALE) {
        memcpy(image, fb->buf, fb->len);
    } else {
        // Convert JPEG to grayscale (simplified)
        // In produzione usa libjpeg per decodifica corretta
        Serial.println("Formato non grayscale, conversione necessaria");
        // TODO: implement JPEG decode
        return result;
    }
    
    // Finish quirc processing
    quirc_end(qr);
    
    // Get number of detected codes
    int count = quirc_count(qr);
    Serial.printf("Codici rilevati: %d\n", count);
    
    if(count == 0) {
        return result;
    }
    
    // Decode first code found
    for(int i = 0; i < count; i++) {
        struct quirc_code code;
        struct quirc_data data;
        
        quirc_extract(qr, i, &code);
        
        quirc_decode_error_t err = quirc_decode(&code, &data);
        
        if(err == QUIRC_SUCCESS) {
            result.found = true;
            result.type = "QR";
            result.data = String((char*)data.payload);
            
            Serial.printf("QR Version: %d\n", data.version);
            Serial.printf("ECC Level: %c\n", "MLHQ"[data.ecc_level]);
            Serial.printf("Mask: %d\n", data.mask);
            Serial.printf("Data type: %d\n", data.data_type);
            Serial.printf("Payload: %s\n", data.payload);
            
            break;
        } else {
            Serial.printf("Decode error: %s\n", quirc_strerror(err));
        }
    }
    
    // If QR not found, try EAN13 detection (simple algorithm)
    if(!result.found) {
        result = detectEAN13(fb);
    }
    
    return result;
}

// ============ SIMPLE EAN13 DETECTION ============
BarcodeResult detectEAN13(camera_fb_t *fb) {
    BarcodeResult result;
    result.found = false;
    
    // Simplified EAN13 scanner using edge detection
    // In produzione usa ZXing C++ o ZBar
    
    Serial.println("Tentativo rilevamento EAN13...");
    
    // TODO: Implement proper EAN13/Code128 scanner
    // Placeholder: simula detection per testing
    
    // Per ora ritorna non trovato
    return result;
}

// ============ CLEANUP ============
void cleanupBarcodeScanner() {
    if(qr) {
        quirc_destroy(qr);
        qr = NULL;
    }
}

#endif
