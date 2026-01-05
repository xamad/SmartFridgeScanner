#ifndef CONFIG_H
#define CONFIG_H

// ============ BOARD SELECTION ============
// Uncomment ONE of these if auto-detection fails:
// - ESP32-CAM AI-Thinker (with or without PSRAM): use FORCE_BOARD_ESP32CAM
// - ESP32-WROVER-CAM: use FORCE_BOARD_WROVER
// - ESP32-S3-CAM: auto-detected, no need to force

#define FORCE_BOARD_ESP32CAM  // <-- Force ESP32-CAM mode (PIR on GPIO13)
// #define FORCE_BOARD_WROVER  // <-- Force WROVER mode (PIR on GPIO34)

// ============ PIR SENSOR ============
// Comment out to DISABLE PIR sensor (use only manual BOOT button scans)
// #define ENABLE_PIR

// ============ DEEP SLEEP ============
// Comment out to DISABLE deep sleep (device stays always on)
// #define ENABLE_DEEP_SLEEP

// ============ SERVER CONFIGURATION ============
// VPS Server: frigo.xamad.net
#define SERVER_HOST "frigo.xamad.net"
#define WEBHOOK_ENDPOINT "/api/product"
#define OCR_ENDPOINT "/api/ocr"

// ============ TIMING CONFIGURATION ============
#define PIR_CHECK_INTERVAL_MS   2000    // Check PIR every 2 sec
#define MODE_TIMEOUT_MS         30000   // Auto-return to IN mode after 30 sec
#define SCAN_COOLDOWN_MS        3000    // Min 3 sec between scans
#define LONG_PRESS_MS           1000    // 1 sec for mode toggle
#define DEBOUNCE_MS             50      // Button debounce
#define DEEP_SLEEP_TIMEOUT_MS   300000  // 5 min inactivity -> sleep

// ============ WIFI AP CONFIGURATION ============
#define WIFI_AP_SSID "FridgeScanner"
#define WIFI_AP_PASS "fridge2026"
#define WIFI_CONFIG_TIMEOUT 180  // seconds

// ============ DEBUG ============
#define DEBUG_SERIAL true

#endif
