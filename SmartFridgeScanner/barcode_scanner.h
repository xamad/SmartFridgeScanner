#ifndef BARCODE_SCANNER_H
#define BARCODE_SCANNER_H

#include "esp_camera.h"
#include "quirc.h"

// Barcode result structure
struct BarcodeResult {
    bool found;
    String type;
    String data;
};

// Quirc instance for QR decoding
struct quirc *qr = NULL;

// ============ EAN/UPC BARCODE PATTERNS ============
// L-codes (left side, odd parity) - used in EAN-13, EAN-8, UPC-A
const uint8_t EAN_L[] = {
    0b0001101, // 0
    0b0011001, // 1
    0b0010011, // 2
    0b0111101, // 3
    0b0100011, // 4
    0b0110001, // 5
    0b0101111, // 6
    0b0111011, // 7
    0b0110111, // 8
    0b0001011  // 9
};

// G-codes (left side, even parity) - used in EAN-13
const uint8_t EAN_G[] = {
    0b0100111, // 0
    0b0110011, // 1
    0b0011011, // 2
    0b0100001, // 3
    0b0011101, // 4
    0b0111001, // 5
    0b0000101, // 6
    0b0010001, // 7
    0b0001001, // 8
    0b0010111  // 9
};

// R-codes (right side) - used in all EAN/UPC
const uint8_t EAN_R[] = {
    0b1110010, // 0
    0b1100110, // 1
    0b1101100, // 2
    0b1000010, // 3
    0b1011100, // 4
    0b1001110, // 5
    0b1010000, // 6
    0b1000100, // 7
    0b1001000, // 8
    0b1110100  // 9
};

// First digit encoding (parity pattern for EAN-13 digits 2-7)
const uint8_t EAN_FIRST[] = {
    0b000000, // 0: LLLLLL
    0b001011, // 1: LLGLGG
    0b001101, // 2: LLGGLG
    0b001110, // 3: LLGGGL
    0b010011, // 4: LGLLGG
    0b011001, // 5: LGGLLG
    0b011100, // 6: LGGGLL
    0b010101, // 7: LGLGLG
    0b010110, // 8: LGLGGL
    0b011010  // 9: LGGLGL
};

// Code 128 patterns (START, digits, STOP)
const uint16_t CODE128_PATTERNS[] = {
    0b11011001100, // 0 (space in B)
    0b11001101100, // 1
    0b11001100110, // 2
    0b10010011000, // 3
    0b10010001100, // 4
    0b10001001100, // 5
    0b10011001000, // 6
    0b10011000100, // 7
    0b10001100100, // 8
    0b11001001000, // 9
    // ... more patterns for full Code128
};

// ============ INITIALIZE BARCODE SCANNER ============
void initBarcodeScanner() {
    qr = quirc_new();
    if (qr == NULL) {
        Serial.println("[SCAN] Failed to allocate quirc");
        return;
    }
    if (quirc_resize(qr, 640, 480) < 0) {
        Serial.println("[SCAN] Failed to resize quirc");
        quirc_destroy(qr);
        qr = NULL;
        return;
    }
    Serial.println("[SCAN] Scanner ready: QR, EAN-13, EAN-8, UPC-A");
}

// ============ FIND BARCODE START GUARD ============
int findStartGuard(uint8_t *line, int width, int threshold, int *moduleWidth) {
    // Look for start pattern: bar-space-bar (101)
    for (int i = 20; i < width - 200; i++) {
        // Find white->black transition
        if (line[i] > threshold && line[i+1] <= threshold) {
            int bar1 = 0, space = 0, bar2 = 0;
            int j = i + 1;

            // Measure first black bar
            while (j < width && line[j] <= threshold) { bar1++; j++; }
            if (bar1 < 2 || bar1 > 20) continue;

            // Measure white space
            while (j < width && line[j] > threshold) { space++; j++; }
            if (space < 1 || abs(space - bar1) > bar1) continue;

            // Measure second black bar
            while (j < width && line[j] <= threshold) { bar2++; j++; }
            if (abs(bar2 - bar1) > bar1 / 2 + 1) continue;

            // Valid start guard found
            *moduleWidth = (bar1 + space + bar2) / 3;
            if (*moduleWidth < 2) *moduleWidth = 2;
            return i + 1;
        }
    }
    return -1;
}

// ============ DECODE SINGLE DIGIT ============
int decodeDigit(uint8_t *pattern, bool isRight, bool *isG) {
    uint8_t code = 0;
    for (int i = 0; i < 7; i++) {
        code = (code << 1) | (pattern[i] ? 1 : 0);
    }

    // Try normal orientation
    for (int d = 0; d < 10; d++) {
        if (isRight) {
            if (code == EAN_R[d]) return d;
        } else {
            if (code == EAN_L[d]) { if (isG) *isG = false; return d; }
            if (code == EAN_G[d]) { if (isG) *isG = true; return d; }
        }
    }

    // Try inverted (white/black swapped)
    code = ~code & 0x7F;
    for (int d = 0; d < 10; d++) {
        if (isRight) {
            if (code == EAN_R[d]) return d;
        } else {
            if (code == EAN_L[d]) { if (isG) *isG = false; return d; }
            if (code == EAN_G[d]) { if (isG) *isG = true; return d; }
        }
    }

    return -1;
}

// ============ READ 7-MODULE PATTERN (multi-sample) ============
void readPattern(uint8_t *line, int startX, int moduleWidth, int threshold, uint8_t *pattern, int width) {
    for (int m = 0; m < 7; m++) {
        // Sample at 3 points within each module and take majority vote
        int count = 0;
        for (int s = 0; s < 3; s++) {
            int px = startX + m * moduleWidth + (moduleWidth * (s + 1)) / 4;
            if (px >= 0 && px < width && line[px] <= threshold) count++;
        }
        pattern[m] = (count >= 2) ? 1 : 0;
    }
}

// ============ VERIFY EAN CHECKSUM ============
bool verifyEAN13Checksum(char *digits) {
    int sum = 0;
    for (int i = 0; i < 12; i++) {
        int val = digits[i] - '0';
        sum += (i % 2 == 0) ? val : val * 3;
    }
    int expected = (10 - (sum % 10)) % 10;
    return (digits[12] - '0') == expected;
}

bool verifyEAN8Checksum(char *digits) {
    int sum = 0;
    for (int i = 0; i < 7; i++) {
        int val = digits[i] - '0';
        sum += (i % 2 == 0) ? val * 3 : val;
    }
    int expected = (10 - (sum % 10)) % 10;
    return (digits[7] - '0') == expected;
}

bool verifyUPCAChecksum(char *digits) {
    int sum = 0;
    for (int i = 0; i < 11; i++) {
        int val = digits[i] - '0';
        sum += (i % 2 == 0) ? val * 3 : val;
    }
    int expected = (10 - (sum % 10)) % 10;
    return (digits[11] - '0') == expected;
}

// ============ SCAN EAN-13 (13 digits) ============
BarcodeResult scanEAN13(uint8_t *line, int width, int threshold, int start, int moduleWidth) {
    BarcodeResult result;
    result.found = false;

    // EAN-13: 95 modules = 3 (start) + 42 (left) + 5 (center) + 42 (right) + 3 (end)
    if (start + moduleWidth * 95 > width) return result;

    char digits[14] = {0};
    uint8_t parityPattern = 0;
    uint8_t pattern[7];

    // Decode left 6 digits
    int x = start + moduleWidth * 3;  // Skip start guard
    for (int d = 0; d < 6; d++) {
        readPattern(line, x + d * 7 * moduleWidth, moduleWidth, threshold, pattern, width);
        bool isG = false;
        int digit = decodeDigit(pattern, false, &isG);
        if (digit < 0) return result;
        digits[d + 1] = '0' + digit;
        if (isG) parityPattern |= (1 << (5 - d));
    }

    // Decode first digit from parity
    digits[0] = '?';
    for (int fd = 0; fd < 10; fd++) {
        if (EAN_FIRST[fd] == parityPattern) {
            digits[0] = '0' + fd;
            break;
        }
    }
    if (digits[0] == '?') return result;

    // Decode right 6 digits
    x = start + moduleWidth * 50;  // After center guard
    for (int d = 0; d < 6; d++) {
        readPattern(line, x + d * 7 * moduleWidth, moduleWidth, threshold, pattern, width);
        int digit = decodeDigit(pattern, true, NULL);
        if (digit < 0) return result;
        digits[d + 7] = '0' + digit;
    }

    // Verify checksum
    if (!verifyEAN13Checksum(digits)) {
        Serial.printf("[EAN13] Checksum FAIL: %s\n", digits);
        return result;
    }

    result.found = true;
    result.type = "EAN13";
    result.data = String(digits);
    return result;
}

// ============ SCAN EAN-8 (8 digits) ============
BarcodeResult scanEAN8(uint8_t *line, int width, int threshold, int start, int moduleWidth) {
    BarcodeResult result;
    result.found = false;

    // EAN-8: 67 modules = 3 (start) + 28 (left) + 5 (center) + 28 (right) + 3 (end)
    if (start + moduleWidth * 67 > width) return result;

    char digits[9] = {0};
    uint8_t pattern[7];

    // Decode left 4 digits (all L-codes)
    int x = start + moduleWidth * 3;
    for (int d = 0; d < 4; d++) {
        readPattern(line, x + d * 7 * moduleWidth, moduleWidth, threshold, pattern, width);
        int digit = decodeDigit(pattern, false, NULL);
        if (digit < 0) return result;
        digits[d] = '0' + digit;
    }

    // Decode right 4 digits (all R-codes)
    x = start + moduleWidth * 36;  // After center guard
    for (int d = 0; d < 4; d++) {
        readPattern(line, x + d * 7 * moduleWidth, moduleWidth, threshold, pattern, width);
        int digit = decodeDigit(pattern, true, NULL);
        if (digit < 0) return result;
        digits[d + 4] = '0' + digit;
    }

    // Verify checksum
    if (!verifyEAN8Checksum(digits)) {
        Serial.printf("[EAN8] Checksum FAIL: %s\n", digits);
        return result;
    }

    result.found = true;
    result.type = "EAN8";
    result.data = String(digits);
    return result;
}

// ============ SCAN UPC-A (12 digits) ============
BarcodeResult scanUPCA(uint8_t *line, int width, int threshold, int start, int moduleWidth) {
    BarcodeResult result;
    result.found = false;

    // UPC-A: 95 modules (same as EAN-13, but all L-codes on left)
    if (start + moduleWidth * 95 > width) return result;

    char digits[13] = {0};
    uint8_t pattern[7];

    // Decode left 6 digits (all L-codes)
    int x = start + moduleWidth * 3;
    for (int d = 0; d < 6; d++) {
        readPattern(line, x + d * 7 * moduleWidth, moduleWidth, threshold, pattern, width);
        int digit = decodeDigit(pattern, false, NULL);
        if (digit < 0) return result;
        digits[d] = '0' + digit;
    }

    // Decode right 6 digits (all R-codes)
    x = start + moduleWidth * 50;
    for (int d = 0; d < 6; d++) {
        readPattern(line, x + d * 7 * moduleWidth, moduleWidth, threshold, pattern, width);
        int digit = decodeDigit(pattern, true, NULL);
        if (digit < 0) return result;
        digits[d + 6] = '0' + digit;
    }

    // Verify checksum
    if (!verifyUPCAChecksum(digits)) {
        Serial.printf("[UPCA] Checksum FAIL: %s\n", digits);
        return result;
    }

    result.found = true;
    result.type = "UPCA";
    result.data = String(digits);
    return result;
}

// ============ SCAN ALL 1D BARCODES ============
BarcodeResult scan1DBarcode(camera_fb_t *fb) {
    BarcodeResult result;
    result.found = false;

    if (fb->format != PIXFORMAT_GRAYSCALE) {
        return result;
    }

    int width = fb->width;
    int height = fb->height;
    uint8_t *pixels = fb->buf;

    // Scan multiple horizontal lines
    int scanLines[] = { height/2, height/3, height*2/3, height/4, height*3/4,
                        height*2/5, height*3/5, height*5/12, height*7/12 };
    int numLines = 9;

    // Temporary buffer for reversed line
    static uint8_t reversedLine[1280];  // Max width

    for (int sl = 0; sl < numLines; sl++) {
        int y = scanLines[sl];
        uint8_t *line = pixels + y * width;

        // Calculate adaptive threshold for this line
        uint8_t minVal = 255, maxVal = 0;
        for (int x = 0; x < width; x++) {
            if (line[x] < minVal) minVal = line[x];
            if (line[x] > maxVal) maxVal = line[x];
        }
        int threshold = (minVal + maxVal) / 2;

        // Skip low contrast lines
        if (maxVal - minVal < 60) continue;

        // Try multiple start guards on same line
        for (int attempt = 0; attempt < 5; attempt++) {
            int searchStart = attempt * (width / 6);
            int moduleWidth = 0;

            // Temporary modify line pointer for search offset
            int start = -1;
            for (int i = searchStart + 20; i < width - 200; i++) {
                if (line[i] > threshold && line[i+1] <= threshold) {
                    int bar1 = 0, space = 0, bar2 = 0;
                    int j = i + 1;

                    while (j < width && line[j] <= threshold) { bar1++; j++; }
                    if (bar1 < 2 || bar1 > 25) continue;

                    while (j < width && line[j] > threshold) { space++; j++; }
                    if (space < 1 || abs(space - bar1) > bar1) continue;

                    while (j < width && line[j] <= threshold) { bar2++; j++; }
                    if (abs(bar2 - bar1) > bar1 / 2 + 1) continue;

                    moduleWidth = (bar1 + space + bar2) / 3;
                    if (moduleWidth >= 2 && moduleWidth <= 20) {
                        start = i + 1;
                        break;
                    }
                }
            }

            if (start < 0) continue;

            // Try different module width variations (+/- 20%)
            for (int mwVar = -2; mwVar <= 2; mwVar++) {
                int mw = moduleWidth + mwVar;
                if (mw < 2) continue;

                // Try EAN-13
                result = scanEAN13(line, width, threshold, start, mw);
                if (result.found) return result;

                // Try EAN-8
                result = scanEAN8(line, width, threshold, start, mw);
                if (result.found) return result;

                // Try UPC-A
                result = scanUPCA(line, width, threshold, start, mw);
                if (result.found) return result;
            }
        }

        // Also try scanning in reverse direction
        for (int x = 0; x < width; x++) {
            reversedLine[x] = line[width - 1 - x];
        }

        int moduleWidth = 0;
        int start = findStartGuard(reversedLine, width, threshold, &moduleWidth);
        if (start >= 0 && moduleWidth >= 2 && moduleWidth <= 20) {
            result = scanEAN13(reversedLine, width, threshold, start, moduleWidth);
            if (result.found) return result;

            result = scanEAN8(reversedLine, width, threshold, start, moduleWidth);
            if (result.found) return result;

            result = scanUPCA(reversedLine, width, threshold, start, moduleWidth);
            if (result.found) return result;
        }
    }

    return result;
}

// ============ SCAN QR CODE ============
BarcodeResult scanQRCode(camera_fb_t *fb) {
    BarcodeResult result;
    result.found = false;

    if (qr == NULL || fb->format != PIXFORMAT_GRAYSCALE) {
        return result;
    }

    int w = fb->width;
    int h = fb->height;

    if (quirc_resize(qr, w, h) < 0) {
        return result;
    }

    uint8_t *image = quirc_begin(qr, NULL, NULL);
    if (image == NULL) {
        return result;
    }

    memcpy(image, fb->buf, w * h);
    quirc_end(qr);

    int count = quirc_count(qr);
    for (int i = 0; i < count; i++) {
        struct quirc_code code;
        struct quirc_data data;

        quirc_extract(qr, i, &code);
        if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
            result.found = true;
            result.type = "QR";
            result.data = String((const char *)data.payload);
            Serial.printf("[QR] SUCCESS: %s\n", data.payload);
            return result;
        }
    }

    return result;
}

// ============ MAIN SCAN FUNCTION ============
BarcodeResult scanBarcode(camera_fb_t *fb) {
    BarcodeResult result;
    result.found = false;

    Serial.println("[SCAN] Analyzing frame...");
    Serial.printf("[SCAN] Size: %dx%d, Format: %d\n", fb->width, fb->height, fb->format);

    // Try QR code first
    result = scanQRCode(fb);
    if (result.found) return result;

    // Try 1D barcodes (EAN-13, EAN-8, UPC-A)
    Serial.println("[SCAN] Trying 1D barcodes...");
    result = scan1DBarcode(fb);
    if (result.found) return result;

    // Debug: analyze image quality
    if (fb->format == PIXFORMAT_GRAYSCALE) {
        uint8_t minVal = 255, maxVal = 0;
        long sum = 0;
        for (int i = 0; i < (int)fb->len; i += 100) {
            uint8_t v = fb->buf[i];
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
            sum += v;
        }
        int avg = sum / (fb->len / 100);
        int contrast = maxVal - minVal;

        Serial.printf("[SCAN] Brightness=%d, Contrast=%d (min=%d, max=%d)\n",
                      avg, contrast, minVal, maxVal);

        if (contrast < 80) {
            Serial.println("[SCAN] HINT: Low contrast - improve lighting");
        }
        if (avg < 60) {
            Serial.println("[SCAN] HINT: Too dark - enable flash");
        } else if (avg > 190) {
            Serial.println("[SCAN] HINT: Too bright - reduce light");
        }
    }

    Serial.println("[SCAN] No barcode detected");
    return result;
}

// ============ CLEANUP ============
void cleanupBarcodeScanner() {
    if (qr != NULL) {
        quirc_destroy(qr);
        qr = NULL;
    }
}

#endif
