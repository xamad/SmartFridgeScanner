#ifndef BARCODE_SCANNER_H
#define BARCODE_SCANNER_H

#include "esp_camera.h"
#include <ESP32QRCodeReader.h>

ESP32QRCodeReader reader(CAMERA_GRAB_LATEST);

// Barcode result structure
struct BarcodeResult {
    bool found;
    String type;
    String data;
};

// ============ INITIALIZE BARCODE SCANNER ============
void initBarcodeScanner() {
    reader.setup();
    Serial.println("✓ ESP32QRCodeReader inizializzato");
}

// ============ SCAN BARCODE/QR ============
BarcodeResult scanBarcode(camera_fb_t *fb) {
    BarcodeResult result;
    result.found = false;
    
    Serial.println("🔍 Scansione QR/Barcode...");
    
    struct QRCodeData qrCodeData;
    
    // Prova a leggere QR code (timeout 1000ms)
    if(reader.receiveQrCode(&qrCodeData, 1000)) {
        result.found = true;
        result.type = "QR";
        result.data = String((const char*)qrCodeData.payload);
        
        Serial.printf("✅ QR Code trovato: %s\n", qrCodeData.payload);
        Serial.printf("   Version: %d, ECC: %d\n", qrCodeData.version, qrCodeData.eccLevel);
        
        return result;
    }
    
    // Se QR non trovato, prova analisi immagine base
    Serial.println("⚠️  QR non trovato, analisi immagine...");
    
    // Validazione qualità immagine
    if(fb->format == PIXFORMAT_GRAYSCALE && fb->len > 0) {
        unsigned long brightness = 0;
        int samples = min(1000, (int)fb->len);
        
        for(int i=0; i<samples; i++) {
            brightness += fb->buf[i];
        }
        brightness /= samples;
        
        Serial.printf("   Luminosità: %lu/255\n", brightness);
        
        if(brightness < 30) {
            Serial.println("   ⚠️  Immagine troppo scura!");
        } else if(brightness > 230) {
            Serial.println("   ⚠️  Immagine sovraesposta!");
        }
    }
    
    // Fallback: simulazione per testing
    // RIMUOVI IN PRODUZIONE FINALE
    Serial.println("⚠️  MODALITÀ TEST: Genera barcode simulato");
    if(random(0, 100) > 40) {
        result.found = true;
        result.type = "EAN13";
        result.data = "80012345670" + String(random(10, 99));
    }
    
    return result;
}

// ============ CLEANUP ============
void cleanupBarcodeScanner() {
    // Cleanup if needed
}

#endif
