/*
 * Smart Fridge Barcode Scanner v2.2
 * Server: https://frigo.xamad.net
 */

#include "config.h"  // Must be first for FORCE_BOARD_* defines

// ============ BOARD DETECTION ============
// Priority: 1) Forced board from config.h, 2) Auto-detect
#if CONFIG_IDF_TARGET_ESP32S3
    #define BOARD_ESP32S3
    const char* BOARD_NAME = "ESP32-S3";
    bool useLocalOCR = true;
#elif CONFIG_IDF_TARGET_ESP32
    #if defined(FORCE_BOARD_ESP32CAM)
        // Forced ESP32-CAM mode (ignores PSRAM detection)
        #define BOARD_ESP32CAM
        const char* BOARD_NAME = "ESP32-CAM";
        bool useLocalOCR = false;
    #elif defined(FORCE_BOARD_WROVER)
        // Forced WROVER mode
        #define BOARD_WROVER
        const char* BOARD_NAME = "ESP32-WROVER";
        bool useLocalOCR = false;
    #elif defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
        // Auto-detect: has PSRAM -> assume WROVER (may fail on ESP32-CAM with PSRAM!)
        #define BOARD_WROVER
        const char* BOARD_NAME = "ESP32-WROVER";
        bool useLocalOCR = false;
    #else
        // Auto-detect: no PSRAM -> assume basic ESP32-CAM
        #define BOARD_ESP32CAM
        const char* BOARD_NAME = "ESP32-CAM";
        bool useLocalOCR = false;
    #endif
#else
    #error "Board non supportata!"
#endif

#include "camera_config.h"
#include "led_feedback.h"
#include "wifi_manager.h"
#include "barcode_scanner.h"
#include "api_client.h"

#if defined(BOARD_WROVER)
    #define PIR_PIN 34
    #define BOOT_BTN 0
#elif defined(BOARD_ESP32S3)
    #define PIR_PIN 1
    #define BOOT_BTN 0
#else
    #define PIR_PIN 13
    #define BOOT_BTN 0
#endif

const unsigned long PIR_CHECK_INTERVAL = 2000;
const unsigned long MODE_TIMEOUT = 30000;
const unsigned long SCAN_COOLDOWN = 3000;
const unsigned long LONG_PRESS_TIME = 1000;
const unsigned long DEBOUNCE_TIME = 50;
const unsigned long DEEP_SLEEP_TIMEOUT = 300000;

RTC_DATA_ATTR bool modeAdd = true;
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR bool wasInOutMode = false;

volatile unsigned long lastActivity = 0;
volatile bool pirTriggered = false;
unsigned long lastPIRCheck = 0;
unsigned long lastScanTime = 0;
unsigned long modeChangeTime = 0;
unsigned long buttonPressStart = 0;
bool buttonPressed = false;

void printBanner();
void showMode();
void toggleMode();
void handleScan();
void handleReceiptScan();
#ifdef ENABLE_DEEP_SLEEP
void enterDeepSleep();
#endif

// Triple-press detection for receipt mode
int pressCount = 0;
unsigned long lastPressTime = 0;
const unsigned long TRIPLE_PRESS_WINDOW = 800; // ms between presses

#ifdef ENABLE_PIR
void IRAM_ATTR pirISR() { pirTriggered = true; lastActivity = millis(); }
#endif

void setup() {
    Serial.begin(115200);
    delay(500);
    bootCount++;
    lastActivity = millis();
    esp_sleep_wakeup_cause_t wr = esp_sleep_get_wakeup_cause();
    printBanner();
    switch(wr) {
        case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Wake: PIR!"); break;
        case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Wake: Timer"); break;
        default: Serial.println("Wake: Power on"); break;
    }
    initLED();
    pinMode(BOOT_BTN, INPUT_PULLUP);

    // Initialize camera FIRST (before PIR to avoid GPIO ISR conflict)
    if(!initCamera()) { Serial.println("Camera FAIL!"); ledError(); speakerError(); delay(3000); ESP.restart(); }
    Serial.println("Camera OK");

    #ifdef ENABLE_PIR
        pinMode(PIR_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING);
        Serial.println("PIR OK");
        if(wasInOutMode && wr == ESP_SLEEP_WAKEUP_EXT0) { modeAdd = false; modeChangeTime = millis(); }
        else { modeAdd = true; wasInOutMode = false; }
    #else
        modeAdd = true; wasInOutMode = false;
        Serial.println("PIR: Disabled");
    #endif

    showMode();
    initBarcodeScanner();
    Serial.println("Scanner OK");
    setupWiFiManager();
    speakerBeep(2000,100); delay(100); speakerBeep(2500,100);
    Serial.println("\nPronto! Server: frigo.xamad.net");
    Serial.println("BOOT 1x=scan, BOOT 3x=scontrino, BOOT lungo=toggle\n");
}

void loop() {
    unsigned long now = millis();
    static bool waitingForMorePresses = false;
    static unsigned long pressWindowStart = 0;

    // Button handling with triple-press detection for receipt mode
    bool btn = (digitalRead(BOOT_BTN) == LOW);

    if(btn && !buttonPressed) {
        buttonPressed = true;
        buttonPressStart = now;
        lastActivity = now;
    }
    else if(!btn && buttonPressed) {
        unsigned long dur = now - buttonPressStart;
        buttonPressed = false;

        if(dur >= LONG_PRESS_TIME) {
            // Long press: toggle mode
            toggleMode();
            modeChangeTime = now;
            pressCount = 0;
            waitingForMorePresses = false;
        }
        else if(dur >= DEBOUNCE_TIME) {
            // Short press: count it
            pressCount++;
            lastPressTime = now;

            if(pressCount == 1) {
                // First press - start the window
                pressWindowStart = now;
                waitingForMorePresses = true;
            }

            if(pressCount >= 3) {
                // Triple press: Receipt scan
                Serial.println("\n>>> SCONTRINO <<<");
                speakerBeep(1000,50); delay(50); speakerBeep(1500,50); delay(50); speakerBeep(2000,50);
                handleReceiptScan();
                lastScanTime = now;
                pressCount = 0;
                waitingForMorePresses = false;
            }
        }
        delay(50);  // Small debounce
    }

    // Non-blocking: check if press window expired - do normal scan
    if(waitingForMorePresses && pressCount > 0 && pressCount < 3) {
        if(now - pressWindowStart > TRIPLE_PRESS_WINDOW) {
            Serial.println("\n>>> SCAN <<<");
            speakerBeep(1500,50);
            handleScan();
            lastScanTime = now;
            pressCount = 0;
            waitingForMorePresses = false;
        }
    }

    // PIR motion detection (only if enabled)
    #ifdef ENABLE_PIR
    if(pirTriggered || (now - lastPIRCheck > PIR_CHECK_INTERVAL && digitalRead(PIR_PIN) == HIGH)) {
        pirTriggered = false; lastPIRCheck = now; lastActivity = now;
        if(now - lastScanTime > SCAN_COOLDOWN) { Serial.println("\n>>> PIR <<<"); speakerBeep(1500,50); handleScan(); lastScanTime = now; }
    }
    #endif

    // Mode timeout (auto-return to IN mode)
    if(!modeAdd && modeChangeTime > 0 && (now - modeChangeTime > MODE_TIMEOUT)) {
        Serial.println("Timeout->IN"); modeAdd = true; wasInOutMode = false; modeChangeTime = 0; showMode();
    }

    // Deep sleep (only if enabled)
    #ifdef ENABLE_DEEP_SLEEP
    if(now - lastActivity > DEEP_SLEEP_TIMEOUT) enterDeepSleep();
    #endif

    delay(50);
}

void printBanner() {
    Serial.println("\n=== SMART FRIDGE v2.3 ===");
    #if defined(FORCE_BOARD_ESP32CAM) || defined(FORCE_BOARD_WROVER)
        Serial.printf("Board: %s (forced), Boot #%d\n", BOARD_NAME, bootCount);
    #else
        Serial.printf("Board: %s, Boot #%d\n", BOARD_NAME, bootCount);
    #endif
    Serial.printf("OCR: %s\n", useLocalOCR ? "Local" : "Remote");
    #ifdef ENABLE_PIR
        Serial.printf("PIR: GPIO%d\n", PIR_PIN);
    #else
        Serial.println("PIR: Disabled");
    #endif
    #ifdef ENABLE_DEEP_SLEEP
        Serial.println("Sleep: Enabled (5min)");
    #else
        Serial.println("Sleep: Disabled");
    #endif
    Serial.println("Server: frigo.xamad.net\n");
}

void showMode() {
    if(modeAdd) { ledModeIn(); Serial.println("[IN] INGRESSO"); }
    else { ledModeOut(); Serial.println("[OUT] USCITA"); }
}

void toggleMode() {
    modeAdd = !modeAdd; wasInOutMode = !modeAdd;
    Serial.printf("Modo: %s\n", modeAdd ? "IN" : "OUT");
    for(int i=0; i<3; i++) {
        if(modeAdd) { ledModeIn(); speakerBeep(1000+i*200, 100); }
        else { ledModeOut(); speakerBeep(1400-i*200, 100); }
        delay(150);
    }
    showMode();
}

void handleScan() {
    Serial.println("\n==== SCAN ====");
    lastActivity = millis();
    ledProcessing(); speakerBeep(1800,50);
    flashOn(); delay(300);
    camera_fb_t *fb = esp_camera_fb_get();
    if(!fb) { Serial.println("Frame fail!"); flashOff(); ledError(); speakerError(); showMode(); return; }
    Serial.printf("Frame: %dx%d\n", fb->width, fb->height);
    flashOff();
    BarcodeResult result = scanBarcode(fb);
    if(result.found) {
        Serial.printf("%s: %s\n", result.type.c_str(), result.data.c_str());
        speakerBeep(2500,100);
        String expiry = "";
        #if defined(BOARD_ESP32S3)
            ledBlink(5,50,50); expiry = performLocalOCR(fb);
        #else
            ledBlink(3,100,100); expiry = performRemoteOCR(fb);
        #endif
        if(expiry.length() > 0) Serial.printf("Scadenza: %s\n", expiry.c_str());
        Serial.println("Invio...");
        bool ok = sendProductWebhook(result.data, expiry, result.type);
        esp_camera_fb_return(fb);
        if(ok) { Serial.println("OK!"); ledSuccess(); speakerSuccess(); }
        else { Serial.println("FAIL"); ledError(); speakerError(); }
    } else {
        Serial.println("No barcode");
        esp_camera_fb_return(fb);
        ledError(); speakerError();
    }
    delay(1500); showMode();
    Serial.println("=============\n");
}

void handleReceiptScan() {
    Serial.println("\n==== SCONTRINO ====");
    lastActivity = millis();

    // Special LED pattern for receipt mode
    for(int i=0; i<3; i++) {
        ledcWrite(FLASH_LED, 128);
        delay(100);
        ledcWrite(FLASH_LED, 0);
        delay(100);
    }

    // Switch camera to JPEG for receipt (OCR needs proper image format)
    sensor_t *s = esp_camera_sensor_get();
    s->set_pixformat(s, PIXFORMAT_JPEG);
    s->set_framesize(s, FRAMESIZE_XGA);  // 1024x768 for text clarity
    s->set_quality(s, 10);  // High quality JPEG
    delay(200);  // Let camera adjust

    flashOn();
    delay(500); // Longer delay for receipt positioning

    camera_fb_t *fb = esp_camera_fb_get();
    if(!fb) {
        Serial.println("Frame fail!");
        flashOff();
        // Restore grayscale for barcode scanning
        s->set_pixformat(s, PIXFORMAT_GRAYSCALE);
        ledError();
        speakerError();
        showMode();
        return;
    }

    Serial.printf("Frame: %dx%d, %d bytes (JPEG)\n", fb->width, fb->height, fb->len);
    flashOff();

    Serial.println("Invio scontrino al server...");
    ledBlink(5, 100, 100);

    int productsFound = sendReceiptImage(fb);
    esp_camera_fb_return(fb);

    // Restore grayscale for barcode scanning
    s->set_pixformat(s, PIXFORMAT_GRAYSCALE);
    delay(100);

    if(productsFound > 0) {
        Serial.printf("Aggiunti %d prodotti!\n", productsFound);
        // Success feedback - multiple beeps for number of products
        for(int i=0; i<min(productsFound, 5); i++) {
            speakerBeep(2000, 100);
            ledSuccess();
            delay(200);
        }
    } else if(productsFound == 0) {
        Serial.println("Nessun prodotto riconosciuto");
        ledError();
        speakerError();
    } else {
        Serial.println("Errore invio scontrino");
        ledError();
        speakerError();
    }

    delay(1500);
    showMode();
    Serial.println("==================\n");
}

#ifdef ENABLE_DEEP_SLEEP
void enterDeepSleep() {
    Serial.println("\n--- SLEEP ---");
    wasInOutMode = !modeAdd;
    ledOff(); flashOff();
    WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
    #if defined(ENABLE_PIR) && (defined(BOARD_WROVER) || defined(BOARD_ESP32S3))
        esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_PIN, 1);
    #else
        esp_sleep_enable_timer_wakeup(60000000ULL);  // 60 sec timer wake
    #endif
    Serial.println("Zzz..."); Serial.flush(); delay(100);
    esp_deep_sleep_start();
}
#endif
