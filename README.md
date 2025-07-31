# AuraOS - Intelligent Mood Light

<div align="center">

![AuraOS Version](https://img.shields.io/badge/AuraOS-v8.6-blue)
![Platform](https://img.shields.io/badge/Platform-ESP32-green)
![Framework](https://img.shields.io/badge/Framework-Arduino-red)
![License](https://img.shields.io/badge/License-MIT-yellow)

*Ein intelligentes IoT-Stimmungslicht, das die Weltlage durch KI-gestützte Sentiment-Analyse visualisiert*

[Features](#features) • [Installation](#installation) • [Konfiguration](#konfiguration) • [API](#api) • [Entwicklung](#entwicklung)

</div>

## Überblick

AuraOS ist ein innovatives ESP32-basiertes Smart Home Gerät, das aktuelle Nachrichten analysiert und die ermittelte Stimmung durch dynamische LED-Beleuchtung visualisiert. Das System integriert sich nahtlos in Home Assistant und bietet eine vollständige Web-Oberfläche für Konfiguration und Monitoring.

### Kernkonzept

- **Sentiment-Analyse**: Verarbeitet Nachrichten-APIs und ermittelt die Weltlage-Stimmung (-1.0 bis +1.0)
- **Visuelle Darstellung**: NeoPixel LEDs zeigen Stimmungen durch Farben (Rot=negativ, Violett=positiv)
- **Smart Integration**: Vollständige Home Assistant MQTT Integration
- **Web Interface**: Responsive Konfigurationsoberfläche mit Dark/Light Theme

## Features

### Stimmungsvisualisierung
- **5-stufige Farbskala**: Von Rot (sehr negativ) bis Violett (sehr positiv)
- **Konfigurierbare Farben**: Individuelle Anpassung der Stimmungsfarben
- **Sanfte Übergänge**: Wellenförmige Farbanimationen
- **Helligkeitssteuerung**: Automatische und manuelle Anpassung

### Smart Home Integration
- **Home Assistant**: Native MQTT Integration mit Auto-Discovery
- **Sensor-Daten**: Temperatur/Luftfeuchtigkeit über DHT22
- **Heartbeat-Monitoring**: Statusüberwachung und Verfügbarkeitsmeldungen
- **mDNS Support**: Automatische Netzwerkerkennung

### Web Interface
- **Dashboard**: Systemstatus, Stimmungsanzeige, Sensor-Werte
- **Konfiguration**: WiFi, MQTT, Hardware-Einstellungen
- **Statistiken**: Historische Sentiment-Daten mit Charts
- **Diagnose**: System-Gesundheit, Speicher, Netzwerk-Analyse
- **OTA Updates**: Firmware-Updates über Web-Interface

### Datenmanagement
- **CSV Export/Import**: Historische Daten sichern und wiederherstellen
- **Buffered Logging**: Intelligente Datensammlung mit automatischem Flush
- **Backup-System**: Sichere Konfigurationsspeicherung
- **Memory Monitoring**: Heap-Überwachung und Leak-Detection

### Systemüberwachung
- **Watchdog Management**: Automatische Neustart-Überwachung
- **Network Diagnostics**: WiFi-Signal und Verbindungsanalyse
- **Health Checks**: Umfassende Systemstatusüberwachung
- **Error Recovery**: Robuste Fehlerbehandlung und Wiederherstellung

## Hardware Requirements

### Minimal Setup
- **ESP32** Development Board
- **NeoPixel LED Strip** (WS2812B/WS2813)
- **Stromversorgung** 5V (abhängig von LED-Anzahl)

### Erweiterte Konfiguration
- **DHT22** Sensor für Temperatur/Luftfeuchtigkeit
- **Gehäuse** für professionelle Installation
- **Level Shifter** (optional für stabile LED-Signale)

### Standardpins
```cpp
#define DEFAULT_LED_PIN 26      // NeoPixel Data
#define DEFAULT_DHT_PIN 17      // DHT22 Sensor
#define DEFAULT_NUM_LEDS 12     // LED Anzahl
```

## Installation

### 1. PlatformIO Setup
```bash
# Repository klonen
git clone https://github.com/username/auraos-moodlight.git
cd auraos-moodlight

# Dependencies installieren (automatisch via platformio.ini)
pio lib install

# Projekt kompilieren
pio run
```

### 2. Firmware Upload
```bash
# Firmware flashen
pio run --target upload

# Web-Interface flashen
pio run --target uploadfs
```

### 3. Ersteinrichtung
1. **Setup-Modus**: Beim ersten Start öffnet das Gerät "Moodlight-Setup" WiFi
2. **Captive Portal**: Verbinde dich und konfiguriere WiFi-Zugangsdaten
3. **Web-Interface**: Öffne http://moodlight.local oder die lokale IP
4. **Konfiguration**: MQTT-Broker und API-Endpoints einrichten

## Konfiguration

### WiFi & Netzwerk
```json
{
  "wifi_ssid": "IhrWiFiName",
  "wifi_password": "IhrWiFiPasswort",
  "hostname": "moodlight",
  "static_ip": "192.168.1.100" // optional
}
```

### MQTT & Home Assistant
```json
{
  "mqtt_server": "192.168.1.10",
  "mqtt_port": 1883,
  "mqtt_user": "homeassistant",
  "mqtt_password": "geheim",
  "device_name": "AuraOS Moodlight"
}
```

### Sentiment API
```json
{
  "api_url": "http://analyse.godsapp.de/api/news/total_sentiment",
  "update_interval": 30,  // Minuten
  "api_timeout": 10000    // ms
}
```

### Hardware Settings
```json
{
  "led_pin": 26,
  "dht_pin": 17,
  "num_leds": 12,
  "brightness": 255,
  "wave_duration": 10000    // ms
}
```

## Home Assistant Integration

AuraOS registriert sich automatisch in Home Assistant mit folgenden Entitäten:

### Sensoren
- `sensor.auraos_sentiment` - Aktueller Sentiment-Wert (-1.0 bis 1.0)
- `sensor.auraos_temperature` - Temperatur (°C)
- `sensor.auraos_humidity` - Luftfeuchtigkeit (%)
- `sensor.auraos_uptime` - Betriebszeit
- `sensor.auraos_free_heap` - Verfügbarer Speicher

### Schalter & Steuerung
- `light.auraos_moodlight` - LED-Steuerung (Ein/Aus, Helligkeit, Farbe)
- `switch.auraos_auto_mode` - Automatischer Stimmungsmodus
- `number.auraos_brightness` - Helligkeitssteuerung

### Automation Beispiel
```yaml
automation:
  - alias: "Moodlight Nachtmodus"
    trigger:
      platform: time
      at: "22:00:00"
    action:
      service: number.set_value
      target:
        entity_id: number.auraos_brightness
      data:
        value: 50
```

## Web API Endpoints

### Status & Information
- `GET /api/status` - Systemstatus und Sensordaten
- `GET /api/sentiment` - Aktueller Sentiment-Wert
- `GET /api/health` - System-Gesundheitscheck
- `GET /api/info` - Geräteinformationen

### Konfiguration
- `GET /api/config` - Aktuelle Konfiguration abrufen
- `POST /api/config` - Konfiguration aktualisieren
- `POST /api/wifi` - WiFi-Einstellungen ändern
- `POST /api/mqtt` - MQTT-Konfiguration

### Steuerung
- `POST /api/led/color` - LED-Farbe setzen (`{"r":255,"g":0,"b":0}`)
- `POST /api/led/brightness` - Helligkeit setzen (`{"brightness":128}`)
- `POST /api/mode` - Modus wechseln (`{"mode":"auto|manual"}`)

### Daten
- `GET /api/export/csv` - CSV-Datenexport
- `POST /api/import/csv` - CSV-Datenimport
- `GET /api/logs` - System-Logs abrufen

## Entwicklung

### Build System
Das Projekt nutzt PlatformIO mit folgender Struktur:
```
moodlight/
├── src/                  # Hauptquellcode
│   ├── moodlight.cpp    # Hauptanwendung
│   ├── MoodlightUtils.* # Utility-Bibliothek
│   └── config.h         # Konfiguration
├── data/                # Web-Interface Dateien
│   ├── index.html       # Dashboard
│   ├── setup.html       # Konfiguration
│   ├── mood.html        # Statistiken
│   └── diagnostics.html # Diagnose
├── lib/                 # Externe Bibliotheken
└── platformio.ini       # Build-Konfiguration
```

### Abhängigkeiten
```ini
lib_deps =
    adafruit/Adafruit NeoPixel@^1.12.5
    bblanchon/ArduinoJson@^7.4.0
    dawidchyrzynski/home-assistant-integration@^2.1.0
    adafruit/DHT sensor library@^1.4.6
    tobozo/ESP32-targz@^1.2.7
```

### Debug-Modus
```cpp
// In config.h aktivieren
#define DEBUG_MODE

// Oder über Build-Flags
build_flags = -DDEBUG_MODE
```

### Custom Features hinzufügen
1. **Neue Utility-Klasse**: In `MoodlightUtils.h` definieren
2. **Web-Endpoint**: In `handleWebRequests()` registrieren
3. **MQTT-Entity**: In `setupHA()` konfigurieren
4. **UI-Element**: In entsprechender HTML-Datei ergänzen

## Troubleshooting

### Häufige Probleme

**WiFi-Verbindung fehlgeschlagen**
- Setup-Modus aktivieren (Reset-Button 5 Sekunden halten)
- Captive Portal öffnen und neue Zugangsdaten eingeben

**LEDs funktionieren nicht**
- Pin-Konfiguration in Setup überprüfen
- Stromversorgung ausreichend dimensioniert?
- Level Shifter für Signalintegrität verwenden

**Home Assistant Integration**
- MQTT-Broker erreichbar und Zugangsdaten korrekt?
- Auto-Discovery in HA aktiviert?
- Device-Registry in HA prüfen

**OTA Update fehlgeschlagen**
- Ausreichend freier Speicher verfügbar?
- Netzwerkverbindung stabil?
- .tgz-Datei korrekt formatiert?

### Debug-Informationen
- **Serial Monitor**: `pio device monitor --baud 115200`
- **Web-Diagnose**: http://moodlight.local/diagnostics
- **System-Logs**: Über Web-API `/api/logs` abrufbar

## Changelog

### v8.6 (Aktuell)
- Verbesserte Speicherverwaltung mit JSON-Buffer-Pool
- Erweiterte Systemdiagnose und Health-Checks
- Optimierte Web-Interface Performance
- CSV Import/Export Funktionalität

### v8.5
- Home Assistant Auto-Discovery
- MQTT Heartbeat-Monitoring
- Dark/Light Theme Support
- Erweiterte Netzwerkdiagnostik

### v8.2
- LittleFS Dateisystem Integration
- OTA Update System
- Captive Portal Setup
- DHT Sensor Support

## Contributing

Beiträge sind willkommen! Bitte:

1. Fork das Repository
2. Feature Branch erstellen (`git checkout -b feature/AmazingFeature`)
3. Commits mit aussagekräftigen Nachrichten
4. Push zum Branch (`git push origin feature/AmazingFeature`)  
5. Pull Request erstellen

### Code Style
- Deutsche Kommentare für Hardware-nahe Teile
- Englische Kommentare für allgemeine Funktionen
- Konsistente Einrückung (2 Spaces)
- Aussagekräftige Variablennamen

## Lizenz

Dieses Projekt steht unter der MIT-Lizenz. Siehe [LICENSE](LICENSE) für Details.

## Danksagungen

- **ArduinoHA**: Für die exzellente Home Assistant Integration
- **PlatformIO**: Für das moderne ESP32 Build-System
- **Adafruit**: Für die zuverlässigen NeoPixel-Bibliotheken
- **Community**: Für Feedback und Verbesserungsvorschläge

---

<div align="center">

**Entwickelt für das Smart Home**

[Star this repo](https://github.com/username/auraos-moodlight) • [Report Bug](https://github.com/username/auraos-moodlight/issues) • [Request Feature](https://github.com/username/auraos-moodlight/issues)

</div>