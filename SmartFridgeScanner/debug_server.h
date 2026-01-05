#ifndef DEBUG_SERVER_H
#define DEBUG_SERVER_H

#include <WebServer.h>
#include "esp_camera.h"

WebServer debugServer(80);

// HTML page with live preview
const char DEBUG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>FridgeScanner Debug</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; text-align: center; background: #1a1a1a; color: #fff; margin: 20px; }
        h1 { color: #4CAF50; }
        img { max-width: 100%; border: 2px solid #4CAF50; margin: 10px 0; }
        .btn { background: #4CAF50; color: white; padding: 15px 30px; border: none;
               border-radius: 5px; font-size: 18px; cursor: pointer; margin: 5px; }
        .btn:hover { background: #45a049; }
        .btn-red { background: #f44336; }
        .info { background: #333; padding: 15px; border-radius: 5px; margin: 10px 0; text-align: left; }
        .refresh { font-size: 12px; color: #888; }
        #status { color: #4CAF50; }
    </style>
</head>
<body>
    <h1>FridgeScanner Debug</h1>

    <div>
        <button class="btn" onclick="capture()">Cattura Foto</button>
        <button class="btn" onclick="toggleAuto()">Auto Refresh: <span id="autoStatus">OFF</span></button>
        <button class="btn btn-red" onclick="testScan()">Test Scan</button>
    </div>

    <div>
        <img id="preview" src="/capture" alt="Camera preview">
        <p class="refresh">Ultimo aggiornamento: <span id="timestamp">-</span></p>
    </div>

    <div class="info">
        <strong>Status:</strong> <span id="status">Loading...</span><br>
        <strong>IP:</strong> <span id="ip">-</span><br>
        <strong>RSSI:</strong> <span id="rssi">-</span> dBm<br>
        <strong>Resolution:</strong> <span id="resolution">-</span><br>
        <strong>Free Heap:</strong> <span id="heap">-</span> bytes
    </div>

    <div class="info">
        <strong>Tips per scansione:</strong><br>
        - Distanza: 10-15 cm dalla camera<br>
        - Barcode orizzontale (parallelo al bordo lungo)<br>
        - Buona illuminazione (flash attivo durante scan)<br>
        - Per lattine: ruota per avere parte piatta verso camera
    </div>

    <script>
        let autoRefresh = false;
        let autoInterval = null;

        function capture() {
            document.getElementById('preview').src = '/capture?' + Date.now();
            document.getElementById('timestamp').textContent = new Date().toLocaleTimeString();
        }

        function toggleAuto() {
            autoRefresh = !autoRefresh;
            document.getElementById('autoStatus').textContent = autoRefresh ? 'ON' : 'OFF';
            if (autoRefresh) {
                autoInterval = setInterval(capture, 1000);
            } else {
                clearInterval(autoInterval);
            }
        }

        function testScan() {
            fetch('/scan').then(r => r.json()).then(data => {
                if (data.found) {
                    alert('Barcode trovato!\n\nTipo: ' + data.type + '\nDati: ' + data.data);
                } else {
                    alert('Nessun barcode rilevato.\n\nContrasto: ' + data.contrast + '\nBrightness: ' + data.brightness);
                }
                capture();
            });
        }

        function loadStatus() {
            fetch('/status').then(r => r.json()).then(data => {
                document.getElementById('status').textContent = data.status;
                document.getElementById('ip').textContent = data.ip;
                document.getElementById('rssi').textContent = data.rssi;
                document.getElementById('resolution').textContent = data.width + 'x' + data.height;
                document.getElementById('heap').textContent = data.heap;
            });
        }

        capture();
        loadStatus();
        setInterval(loadStatus, 5000);
    </script>
</body>
</html>
)rawliteral";

// Variables for scan result
extern bool modeAdd;

// Handle root - serve HTML page
void handleRoot() {
    debugServer.send(200, "text/html", DEBUG_HTML);
}

// Handle /capture - return camera frame as BMP
void handleCapture() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        debugServer.send(500, "text/plain", "Camera capture failed");
        return;
    }

    // For grayscale, create a simple BMP
    int width = fb->width;
    int height = fb->height;
    int rowSize = (width + 3) & ~3;  // BMP rows must be 4-byte aligned
    int imageSize = rowSize * height;
    int fileSize = 54 + 256*4 + imageSize;  // Header + palette + data

    // Create BMP header
    uint8_t bmpHeader[54] = {
        'B', 'M',                           // Signature
        (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
        0, 0, 0, 0,                         // Reserved
        54 + 256*4, 0, 0, 0,                // Data offset (header + palette)
        40, 0, 0, 0,                        // Info header size
        (uint8_t)(width), (uint8_t)(width >> 8), (uint8_t)(width >> 16), (uint8_t)(width >> 24),
        (uint8_t)(height), (uint8_t)(height >> 8), (uint8_t)(height >> 16), (uint8_t)(height >> 24),
        1, 0,                               // Planes
        8, 0,                               // Bits per pixel (8 = grayscale)
        0, 0, 0, 0,                         // Compression (none)
        (uint8_t)(imageSize), (uint8_t)(imageSize >> 8), (uint8_t)(imageSize >> 16), (uint8_t)(imageSize >> 24),
        0x13, 0x0B, 0, 0,                   // X pixels per meter
        0x13, 0x0B, 0, 0,                   // Y pixels per meter
        0, 1, 0, 0,                         // Colors used (256)
        0, 0, 0, 0                          // Important colors
    };

    // Create grayscale palette (256 entries)
    uint8_t palette[256 * 4];
    for (int i = 0; i < 256; i++) {
        palette[i * 4] = i;      // Blue
        palette[i * 4 + 1] = i;  // Green
        palette[i * 4 + 2] = i;  // Red
        palette[i * 4 + 3] = 0;  // Reserved
    }

    // Send response
    debugServer.setContentLength(fileSize);
    debugServer.send(200, "image/bmp", "");

    // Send header
    debugServer.sendContent((const char*)bmpHeader, 54);

    // Send palette
    debugServer.sendContent((const char*)palette, 256 * 4);

    // Send image data (BMP is bottom-up, so flip vertically)
    uint8_t rowBuffer[1280];  // Max width
    for (int y = height - 1; y >= 0; y--) {
        memcpy(rowBuffer, fb->buf + y * width, width);
        // Pad row to 4-byte boundary
        for (int p = width; p < rowSize; p++) rowBuffer[p] = 0;
        debugServer.sendContent((const char*)rowBuffer, rowSize);
    }

    esp_camera_fb_return(fb);
}

// Handle /status - return JSON status
void handleStatus() {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"status\":\"OK\",\"ip\":\"%s\",\"rssi\":%d,\"width\":%d,\"height\":%d,\"heap\":%d,\"mode\":\"%s\"}",
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(),
        1024, 768,  // Current resolution
        ESP.getFreeHeap(),
        modeAdd ? "IN" : "OUT"
    );
    debugServer.send(200, "application/json", json);
}

// Handle /scan - perform scan and return result
void handleScan() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        debugServer.send(500, "application/json", "{\"error\":\"Camera failed\"}");
        return;
    }

    // Calculate image stats
    uint8_t minVal = 255, maxVal = 0;
    long sum = 0;
    for (int i = 0; i < (int)fb->len; i += 100) {
        if (fb->buf[i] < minVal) minVal = fb->buf[i];
        if (fb->buf[i] > maxVal) maxVal = fb->buf[i];
        sum += fb->buf[i];
    }
    int brightness = sum / (fb->len / 100);
    int contrast = maxVal - minVal;

    // Try to scan
    BarcodeResult result = scanBarcode(fb);
    esp_camera_fb_return(fb);

    char json[256];
    if (result.found) {
        snprintf(json, sizeof(json),
            "{\"found\":true,\"type\":\"%s\",\"data\":\"%s\",\"brightness\":%d,\"contrast\":%d}",
            result.type.c_str(), result.data.c_str(), brightness, contrast);
    } else {
        snprintf(json, sizeof(json),
            "{\"found\":false,\"brightness\":%d,\"contrast\":%d,\"min\":%d,\"max\":%d}",
            brightness, contrast, minVal, maxVal);
    }
    debugServer.send(200, "application/json", json);
}

// Initialize debug server
void initDebugServer() {
    debugServer.on("/", handleRoot);
    debugServer.on("/capture", handleCapture);
    debugServer.on("/status", handleStatus);
    debugServer.on("/scan", handleScan);

    debugServer.begin();
    Serial.println("\n=== DEBUG SERVER ===");
    Serial.printf("http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.println("====================\n");
}

// Handle client requests (call in loop)
void handleDebugServer() {
    debugServer.handleClient();
}

#endif
