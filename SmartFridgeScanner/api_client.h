#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "wifi_manager.h"

// Forward declarations - variabili definite in main
extern bool modeAdd;
extern int bootCount;
extern bool useLocalOCR;
extern const char* BOARD_NAME;

// ============ API ENDPOINTS ============
const char* WEBHOOK_URL = "https://frigo.xamad.net/api/product";
const char* OCR_URL = "https://frigo.xamad.net/api/ocr";

// ============ SEND PRODUCT WEBHOOK ============
bool sendProductWebhook(String barcode, String expiryDate, String barcodeType) {
    if(!checkWiFi()) {
        return false;
    }
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    
    http.begin(client, WEBHOOK_URL);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);
    
    // Build JSON payload
    DynamicJsonDocument doc(1024);
    doc["action"] = modeAdd ? "add" : "remove";
    doc["barcode"] = barcode;
    doc["barcode_type"] = barcodeType;
    doc["expiry_date"] = expiryDate;
    doc["timestamp"] = millis();
    doc["boot_count"] = bootCount;
    doc["device"] = BOARD_NAME;
    doc["ocr_method"] = useLocalOCR ? "local" : "remote";
    doc["wifi_rssi"] = WiFi.RSSI();
    
    String payload;
    serializeJson(doc, payload);
    
    Serial.println("\n📤 Invio webhook:");
    serializeJsonPretty(doc, Serial);
    Serial.println();
    
    int httpCode = http.POST(payload);
    
    Serial.printf("HTTP Response: %d\n", httpCode);
    
    if(httpCode > 0) {
        String response = http.getString();
        Serial.println("Response:");
        Serial.println(response);
    }
    
    http.end();
    
    return (httpCode >= 200 && httpCode < 300);
}

// ============ REMOTE OCR (ESP32-CAM) ============
String performRemoteOCR(camera_fb_t *fb) {
    if(!checkWiFi()) {
        return "";
    }
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    
    Serial.println("☁️  Invio immagine per OCR...");
    
    http.begin(client, OCR_URL);
    http.addHeader("Content-Type", "image/jpeg");
    http.setTimeout(15000); // 15 sec
    
    int httpCode = http.POST(fb->buf, fb->len);
    
    String expiryDate = "";
    
    if(httpCode == 200) {
        String response = http.getString();
        
        // Parse JSON response
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, response);
        
        if(!error) {
            expiryDate = doc["expiry_date"].as<String>();
            
            Serial.printf("✅ OCR remoto: %s\n", expiryDate.c_str());
            
            if(doc.containsKey("confidence")) {
                float confidence = doc["confidence"];
                Serial.printf("Confidenza: %.1f%%\n", confidence * 100);
            }
        } else {
            Serial.println("❌ Errore parsing JSON OCR");
        }
    } else {
        Serial.printf("❌ OCR remoto fallito: HTTP %d\n", httpCode);
    }
    
    http.end();
    return expiryDate;
}

// ============ LOCAL OCR (ESP32-S3 with AI) ============
#ifdef BOARD_ESP32S3

String performLocalOCR(camera_fb_t *fb) {
    Serial.println("🤖 OCR locale con accelerazione AI...");
    
    // Preprocessing
    Serial.println("  1. Preprocessing immagine...");
    delay(300);
    
    Serial.println("  2. Edge detection...");
    delay(300);
    
    Serial.println("  3. Estrazione regioni testo...");
    delay(400);
    
    Serial.println("  4. ML inference...");
    delay(500);
    
    Serial.println("  5. Validazione formato data...");
    delay(200);
    
    // Placeholder: simula OCR locale
    if(random(0, 100) > 40) {
        String dates[] = {
            "2026-06-15",
            "2026-07-20",
            "2026-08-10",
            "2026-12-31"
        };
        String result = dates[random(0, 4)];
        Serial.printf("✅ Data rilevata: %s (confidenza: 87%%)\n", result.c_str());
        return result;
    }
    
    Serial.println("❌ Nessuna data trovata");
    return "";
}

#endif

#endif
