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

### Active

- [ ] Sentiment-Analyse von OpenAI auf Claude API (Anthropic SDK) umstellen
- [ ] Dynamische Score-Skalierung basierend auf historischen Perzentilen
- [ ] Prompt-Optimierung für ausgewogenere Einzel-Scores
- [ ] Backend liefert Skalierungs-Kontext (Rohwert, Perzentil, historischer Bereich)
- [ ] ESP32 bezieht Schwellwerte dynamisch vom Backend
- [ ] Dashboard zeigt Skalierungs-Transparenz

### Out of Scope

- Authentifizierung/Autorisierung über Authentik/OAuth — Over-Engineering, einfacher Passwort-Schutz reicht
- HTTPS auf ESP32 — gab Probleme in der Vergangenheit, eigener Milestone später
- BLE Proxy für Bermuda — braucht vollen BLE-Scanner (~70KB RAM), ESPHome-Protokoll nicht kompatibel mit Custom-Firmware, eigener Milestone wenn überhaupt
- Firmware-Aufsplittung (Monolith → Module) — erledigt in v3.0
- Feed-Import/Export — kein Bedarf bei ~12 Feeds
- Feed-Kategorien/Tags — Over-Engineering für privates Projekt
- Automatische Feed-Discovery — manuelle Verwaltung reicht
- Mobile App — Web-Interface reicht
- Multi-Device-Support — ein Gerät im Einsatz
- Automatische Tests — wünschenswert, aber nicht in diesem Milestone

## Context

- Hardware: ESP32 dev board, 12 NeoPixel LEDs, DHT-Sensor, im Wohnzimmer unter http://192.168.0.140/
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
| Firmware-Splitting als eigener Milestone | Risiko für Regressionen zu hoch während Stabilitäts-Arbeiten | — Pending |
| HTTPS als eigener Milestone | Gab vorher Probleme, braucht WiFiClientSecure + Zertifikate | — Pending |
| Kein Auth | Privates Projekt im Heimnetz | ✓ Good |
| Unified Version statt getrennte UI/Firmware-Versionen | Eine Combined-Datei = eine Version, weniger Verwirrung | — Pending |
| Combined TGZ statt zwei Dateien | Ein Upload statt zwei, weniger Fehlerquellen | — Pending |
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
## Current Milestone: v6.0 Dynamische Bewertungsskala

**Goal:** Scoring-Pipeline von OpenAI auf Claude API umstellen, Bewertungsskala dynamisch machen, volle Farbpalette ausreizen.

**Target features:**
- Claude API (Anthropic SDK) statt OpenAI für Sentiment-Analyse
- Dynamische Skalierung basierend auf historischen Perzentilen
- Prompt-Optimierung für ausgewogenere Scores
- Backend-Transparenz (Rohwert, Perzentil, historischer Bereich)
- ESP32 dynamische Schwellwerte vom Backend
- Dashboard Skalierungs-Anzeige

---
*Last updated: 2026-03-27 after v6.0 milestone start (Dynamische Bewertungsskala)*
