#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <WiFiManager.h>
#include "led_feedback.h"

WiFiManager wm;

// ============ WIFI SETUP WITH MANAGER ============
void setupWiFiManager() {
    Serial.println("\n--- WiFi Configuration ---");
    
    // LED blink during setup
    ledBlink(3, 200, 200);
    
    // Set timeout
    wm.setConfigPortalTimeout(180); // 3 minutes
    
    // Custom AP name and password
    wm.setAPCallback([](WiFiManager *myWiFiManager) {
        Serial.println("\n╔═══════════════════════════════╗");
        Serial.println("║   MODALITÀ CONFIGURAZIONE    ║");
        Serial.println("╚═══════════════════════════════╝");
        Serial.println("SSID: FridgeScanner");
        Serial.println("Password: fridge2026");
        Serial.print("IP: ");
        Serial.println(WiFi.softAPIP());
        Serial.println("\nConnettiti per configurare WiFi");
        Serial.println("Vai su: http://192.168.4.1\n");
        
        // Continuous blink in config mode
        while(true) {
            ledBlink(1, 500, 500);
        }
    });
    
    // Custom parameters
    WiFiManagerParameter custom_webhook(
        "webhook", 
        "Webhook URL", 
        "https://tuovps.com/api/fridge", 
        100
    );
    WiFiManagerParameter custom_ocr(
        "ocr", 
        "OCR Endpoint", 
        "https://tuovps.com/api/ocr", 
        100
    );
    
    wm.addParameter(&custom_webhook);
    wm.addParameter(&custom_ocr);
    
    // Disable debug output
    wm.setDebugOutput(false);
    
    // Try to connect, else start AP
    bool connected = wm.autoConnect("FridgeScanner", "fridge2026");
    
    if(!connected) {
        Serial.println("❌ Connessione WiFi fallita!");
        ledError();
        speakerError();
        delay(3000);
        ESP.restart();
    }
    
    Serial.println("✅ WiFi connesso!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    
    ledSuccess();
    speakerSuccess();
    
    delay(1000);
}

// ============ CHECK WIFI CONNECTION ============
bool checkWiFi() {
    if(WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️  WiFi disconnesso, riconnessione...");
        WiFi.reconnect();
        
        int attempts = 0;
        while(WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if(WiFi.status() == WL_CONNECTED) {
            Serial.println("\n✅ Riconnesso!");
            return true;
        } else {
            Serial.println("\n❌ Riconnessione fallita");
            return false;
        }
    }
    return true;
}

#endif
