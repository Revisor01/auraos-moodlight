# Roadmap: AuraOS Moodlight — Stabilisierung

## Overview

Dieses Milestone-Roadmap behebt bekannte Stabilitätsprobleme im laufenden System. Keine neuen Features — ausschließlich In-Place-Korrekturen. Firmware und Backend sind unabhängige Arbeitsstränge: Phase 1 fixiert ESP32-Crashes, Heap-Leaks und LED-Flicker; Phase 2 härtet den Backend-Stack ab und bereinigt technische Schulden im Repository.

## Phases

- [ ] **Phase 1: Firmware-Stabilität** - ESP32-Crashes, Buffer-Overflow, Heap-Leak und LED-Flicker beseitigen
- [ ] **Phase 2: Backend-Hardening** - Flask produktionstauglich machen, Konfiguration konsolidieren, Repository säubern

## Phase Details

### Phase 1: Firmware-Stabilität
**Goal**: Der ESP32 läuft dauerhaft ohne Watchdog-Resets, Heap-Leaks oder unerklärliches LED-Blinken
**Depends on**: Nothing (first phase)
**Requirements**: LED-01, LED-02, LED-03, MEM-01, MEM-02, SEC-01
**Success Criteria** (what must be TRUE):
  1. LED-Strip flackert nicht mehr bei WiFi/MQTT-Reconnects — keine sichtbaren Blinkeffekte bei kurzen Verbindungsunterbrechungen
  2. Status-LED bleibt bei Verbindungsunterbrechungen unter 30 Sekunden ruhig
  3. `ESP.getFreeHeap()` bleibt nach 24h Dauerbetrieb stabil — kein kontinuierlicher Rückgang
  4. Gerät startet nach Konfiguration von `numLeds > 12` nicht neu und beschädigt keine benachbarten Variablen
  5. `/api/export/settings` und `/api/settings/mqtt` geben `"****"` für WiFi- und MQTT-Passwörter zurück
**Plans:** 3 plans

Plans:
- [ ] 01-01-PLAN.md — Buffer-Overflow Fix (LED-01) und JSON Buffer Pool RAII Guard (MEM-01)
- [ ] 01-02-PLAN.md — WiFi-Reconnect LED-Guard (LED-02) und Status-LED Debounce (LED-03)
- [ ] 01-03-PLAN.md — Health-Check Konsolidierung (MEM-02) und Credential-Maskierung (SEC-01)

### Phase 2: Backend-Hardening
**Goal**: Backend läuft produktionstauglich hinter Gunicorn, ohne duplizierte Konfiguration oder Secret-Leaks im Repository
**Depends on**: Phase 1
**Requirements**: BE-01, BE-02, DATA-01, DATA-02, DATA-03, REPO-01, REPO-02
**Success Criteria** (what must be TRUE):
  1. Docker-Logs zeigen genau einen "Sentiment update" alle 30 Minuten — kein doppelter Background-Worker
  2. Score 0.35 ergibt überall "positiv", Score 0.80 ergibt überall "sehr positiv" — konsistente Kategorie-Labels in API, Worker und Extensions
  3. RSS-Feed-Liste existiert an einer einzigen Stelle — `shared_config.py` ist die einzige Definition
  4. `/api/dashboard` und `/api/logs` geben 404 zurück
  5. `.env` und `*.tmp.html` sind nicht im Git-Repository enthalten — kein `git status` zeigt diese Dateien als tracked
**Plans**: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Firmware-Stabilität | 0/3 | Planned | - |
| 2. Backend-Hardening | 0/TBD | Not started | - |
