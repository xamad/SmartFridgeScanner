/*
 * =====================================================
 *  Smart Fridge Barcode Scanner - Main
 *  Auto-detect: ESP32-CAM vs ESP32-S3
 *  Features: Barcode, QR, OCR, WiFi Manager, Speaker
 *  Version: 2.0
 * =====================================================
 */

// ============ BOARD AUTO-DETECTION ============
#if CONFIG_IDF_TARGET_ESP32S3
    #define BOARD_ESP32S3
    const char* BOARD_NAME = "ESP32-S3";
    bool useLocalOCR = true;
#elif CONFIG_IDF_TARGET_ESP32
    #define BOARD_ESP32CAM
    const char* BOARD_NAME = "ESP32-CAM";
    bool useLocalOCR = false;
#else
    #error "Board non supportata!"
#endif

// Include headers in correct order
#include "camera_config.h"
#include "led_feedback.h"
#include "wifi_manager.h"
#include "barcode_scanner.h"
#include "api_client.h"

// ============ PIN DEFINITIONS ============
#define PIR_PIN 33
#define BOOT_BTN 0

// ============ CONFIGURATION ============
const int pirCheckInterval = 2000;     // ms
const int modeTimeout = 30000;         // 30 sec auto-return to IN mode
const int scanCooldown = 3000;         // 3 sec between scans

// ============ STATE VARIABLES ============
RTC_DATA_ATTR bool modeAdd = true;     // true=INGRESSO, false=USCITA
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR unsigned long lastModeChange = 0;

unsigned long lastPIRCheck = 0;
unsigned long lastScanTime = 0;

// ============ FUNCTION DECLARATIONS ============
void printBanner();
void showMode();
void toggleMode();
void handleScan();

// ============ SETUP ============
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    bootCount++;
    
    // Print banner
    printBanner();
    
    // Initialize LED feedback system
    initLED();
    
    // Setup GPIO
    pinMode(PIR_PIN, INPUT);
    pinMode(BOOT_BTN, INPUT_PULLUP);
    
    // Show current mode
    showMode();
    
    // Initialize camera
    if(!initCamera()) {
        Serial.println("❌ ERRORE: Camera init fallita!");
        ledError();
        speakerError();
        ESP.restart();
    }
    Serial.println("✓ Camera inizializzata");
    
    // Initialize barcode scanner
    initBarcodeScanner();
    Serial.println("✓ Barcode scanner pronto");
    
    // WiFi setup with manager
    setupWiFiManager();
    
    // Ready beeps
    speakerBeep(2000, 100);
    delay(100);
    speakerBeep(2500, 100);
    
    Serial.println("\n🚀 Sistema pronto!\n");
    Serial.println("Comandi:");
    Serial.println("  - Premi BOOT per cambiare modalità IN/OUT");
    Serial.println("  - Avvicina prodotto per scansione automatica\n");
}

// ============ MAIN LOOP ============
void loop() {
    // Check BOOT button for mode toggle
    if(digitalRead(BOOT_BTN) == LOW) {
        delay(50); // debounce
        if(digitalRead(BOOT_BTN) == LOW) {
            toggleMode();
            // Wait for button release
            while(digitalRead(BOOT_BTN) == LOW) {
                delay(10);
            }
            delay(500); // Additional delay after release
        }
    }
    
    // Check PIR sensor periodically
    if(millis() - lastPIRCheck > pirCheckInterval) {
        lastPIRCheck = millis();
        
        if(digitalRead(PIR_PIN) == HIGH) {
            // Cooldown check to avoid multiple scans
            if(millis() - lastScanTime > scanCooldown) {
                Serial.println("\n>>> 👋 PRESENZA RILEVATA <<<");
                speakerBeep(1500, 50);
                handleScan();
                lastScanTime = millis();
            } else {
                Serial.print(".");
            }
        }
    }
    
    // Auto-return to IN mode after timeout in OUT mode
    if(!modeAdd && (millis() - lastModeChange > modeTimeout)) {
        Serial.println("⏱️  Timeout: ritorno a modalità INGRESSO");
        modeAdd = true;
        lastModeChange = millis();
        showMode();
    }
    
    delay(100);
}

// ============ PRINT BANNER ============
void printBanner() {
    Serial.println("\n\n╔════════════════════════════════════════╗");
    Serial.println("║   SMART FRIDGE BARCODE SCANNER v2.0   ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.printf("Board: %s\n", BOARD_NAME);
    Serial.printf("Boot #%d\n", bootCount);
    Serial.printf("OCR: %s\n", useLocalOCR ? "Locale (AI)" : "Remoto (VPS)");
    
    #if defined(BOARD_ESP32S3)
        Serial.println("Features: WiFi, Camera, AI OCR, Barcode/QR");
    #else
        Serial.println("Features: WiFi, Camera, Remote OCR, Barcode/QR");
    #endif
    
    Serial.println("========================================\n");
}

// ============ SHOW CURRENT MODE ============
void showMode() {
    if(modeAdd) {
        ledGreen();
        Serial.println("📥 Modalità: INGRESSO (aggiungi prodotti)");
    } else {
        ledRed();
        Serial.println("📤 Modalità: USCITA (rimuovi prodotti)");
    }
}

// ============ TOGGLE MODE ============
void toggleMode() {
    modeAdd = !modeAdd;
    lastModeChange = millis();
    
    Serial.printf("\n🔄 Modalità cambiata: %s\n", modeAdd ? "INGRESSO" : "USCITA");
    
    // Visual + audio feedback
    if(modeAdd) {
        // Green mode - single tone
        for(int i=0; i<3; i++) {
            ledGreen();
            speakerBeep(1000, 100);
            delay(200);
            ledOff();
            delay(100);
        }
        ledGreen();
    } else {
        // Red mode - double tone
        for(int i=0; i<3; i++) {
            ledRed();
            speakerBeep(1500, 80);
            delay(80);
            speakerBeep(2000, 80);
            delay(200);
            ledOff();
            delay(100);
        }
        ledRed();
    }
}

// ============ HANDLE SCAN ============
void handleScan() {
    Serial.println("\n╔════ INIZIO SCANSIONE ════╗");
    Serial.printf("Modalità: %s\n", modeAdd ? "INGRESSO" : "USCITA");
    
    // Processing LED
    ledProcessing();
    speakerBeep(1800, 50);
    
    // Flash on for illumination
    flashOn();
    delay(500); // Let camera adjust exposure
    
    // Capture frame
    camera_fb_t *fb = esp_camera_fb_get();
    if(!fb) {
        Serial.println("❌ Cattura frame fallita!");
        flashOff();
        ledError();
        speakerError();
        showMode();
        Serial.println("╚══════════════════════════╝\n");
        return;
    }
    
    Serial.printf("📸 Frame: %dx%d, %d KB\n", fb->width, fb->height, fb->len/1024);
    
    flashOff();
    
    // Decode barcode/QR
    BarcodeResult result = scanBarcode(fb);
    
    if(result.found) {
        Serial.printf("✅ %s rilevato: %s\n", result.type.c_str(), result.data.c_str());
        speakerBeep(2500, 100);
        
        // OCR for expiry date
        String expiryDate = "";
        
        #ifdef BOARD_ESP32S3
            Serial.println("🤖 OCR locale (AI)...");
            ledBlink(5, 50, 50); // Fast blink during AI processing
            expiryDate = performLocalOCR(fb);
        #else
            Serial.println("☁️  OCR remoto (VPS)...");
            ledBlink(3, 100, 100);
            expiryDate = performRemoteOCR(fb);
        #endif
        
        if(expiryDate.length() > 0) {
            Serial.printf("📅 Scadenza rilevata: %s\n", expiryDate.c_str());
        } else {
            Serial.println("⚠️  Scadenza non rilevata");
            Serial.println("   (sarà richiesto input manuale su app)");
        }
        
        // Send webhook
        Serial.println("📡 Invio dati al server...");
        bool success = sendProductWebhook(result.data, expiryDate, result.type);
        
        esp_camera_fb_return(fb);
        
        if(success) {
            Serial.println("✅ Prodotto registrato con successo!");
            ledSuccess();
            speakerSuccess();
        } else {
            Serial.println("❌ Errore invio dati al server");
            ledError();
            speakerError();
        }
        
    } else {
        Serial.println("❌ Nessun barcode/QR trovato");
        Serial.println("   Suggerimenti:");
        Serial.println("   - Assicurati che il barcode sia ben illuminato");
        Serial.println("   - Mantieni distanza 10-20cm");
        Serial.println("   - Prova ad angolare il prodotto");
        
        esp_camera_fb_return(fb);
        ledError();
        speakerError();
    }
    
    delay(2000);
    showMode(); // Restore mode LED
    
    Serial.println("╚══════════════════════════╝\n");
}
