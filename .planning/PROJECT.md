# AuraOS Moodlight

## What This Is

Ein ESP32-basiertes Smart-Moodlight, das die Weltstimmung durch Nachrichtenanalyse visualisiert. Ein Backend (Flask + PostgreSQL + Redis) analysiert deutsche RSS-Feeds mit GPT-4o-mini und berechnet einen Sentiment-Score (-1.0 bis +1.0). Der ESP32 pollt den Score alle 30 Minuten und stellt ihn als LED-Farbe dar (Rot = sehr negativ bis Violett = sehr positiv). Integration in Home Assistant via MQTT, Web-Interface zur Konfiguration. Privates Projekt, ein Gerät im Wohnzimmer.

## Core Value

Das Moodlight läuft stabil und zuverlässig im Dauerbetrieb — ohne unerklärliches Blinken, Hänger oder unerwartete Neustarts.

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

### Active

- [ ] Backend: Flask Dev-Server durch Gunicorn ersetzen
- [ ] Backend: Globale Socket-Timeouts durch per-Connection Timeouts ersetzen
- [ ] Backend: Duplizierte RSS-Feed-Listen konsolidieren
- [ ] Backend: Inkonsistente Sentiment-Kategorie-Thresholds vereinheitlichen
- [ ] Credentials nicht mehr über API exposen (Passwörter maskieren)
- [ ] Tote Endpoints entfernen (/api/dashboard, /api/logs)
- [ ] Aufräumen: tmp-Dateien, .env in .gitignore, unreachable Code

### Out of Scope

- Authentifizierung/Autorisierung — privates Projekt im Heimnetz, kein Bedarf
- HTTPS auf ESP32 — gab Probleme in der Vergangenheit, eigener Milestone später
- BLE Proxy für Bermuda — braucht vollen BLE-Scanner (~70KB RAM), ESPHome-Protokoll nicht kompatibel mit Custom-Firmware, eigener Milestone wenn überhaupt
- Firmware-Aufsplittung (Monolith → Module) — eigener Milestone nach Stabilisierung
- Mobile App — Web-Interface reicht
- Multi-Device-Support — ein Gerät im Einsatz
- Automatische Tests — wünschenswert, aber nicht in diesem Milestone

## Context

- Hardware: ESP32 dev board, 12 NeoPixel LEDs, DHT-Sensor, im Wohnzimmer unter http://192.168.0.140/
- Backend: Docker Stack auf server.godsapp.de, erreichbar unter http://analyse.godsapp.de
- Bekannte Symptome: Gelegentliches Blinken der LEDs (vermutlich bei WiFi/MQTT-Reconnects), manchmal hängt das System
- Firmware ist ein 4500-Zeilen Monolith in einer Datei (moodlight.cpp)
- Backend nutzt Flask Dev-Server in Produktion, hat duplizierte Konfiguration
- Sentiment-Kategorie-Thresholds sind inkonsistent zwischen app.py und background_worker.py/moodlight_extensions.py
- Codebase-Map vorhanden in .planning/codebase/

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
*Last updated: 2026-03-25 after Phase 1 completion*
