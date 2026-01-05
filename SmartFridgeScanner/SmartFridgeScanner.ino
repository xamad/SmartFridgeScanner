/*
 * Smart Fridge Barcode Scanner v2.1
 * Server: https://frigo.xamad.net
 */

#if CONFIG_IDF_TARGET_ESP32S3
    #define BOARD_ESP32S3
    const char* BOARD_NAME = "ESP32-S3";
    bool useLocalOCR = true;
#elif CONFIG_IDF_TARGET_ESP32
    #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
        #define BOARD_WROVER
        const char* BOARD_NAME = "ESP32-WROVER";
        bool useLocalOCR = false;
    #else
        #define BOARD_ESP32CAM
        const char* BOARD_NAME = "ESP32-CAM";
        bool useLocalOCR = false;
    #endif
#else
    #error "Board non supportata!"
#endif

#include "config.h"
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
void enterDeepSleep();

void IRAM_ATTR pirISR() { pirTriggered = true; lastActivity = millis(); }

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
    pinMode(PIR_PIN, INPUT);
    pinMode(BOOT_BTN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirISR, RISING);
    if(wasInOutMode && wr == ESP_SLEEP_WAKEUP_EXT0) { modeAdd = false; modeChangeTime = millis(); }
    else { modeAdd = true; wasInOutMode = false; }
    showMode();
    if(!initCamera()) { Serial.println("Camera FAIL!"); ledError(); speakerError(); delay(3000); ESP.restart(); }
    Serial.println("Camera OK");
    initBarcodeScanner();
    Serial.println("Scanner OK");
    setupWiFiManager();
    speakerBeep(2000,100); delay(100); speakerBeep(2500,100);
    Serial.println("\nPronto! Server: frigo.xamad.net");
    Serial.println("BOOT breve=scan, BOOT lungo=toggle\n");
}

void loop() {
    unsigned long now = millis();
    bool btn = (digitalRead(BOOT_BTN) == LOW);
    if(btn && !buttonPressed) { buttonPressed = true; buttonPressStart = now; lastActivity = now; }
    else if(!btn && buttonPressed) {
        unsigned long dur = now - buttonPressStart;
        buttonPressed = false;
        if(dur >= LONG_PRESS_TIME) { toggleMode(); modeChangeTime = now; }
        else if(dur >= DEBOUNCE_TIME) { Serial.println("\n>>> SCAN <<<"); speakerBeep(1500,50); handleScan(); lastScanTime = now; }
        delay(100);
    }
    if(pirTriggered || (now - lastPIRCheck > PIR_CHECK_INTERVAL && digitalRead(PIR_PIN) == HIGH)) {
        pirTriggered = false; lastPIRCheck = now; lastActivity = now;
        if(now - lastScanTime > SCAN_COOLDOWN) { Serial.println("\n>>> PIR <<<"); speakerBeep(1500,50); handleScan(); lastScanTime = now; }
    }
    if(!modeAdd && modeChangeTime > 0 && (now - modeChangeTime > MODE_TIMEOUT)) {
        Serial.println("Timeout->IN"); modeAdd = true; wasInOutMode = false; modeChangeTime = 0; showMode();
    }
    if(now - lastActivity > DEEP_SLEEP_TIMEOUT) enterDeepSleep();
    delay(50);
}

void printBanner() {
    Serial.println("\n=== SMART FRIDGE v2.1 ===");
    Serial.printf("Board: %s, Boot #%d\n", BOARD_NAME, bootCount);
    Serial.printf("OCR: %s, PIR: GPIO%d\n", useLocalOCR ? "Local" : "Remote", PIR_PIN);
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

void enterDeepSleep() {
    Serial.println("\n--- SLEEP ---");
    wasInOutMode = !modeAdd;
    ledOff(); flashOff();
    WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
    #if defined(BOARD_WROVER) || defined(BOARD_ESP32S3)
        esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_PIN, 1);
    #else
        esp_sleep_enable_timer_wakeup(60000000ULL);
    #endif
    Serial.println("Zzz..."); Serial.flush(); delay(100);
    esp_deep_sleep_start();
}
