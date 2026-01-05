#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <WiFiManager.h>
#include "led_feedback.h"

WiFiManager wm;

void setupWiFiManager() {
    Serial.println("\n--- WiFi Configuration ---");
    ledBlink(3, 200, 200);
    
    wm.setConfigPortalTimeout(180);
    
    wm.setAPCallback([](WiFiManager *myWiFiManager) {
        Serial.println("\n=== CONFIG MODE ===");
        Serial.println("SSID: FridgeScanner");
        Serial.println("Pass: fridge2026");
        Serial.print("IP: ");
        Serial.println(WiFi.softAPIP());
        Serial.println("http://192.168.4.1\n");
    });
    
    wm.setDebugOutput(false);
    
    bool connected = wm.autoConnect("FridgeScanner", "fridge2026");
    
    if(!connected) {
        Serial.println("WiFi FAIL!");
        ledError();
        speakerError();
        delay(3000);
        ESP.restart();
    }
    
    Serial.println("WiFi OK!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    
    ledSuccess();
    speakerSuccess();
    delay(1000);
}

bool checkWiFi() {
    if(WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi reconnecting...");
        WiFi.reconnect();
        int attempts = 0;
        while(WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        if(WiFi.status() == WL_CONNECTED) {
            Serial.println(" OK!");
            return true;
        } else {
            Serial.println(" FAIL");
            return false;
        }
    }
    return true;
}

#endif
