# Requirements: AuraOS Moodlight — Stabilisierung

**Defined:** 2026-03-25
**Core Value:** Das Moodlight läuft stabil und zuverlässig im Dauerbetrieb — ohne unerklärliches Blinken, Hänger oder unerwartete Neustarts.

## v1 Requirements

Requirements for the stabilization milestone. Each maps to roadmap phases.

### Firmware — LED Stabilität

- [x] **LED-01**: LED-Array ist gegen Buffer-Overflow geschützt — `ledColors` verwendet `MAX_LEDS` Konstante, `numLeds` wird beim Laden validiert
- [x] **LED-02**: LED-Updates flackern nicht bei WiFi/MQTT-Reconnects — keine `pixels.show()` während aktiver Reconnect-Phase
- [x] **LED-03**: Status-LED blinkt nicht bei kurzen Verbindungsunterbrechungen (<30s) — Debounce mit konfigurierbarem Threshold

### Firmware — Speicher & Stabilität

- [x] **MEM-01**: JSON Buffer Pool hat kein Memory Leak — RAII-Guard stellt sicher dass Heap-Allokationen immer freigegeben werden, auch bei Mutex-Timeout
- [x] **MEM-02**: Health-Checks sind in einer einzigen Routine konsolidiert — kein doppelter Timer (1h + 5min), klare Eskalation von Warnung zu Neustart

### Firmware — Sicherheit

- [x] **SEC-01**: API-Responses enthalten keine Klartext-Passwörter — `/api/export/settings` und `/api/settings/mqtt` maskieren WiFi- und MQTT-Passwörter mit `"****"`

### Backend — Production Readiness

- [ ] **BE-01**: Flask läuft hinter Gunicorn statt Dev-Server — `gunicorn -w 1 -b 0.0.0.0:6237 app:app` in Dockerfile, Background Worker startet nur einmal
- [ ] **BE-02**: Socket-Timeouts sind per-Connection statt global — kein `socket.setdefaulttimeout()`, stattdessen `requests.get(url, timeout=15)` für Feed-Fetching

### Backend — Datenqualität

- [x] **DATA-01**: RSS-Feed-Liste existiert nur einmal — `background_worker.py` importiert Feeds aus `app.py` oder einer shared Config
- [x] **DATA-02**: Sentiment-Kategorie-Thresholds sind konsistent — eine einzige `get_sentiment_category()` Funktion, verwendet von app.py, background_worker.py und moodlight_extensions.py
- [x] **DATA-03**: Tote API-Endpoints sind entfernt — `/api/dashboard` und `/api/logs` existieren nicht mehr

### Repository — Hygiene

- [x] **REPO-01**: `.env` ist in `.gitignore` — kein Risiko dass API-Keys oder DB-Passwörter versehentlich committed werden
- [x] **REPO-02**: Temporäre und binäre Dateien sind aus Git entfernt — `setup.html.tmp.html` gelöscht, `releases/`, `*.tmp.html` in `.gitignore`

## v2 Requirements

Deferred to future milestones. Tracked but not in current roadmap.

### Firmware Modularisierung (Milestone 2)

- **MOD-01**: `moodlight.cpp` ist in logische Module aufgeteilt (wifi_manager, led_controller, web_handlers, mqtt_handler, settings)
- **MOD-02**: Globale Variablen sind durch Klassen-Interfaces ersetzt

### HTTPS (Milestone 3)

- **HTTPS-01**: Backend-Kommunikation läuft über HTTPS statt HTTP
- **HTTPS-02**: ESP32 verwendet `WiFiClientSecure` mit CA-Bundle oder Certificate Pinning

### Qualität (Future)

- **TEST-01**: Backend hat Unit-Tests für Sentiment-Scoring und API-Endpoints (pytest)
- **TEST-02**: GPIO-Pin-Validierung gegen gültige ESP32-Pins
- **HEALTH-01**: Backend `/health` Endpoint für Uptime Kuma Monitoring
- **COST-01**: OpenAI API-Call Counter mit konfigurierbarem Tageslimit

## Out of Scope

| Feature | Reason |
|---------|--------|
| Authentifizierung/Autorisierung | Privates Projekt im Heimnetz, kein Bedarf |
| BLE Proxy für Bermuda | ~70KB RAM, ESPHome-Protokoll inkompatibel, eigener Milestone |
| Mobile App | Web-Interface reicht |
| Multi-Device-Support | Ein Gerät im Einsatz |
| feedconfig Persistierung | Endpoint ist de facto no-op, nicht in Scope |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| LED-01 | Phase 1 | Complete |
| LED-02 | Phase 1 | Complete |
| LED-03 | Phase 1 | Complete |
| MEM-01 | Phase 1 | Complete |
| MEM-02 | Phase 1 | Complete |
| SEC-01 | Phase 1 | Complete |
| BE-01 | Phase 2 | Pending |
| BE-02 | Phase 2 | Pending |
| DATA-01 | Phase 2 | Complete |
| DATA-02 | Phase 2 | Complete |
| DATA-03 | Phase 2 | Complete |
| REPO-01 | Phase 2 | Complete |
| REPO-02 | Phase 2 | Complete |

**Coverage:**
- v1 requirements: 13 total
- Mapped to phases: 13
- Unmapped: 0

---
*Requirements defined: 2026-03-25*
*Last updated: 2026-03-25 after roadmap creation*
