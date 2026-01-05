#ifndef BARCODE_SCANNER_H
#define BARCODE_SCANNER_H

#include "esp_camera.h"
#include <ESP32QRCodeReader.h>

// Inizializza con framesize invece di grab mode
ESP32QRCodeReader reader(FRAMESIZE_VGA);

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
        
        // Informazioni QR Code (se disponibili)
        Serial.printf("   Lunghezza payload: %d bytes\n", strlen((const char*)qrCodeData.payload));
        
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
            Serial.println("   ⚠️  Immagine troppo scura! Aumenta illuminazione");
        } else if(brightness > 230) {
            Serial.println("   ⚠️  Immagine sovraesposta! Riduci illuminazione");
        } else {
            Serial.println("   ✓ Luminosità OK");
        }
    }
    
    // Fallback: simulazione per testing
    // RIMUOVI IN PRODUZIONE FINALE o integra libreria barcode 1D
    Serial.println("\n⚠️  MODALITÀ TEST ATTIVA ⚠️");
    Serial.println("   Nessun QR rilevato, genera barcode simulato");
    Serial.println("   Per produzione: integra ZXing/ZBar per barcode 1D");
    
    if(random(0, 100) > 30) {
        result.found = true;
        result.type = "EAN13";
        result.data = "80012345670" + String(random(10, 99));
        Serial.printf("   TEST: Barcode simulato: %s\n", result.data.c_str());
    }
    
    return result;
}

// ============ CLEANUP ============
void cleanupBarcodeScanner() {
    // Cleanup if needed
}

#endif
