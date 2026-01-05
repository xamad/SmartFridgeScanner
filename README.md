# Smart Fridge Barcode Scanner

Sistema di gestione inventario frigorifero con scansione automatica barcode/QR, rilevamento data scadenza OCR, e interfaccia web.

## Caratteristiche

- Scansione automatica barcode/QR tramite PIR motion sensor
- Rilevamento data scadenza tramite OCR (locale su S3, remoto su CAM)
- Due modalita: INGRESSO (aggiungi) e USCITA (rimuovi)
- Deep sleep per risparmio energetico
- Web interface per gestione inventario
- Lista della spesa automatica per prodotti finiti
- Supporto ESP32-CAM, ESP32-S3-CAM, ESP32-WROVER

## Hardware Supportato

### ESP32-CAM (AI-Thinker)
- Camera OV2640
- OCR remoto via VPS
- Deep sleep con timer wake

### ESP32-S3-CAM
- Camera OV2640/OV5640
- OCR locale con AI
- Deep sleep con PIR wake

### ESP32-WROVER Kit
- Camera OV2640
- OCR remoto via VPS
- Deep sleep con PIR wake
- Display TFT opzionale

## Pinout

### ESP32-CAM
```
PIR Sensor: GPIO 13
BOOT Button: GPIO 0
Flash LED: GPIO 4
```

### ESP32-S3
```
PIR Sensor: GPIO 1
BOOT Button: GPIO 0
Flash LED: GPIO 48
```

### ESP32-WROVER Kit
```
PIR Sensor: GPIO 34
BOOT Button: GPIO 0
Flash LED: GPIO 4
```

### Display TFT 2" SPI (Opzionale - Solo WROVER)

Per ESP32-WROVER Kit con display TFT ILI9341/ST7789:

```
TFT Pin    ESP32 WROVER Pin
-------    ----------------
VCC        3.3V
GND        GND
CS         GPIO 5
RST        GPIO 16
DC         GPIO 17
MOSI       GPIO 23
SCLK       GPIO 18
LED        3.3V (o GPIO per dimming)
MISO       (non usato, solo lettura)
```

**Nota:** Su ESP32-CAM questi pin sono usati dalla camera, quindi il display TFT NON e' supportato.

## Funzionamento

### Controlli
- **BOOT breve (<1 sec):** Scansione manuale
- **BOOT lungo (>1 sec):** Cambia modalita IN/OUT
- **PIR motion:** Scansione automatica

### LED Feedback
- **Lampeggio veloce:** Scansione in corso
- **Luce bassa (30%):** Modalita INGRESSO (verde)
- **Luce alta (80%):** Modalita USCITA (rosso)

### Deep Sleep
Dopo 5 minuti di inattivita, il dispositivo entra in deep sleep.
Si risveglia automaticamente al rilevamento PIR o tramite timer.

## Server VPS

Il server backend si trova in `/server/` con istruzioni complete in `server/CLAUDE.md`.

**Endpoint:** https://frigo.xamad.net

### API
- `POST /api/product` - Riceve barcode da ESP32
- `POST /api/ocr` - OCR per data scadenza
- `GET /api/inventory` - Lista prodotti
- `GET /api/shopping` - Lista della spesa
- `POST /api/manual` - Inserimento manuale

## Struttura Progetto

```
SmartFridgeScanner/
├── SmartFridgeScanner/          # Firmware Arduino
│   ├── SmartFridgeScanner.ino   # Main
│   ├── config.h                 # Configurazione
│   ├── camera_config.h          # Pin camera
│   ├── led_feedback.h           # LED e speaker
│   ├── wifi_manager.h           # WiFi setup
│   ├── barcode_scanner.h        # QR/Barcode
│   └── api_client.h             # HTTP client
│
├── server/                      # Backend Node.js
│   ├── server.js                # API server
│   ├── package.json             # Dependencies
│   ├── public/index.html        # Web UI
│   └── CLAUDE.md                # Setup VPS
│
└── README.md
```

## Compilazione Firmware

1. Aprire Arduino IDE
2. Selezionare board: ESP32-CAM / ESP32S3 Dev Module / ESP32 Wrover Module
3. Installare librerie:
   - WiFiManager
   - ArduinoJson
   - ESP32QRCodeReader
4. Compilare e uploadare

## Configurazione WiFi

Al primo avvio, il dispositivo crea un access point:
- **SSID:** FridgeScanner
- **Password:** fridge2026
- **URL:** http://192.168.4.1

Connettersi e configurare la rete WiFi domestica.

## License

MIT License
