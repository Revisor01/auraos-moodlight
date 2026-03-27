# Roadmap: AuraOS Moodlight

## Completed Milestones

- ✅ **v1.0: Stabilisierung** — 2 phases, 6 plans, 13 requirements (shipped 2026-03-25) → [archive](milestones/v1.0-ROADMAP.md)
- ✅ **v2.0: Combined Update + Build Automation** — 3 phases, 4 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v2.0-ROADMAP.md)
- ✅ **v3.0: Firmware-Modularisierung** — 3 phases, 11 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v3.0-ROADMAP.md)
- ✅ **v4.0: Konfigurierbare RSS-Feeds** — 3 phases, 4 plans, 9 requirements (shipped 2026-03-26) → [archive](milestones/v4.0-ROADMAP.md)
- ✅ **v5.0: Schlagzeilen-Transparenz & Dashboard** — 4 phases, 7 plans, 12 requirements (shipped 2026-03-27) → [archive](milestones/v5.0-ROADMAP.md)
- ✅ **v6.0: Dynamische Bewertungsskala** — 3 phases, 6 plans, 11 requirements (shipped 2026-03-27) → [archive](milestones/v6.0-ROADMAP.md)
- ✅ **v7.0: Dashboard-Einstellungen** — 3 phases, 6 plans, 13 requirements (shipped 2026-03-27) → [archive](milestones/v7.0-ROADMAP.md)

## Current Milestone: v8.0 ESP32 UI-Redesign

**Goal:** ESP32 Web-Interface an das Dashboard-Design angleichen — alle 4 Seiten + CSS.

## Phases

- [x] **Phase 22: CSS-Fundament** — style.css komplett überarbeiten mit CSS-Variablen und Dark/Light-Mode-Kompatibilität (completed 2026-03-27)
- [ ] **Phase 23: Seiten-Redesign** — alle 4 HTML-Seiten (index, setup, mood, diagnostics) im Dashboard-Stil umbauen

## Phase Details

### Phase 22: CSS-Fundament
**Goal**: Das ESP32 Web-Interface hat ein konsistentes visuelles Fundament aus CSS-Variablen, das alle Seiten teilen
**Depends on**: Nothing (erste Phase dieses Milestones)
**Requirements**: CSS-01, CSS-02
**Success Criteria** (what must be TRUE):
  1. style.css definiert alle Farben, Abstände, border-radius und Schriften über CSS-Variablen, die mit dem Backend-Dashboard übereinstimmen
  2. Dark Mode und Light Mode funktionieren weiterhin vollständig — kein Theme-Wechsel ist gebrochen
  3. Alle vier ESP32-Seiten laden ohne visuelle Fehler, wenn nur das neue style.css aktiv ist
**Plans**: 1 plan

Plans:
- [x] 22-01-PLAN.md — style.css komplett neu schreiben mit Dashboard-Design-Variablen und Dark-Mode

### Phase 23: Seiten-Redesign
**Goal**: Alle vier ESP32-Seiten sehen aus und verhalten sich konsistent wie das Backend-Dashboard, ohne Funktionsverlust
**Depends on**: Phase 22
**Requirements**: IDX-01, IDX-02, IDX-03, SET-01, SET-02, MOOD-01, MOOD-02, DIAG-01, DIAG-02
**Success Criteria** (what must be TRUE):
  1. index.html zeigt den Score mit Farbkodierung in 5 Stufen und Karten-Layout (border-radius 8px, kompaktes Padding)
  2. LED-Steuerung und Helligkeitsregler auf index.html funktionieren unverändert
  3. setup.html verwendet Formular-Design konsistent mit dem Dashboard-Einstellungs-Tab; alle Einstellungen (WiFi, MQTT, Hardware, Farben) bleiben vollständig bedienbar
  4. mood.html zeigt Headlines mit Farbkodierung und Score-Verlauf im Karten-Stil des Dashboards
  5. diagnostics.html zeigt System-Info (Speicher, WiFi, Uptime) in Dashboard-Karten; Firmware- und UI-Upload funktionieren ohne Einschränkung
**Plans**: 4 plans

Plans:
- [x] 23-01-PLAN.md — index.html: Score-Farbklassen + Dashboard-Karten-Layout
- [ ] 23-02-PLAN.md — setup.html: Formular-Design konsistent, alle 7 Tabs funktional
- [x] 23-03-PLAN.md — mood.html: Schlagzeilen-Farbkodierung per CSS-Klassen
- [x] 23-04-PLAN.md — diagnostics.html: System-Info-Karten im Dashboard-Stil

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 22. CSS-Fundament | 1/1 | Complete    | 2026-03-27 |
| 23. Seiten-Redesign | 3/4 | In Progress|  |
