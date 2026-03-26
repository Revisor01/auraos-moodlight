# Requirements: AuraOS Moodlight — Firmware-Modularisierung

**Defined:** 2026-03-26
**Core Value:** Die Firmware ist modular aufgebaut — jedes Modul hat eine klare Verantwortung, ist einzeln lesbar und änderbar.

## v1 Requirements

### Module-Extraktion

- [ ] **MOD-01**: WiFi-Management ist in `wifi_manager.h/.cpp` extrahiert — AP-Mode, Captive Portal, Station-Connect, Reconnect-Logik mit wifiReconnectActive-Flag, disconnectStartMs-Debounce
- [ ] **MOD-02**: LED-Controller ist in `led_controller.h/.cpp` extrahiert — NeoPixel-Init, updateLEDs(), processLEDUpdates(), Status-LED, Pulse-Animation, ledMutex, ledColors[]
- [ ] **MOD-03**: Web-Server ist in `web_server.h/.cpp` extrahiert — Route-Registration, API-Handler (/api/status, /api/settings/*, /api/export/*), statische Datei-Handler, UI/Firmware-Upload-Handler
- [ ] **MOD-04**: MQTT/HA-Integration ist in `mqtt_handler.h/.cpp` extrahiert — setupHA(), MQTT-Connect/Reconnect, ArduinoHA-Entity-Definitionen, Heartbeat, Command-Handler
- [ ] **MOD-05**: Settings-Management ist in `settings_manager.h/.cpp` extrahiert — loadSettings(), saveSettings(), saveSettingsToFile(), loadSettingsFromJSON(), Preferences-API, JSON-Serialisierung
- [ ] **MOD-06**: Sensor & Sentiment ist in `sensor_manager.h/.cpp` extrahiert — DHT-Read, Sentiment-API-Fetch, mapSentimentToLED(), Fehler-Counter, Fallback-Logik

### Architektur

- [ ] **ARCH-01**: Shared State ist über ein `AppState`-Struct oder globale Header-Datei zugänglich — Module kommunizieren über definiertes Interface, nicht über extern-Variablen kreuz und quer
- [ ] **ARCH-02**: `moodlight.cpp` enthält nur noch `setup()`, `loop()` und Modul-Initialisierung — max 200 Zeilen
- [ ] **ARCH-03**: `pio run` baut fehlerfrei mit allen Modulen — kein Linking-Fehler, keine zirkulären Dependencies

### Qualität

- [ ] **QUAL-01**: Tote/unreachable Code-Blöcke sind entfernt (erkannt beim Aufteilen)
- [ ] **QUAL-02**: Magic Numbers sind durch benannte Konstanten in config.h ersetzt
- [ ] **QUAL-03**: Duplizierter Code innerhalb der Module ist konsolidiert

## v2 Requirements

### HTTPS (Future Milestone)

- **HTTPS-01**: Backend-Kommunikation läuft über HTTPS
- **HTTPS-02**: ESP32 verwendet WiFiClientSecure

## Out of Scope

| Feature | Reason |
|---------|--------|
| Unit-Tests | Eigener Milestone, braucht Test-Infrastruktur für ESP32 |
| Neue Features | Reines Refactoring, keine funktionalen Änderungen |
| Backend-Änderungen | Backend ist nicht betroffen |
| Web-UI-Änderungen | HTML/CSS/JS bleibt unverändert |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| MOD-01 | Pending | Pending |
| MOD-02 | Pending | Pending |
| MOD-03 | Pending | Pending |
| MOD-04 | Pending | Pending |
| MOD-05 | Pending | Pending |
| MOD-06 | Pending | Pending |
| ARCH-01 | Pending | Pending |
| ARCH-02 | Pending | Pending |
| ARCH-03 | Pending | Pending |
| QUAL-01 | Pending | Pending |
| QUAL-02 | Pending | Pending |
| QUAL-03 | Pending | Pending |

**Coverage:**
- v1 requirements: 12 total
- Mapped to phases: 0
- Unmapped: 12

---
*Requirements defined: 2026-03-26*
