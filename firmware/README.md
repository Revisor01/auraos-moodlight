# AuraOS Firmware

ESP32-basierte Firmware für das AuraOS Moodlight.

## Hardware

- **ESP32** Development Board
- **WS2812B LED Strip** (NeoPixel)
- **DHT22** Sensor (optional)
- **5V Netzteil** (2A+)

**Standard-Pins:**
```cpp
LED_PIN:  26
DHT_PIN:  17
NUM_LEDS: 12
```

## Build & Flash

### Mit PlatformIO

```bash
# Kompilieren
pio run

# Filesystem flashen (Web-UI)
pio run --target uploadfs

# Firmware flashen
pio run --target upload

# Serial Monitor
pio device monitor
```

### OTA Update

Siehe `../releases/` für fertige Binaries.

## Konfiguration

Web-Interface: `http://moodlight.local/setup`

- WiFi Zugangsdaten
- API-URL (Backend)
- MQTT Broker
- LED/DHT Pins
- Farben anpassen

## Web-Interface

- **`/`** - Dashboard mit Live-Anzeige
- **`/setup`** - Konfiguration
- **`/mood`** - Statistiken
- **`/diagnostics`** - System-Diagnose & OTA Updates

## API Endpoints (ESP32)

- `GET /api/status` - Systemstatus
- `POST /api/led/color` - LED-Farbe setzen
- `POST /api/mode` - Modus wechseln (auto/manual)
- `GET /api/backend/stats` - Backend-Statistiken

## Entwicklung

**Wichtige Dateien:**
- `src/moodlight.cpp` - Hauptcode
- `src/config.h` - Konfiguration
- `data/*.html` - Web-UI
- `data/js/*.js` - Frontend-Logik

**Dependencies** (auto-installiert):
- Adafruit NeoPixel
- ArduinoJson
- Home Assistant Integration
- DHT sensor library

## Troubleshooting

**WiFi Setup-Modus:**
Reset-Button 5 Sekunden halten → "Moodlight-Setup" WiFi

**Serial Debug:**
```bash
pio device monitor --baud 115200
```

**OTA Update fehlgeschlagen:**
- Genug freier Speicher? (Check `/diagnostics`)
- Firmware-Datei muss `Firmware-X.X-AuraOS.bin` heißen!
