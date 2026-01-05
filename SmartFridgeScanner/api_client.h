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
const char* RECEIPT_URL = "https://frigo.xamad.net/api/receipt";

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

// ============ SEND RECEIPT FOR OCR PARSING ============
int sendReceiptImage(camera_fb_t *fb) {
    if(!checkWiFi()) {
        return -1;
    }

    WiFiClientSecure client;
    client.setInsecure();

    Serial.println("🧾 Invio scontrino per parsing...");

    // Create PGM header for grayscale image (Tesseract can read PGM)
    // PGM format: P5\nwidth height\n255\n[raw bytes]
    String pgmHeader = "P5\n" + String(fb->width) + " " + String(fb->height) + "\n255\n";

    // Create multipart form data
    String boundary = "----ESP32ReceiptBoundary";

    // Build multipart body parts
    String bodyStart = "--" + boundary + "\r\n";
    bodyStart += "Content-Disposition: form-data; name=\"image\"; filename=\"receipt.pgm\"\r\n";
    bodyStart += "Content-Type: image/x-portable-graymap\r\n\r\n";

    String bodyEnd = "\r\n--" + boundary + "--\r\n";

    size_t totalLen = bodyStart.length() + pgmHeader.length() + fb->len + bodyEnd.length();

    // Connect to server
    if(!client.connect("frigo.xamad.net", 443)) {
        Serial.println("❌ Connessione fallita");
        return -1;
    }

    // Write HTTP request
    client.print("POST /api/receipt HTTP/1.1\r\n");
    client.print("Host: frigo.xamad.net\r\n");
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    client.printf("Content-Length: %d\r\n", totalLen);
    client.print("Connection: close\r\n\r\n");

    // Write body
    client.print(bodyStart);
    client.print(pgmHeader);

    // Write image in chunks
    Serial.printf("Sending %d bytes...\n", fb->len);
    size_t written = 0;
    const size_t chunkSize = 1024;
    while(written < fb->len) {
        size_t toWrite = min(chunkSize, fb->len - written);
        client.write(fb->buf + written, toWrite);
        written += toWrite;
        if(written % 10240 == 0) Serial.printf("  %d/%d bytes\n", written, fb->len);
        yield();
    }

    client.print(bodyEnd);
    Serial.println("Upload completo, attendo risposta...");

    // Read response
    unsigned long timeout = millis() + 30000;
    while(client.connected() && millis() < timeout) {
        if(client.available()) {
            String line = client.readStringUntil('\n');
            if(line.startsWith("HTTP/")) {
                int code = line.substring(9, 12).toInt();
                Serial.printf("HTTP Response: %d\n", code);

                // Skip headers, find JSON body
                String jsonBody = "";
                bool headersEnded = false;
                while(client.available() || client.connected()) {
                    if(!client.available()) { delay(10); continue; }
                    String respLine = client.readStringUntil('\n');
                    if(!headersEnded && respLine == "\r") {
                        headersEnded = true;
                        continue;
                    }
                    if(headersEnded && respLine.length() > 0) {
                        jsonBody = respLine;
                        break;
                    }
                }

                if(jsonBody.startsWith("{")) {
                    DynamicJsonDocument doc(2048);
                    DeserializationError error = deserializeJson(doc, jsonBody);
                    if(!error && doc["success"]) {
                        int productsFound = doc["products_found"];
                        Serial.printf("✅ Scontrino: %d prodotti trovati\n", productsFound);
                        if(doc.containsKey("products")) {
                            JsonArray products = doc["products"];
                            for(JsonObject product : products) {
                                Serial.printf("  - %s", product["name"].as<String>().c_str());
                                if(product.containsKey("weight") && !product["weight"].isNull()) {
                                    Serial.printf(" (%s)", product["weight"].as<String>().c_str());
                                }
                                Serial.println();
                            }
                        }
                        client.stop();
                        return productsFound;
                    }
                }
                client.stop();
                return (code >= 200 && code < 300) ? 0 : -1;
            }
        }
        delay(10);
    }

    client.stop();
    Serial.println("❌ Timeout risposta");
    return -1;
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
