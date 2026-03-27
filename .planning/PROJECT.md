# AuraOS Moodlight

## What This Is

Ein ESP32-basiertes Smart-Moodlight, das die Weltstimmung durch Nachrichtenanalyse visualisiert. Ein Backend (Flask + PostgreSQL + Redis) analysiert deutsche RSS-Feeds mit GPT-4o-mini und berechnet einen Sentiment-Score (-1.0 bis +1.0). Der ESP32 pollt den Score alle 30 Minuten und stellt ihn als LED-Farbe dar (Rot = sehr negativ bis Violett = sehr positiv). Integration in Home Assistant via MQTT, Web-Interface zur Konfiguration. Privates Projekt, ein Gerät im Wohnzimmer.

## Core Value

Die Firmware ist modular aufgebaut — jedes Modul hat eine klare Verantwortung, ist einzeln lesbar und änderbar, ohne den Rest des Systems zu verstehen.

## Requirements

### Validated

- ✓ Sentiment-Analyse via GPT-4o-mini aus 12 deutschen RSS-Feeds — existing
- ✓ LED-Farbdarstellung basierend auf Sentiment-Score (5 Stufen) — existing
- ✓ Home Assistant Integration via MQTT (ArduinoHA) — existing
- ✓ Web-Interface für Konfiguration (WiFi, MQTT, Hardware, Diagnostics) — existing
- ✓ Captive Portal Setup-Modus für initiale WiFi-Konfiguration — existing
- ✓ OTA Firmware-Update über Web-Interface — existing
- ✓ DHT-Sensor für Temperatur/Luftfeuchtigkeit — existing
- ✓ Backend mit PostgreSQL-Persistenz und Redis-Caching — existing
- ✓ Background Worker für automatische Sentiment-Updates (30 min) — existing
- ✓ CI/CD Pipeline: GitHub Actions → GHCR → Portainer Webhook — existing
- ✓ CSV-Export/Import für Sentiment-Verlaufsdaten — existing
- ✓ Device-Tracking im Backend (Firmware-Version, Statistiken) — existing
- ✓ LED-Steuerung stabilisiert: WiFi/MQTT-Reconnect LED-Guard, Status-LED 30s Debounce — Phase 1
- ✓ Buffer-Overflow bei LED-Anzahl > 12 gefixt (MAX_LEDS 64) — Phase 1
- ✓ JSON Buffer Pool Memory Leak gefixt (RAII-Guard) — Phase 1
- ✓ Health-Checks konsolidiert (ein Timer, klare Eskalation) — Phase 1
- ✓ Credentials in API-Responses maskiert — Phase 1
- ✓ moodlight.cpp in logische Module aufgeteilt (6 Module) — v3.0 Phase 7
- ✓ AppState Shared-State-Fundament — v3.0 Phase 6
- ✓ Code-Qualität konsolidiert (Dead Code, Magic Numbers, Redundanz) — v3.0 Phase 8
- ✓ RSS-Feed-Liste in PostgreSQL statt hardcoded — v4.0 Phase 9
- ✓ GET/POST/DELETE /api/moodlight/feeds API-Endpoints — v4.0 Phase 10
- ✓ Background Worker liest Feeds aus DB — v4.0 Phase 9
- ✓ Focus-Feed (404) entfernt — v4.0 Phase 9
- ✓ Feed-URL-Validierung beim Hinzufügen — v4.0 Phase 10
- ✓ Web-Interface Feed-Management unter /feeds — v4.0 Phase 11

- ✓ Headlines + Einzel-Scores in DB gespeichert — v5.0 Phase 12
- ✓ GET /api/moodlight/headlines API-Endpoint — v5.0 Phase 14
- ✓ Backend-Dashboard mit Übersicht, Headlines, Feeds (934-Zeilen SPA) — v5.0 Phase 14
- ✓ Passwort-Login für Backend-Interface — v5.0 Phase 13
- ✓ ESP32 mood.html zeigt Headlines mit Einzel-Scores — v5.0 Phase 15
- ✓ GitHub Page erweitert mit Headline-Darstellung — v5.0 Phase 15

- ✓ Sentiment-Analyse auf Claude API (Anthropic SDK) umgestellt — v6.0 Phase 16
- ✓ Dynamische Score-Skalierung mit Perzentilen — v6.0 Phase 17
- ✓ Kalibrierter Prompt für ausgewogenere Scores — v6.0 Phase 16
- ✓ API liefert Skalierungs-Kontext (Rohwert, Perzentil, Bereich) — v6.0 Phase 17
- ✓ ESP32 bezieht led_index dynamisch vom Backend — v6.0 Phase 18
- ✓ Dashboard zeigt Skalierungs-Transparenz — v6.0 Phase 18

- ✓ Einstellungen in PostgreSQL persistiert — v7.0 Phase 19
- ✓ Analyse-Frequenz + Headlines im Dashboard konfigurierbar — v7.0 Phase 21
- ✓ API Key + Passwort im Dashboard änderbar — v7.0 Phase 21
- ✓ Manueller Analyse-Trigger im Dashboard — v7.0 Phase 20

- ✓ style.css komplett überarbeitet (1035 Zeilen, CSS-Variablen) — v8.0 Phase 22
- ✓ Alle 4 ESP32 Seiten an Dashboard-Design angeglichen — v8.0 Phase 23

- ✓ Feed-Trend-Analyse mit 7/30-Tage-Fenster — v9.0 Phase 24
- ✓ Feed-Ranking im Dashboard + GitHub Page — v9.0 Phase 25

### Active

- Watchdog-Feed während aller blockierenden Operationen — v10.0 FW-01
- MQTT Refresh nur Flag, HTTP in loop() — v10.0 FW-02
- initialStatesPending als einzelne AppState-Variable — v10.0 FW-03
- WiFi-Hysterese: 3s Pause nach Reconnect vor HTTP — v10.0 FW-04
- WiFi.disconnect() nicht während laufender HTTP-Op — v10.0 FW-05
- ESP32 mood.html zeigt Perzentil-Kontext (Badge, Balken, Erklärung) — v10.0 PZ-01..03
- GitHub Page zeigt identische Perzentil-Visualisierung — v10.0 PZ-04
- Fallback-Hinweis bei wenigen Datenpunkten — v10.0 PZ-05
- Attraktives, responsives Layout mit Dark/Light Mode — v10.0 PZ-06

### Out of Scope

- Authentifizierung/Autorisierung über Authentik/OAuth — einfacher Passwort-Schutz reicht (v5.0)
- HTTPS auf ESP32 — gestrichen, kein Bedarf im Heimnetz
- Combined Update (ein Upload statt zwei) — gestrichen, funktioniert so
- BLE Proxy für Bermuda — RAM reicht (228KB frei), aber ESPHome-Protokoll inkompatibel, vertagt
- Historische Headline-Suche — gestrichen, kein Bedarf
- Feed-Import/Export — kein Bedarf bei ~12 Feeds
- Mobile App — Web-Interface reicht
- Multi-Device-Support — ein Gerät im Einsatz

## Context

- Hardware: ESP32 dev board, 12 NeoPixel LEDs, DHT-Sensor, im Wohnzimmer unter http://192.168.0.37/
- Backend: Docker Stack auf server.godsapp.de, erreichbar unter http://analyse.godsapp.de
- v1.0 Stabilisierung shipped: LED-Flicker, Buffer-Overflow, RAII, Health-Check, Gunicorn, shared_config
- **Aktueller Build-Workflow**: `build-release.sh` liest Version aus `config.h`, baut UI-TGZ + Firmware-BIN getrennt in `releases/vX.X/`
- **Aktueller Update-Workflow**: Zwei getrennte Uploads auf http://192.168.0.140/diagnostics.html — erst UI-TGZ, dann Firmware-BIN
- **Versionsformat**: `MOODLIGHT_VERSION "9.0"` in config.h, UI-Version in `ui-version.txt` auf LittleFS
- **Dateinamen-Konvention**: `UI-X.X-AuraOS.tgz` und `Firmware-X.X-AuraOS.bin`
- **Bestehende Libraries**: ESP32-targz für TGZ-Extraktion, Arduino Update-API für Firmware-Flash
- **Pre-existing Bug**: `esp_task_wdt_init(30, false)` inkompatibel mit Arduino ESP32 Core 3.x — verhindert `pio run`
- Codebase-Map vorhanden in .planning/codebase/
- v4.0 shipped: Feed-Verwaltung über Web-UI und API, Background Worker liest aus DB
- Feed-Management-Seite: https://analyse.godsapp.de/feeds (nach Deploy)

## Constraints

- **Hardware**: ESP32 mit 4MB Flash, ~320KB RAM — Speichereffizienz kritisch
- **Partition**: min_spiffs.csv — maximaler Flash für App, minimaler Filesystem-Speicher
- **Framework**: Arduino + PlatformIO — kein Wechsel zu ESPHome geplant
- **Deployment**: OTA über Web-Interface, kein physischer Zugang zum Gerät nötig
- **Backend**: Docker Compose auf Hetzner-Server, CI/CD über GitHub Actions + Portainer

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| BLE Proxy vertagt | ~70KB RAM, ESPHome-Protokoll inkompatibel, Stabilität hat Vorrang | — Pending |
| Firmware-Splitting als eigener Milestone | Risiko für Regressionen zu hoch während Stabilitäts-Arbeiten | ✓ Good (v3.0) |
| HTTPS gestrichen | Kein Bedarf im Heimnetz | ✓ Good |
| Kein Auth | Privates Projekt im Heimnetz | ✓ Good |
| Unified Version gestrichen | Funktioniert so, kein Bedarf | ✓ Good |
| Combined TGZ gestrichen | Zwei Uploads reichen | ✓ Good |
| Feed-Management als eigenständige Backend-Seite statt ESP32 setup.html | Feeds sind Backend-Concern, ESP32 hat keinen Backend-Zugriff | ✓ Good |
| Firmware-Splitting erledigt | v3.0 hat moodlight.cpp in 6 Module aufgeteilt | ✓ Good |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd:transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd:complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
---
*Last updated: 2026-03-27 after v9.0 milestone complete (Sentiment-Trend pro Feed)*
