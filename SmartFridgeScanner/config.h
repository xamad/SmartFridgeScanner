#ifndef CONFIG_H
#define CONFIG_H

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
