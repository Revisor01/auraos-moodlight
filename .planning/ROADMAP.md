# Roadmap: AuraOS Moodlight

## Completed Milestones

- ✅ **v1.0: Stabilisierung** — 2 phases, 6 plans, 13 requirements (shipped 2026-03-25) → [archive](milestones/v1.0-ROADMAP.md)
- ✅ **v2.0: Combined Update + Build Automation** — 3 phases, 4 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v2.0-ROADMAP.md)
- ✅ **v3.0: Firmware-Modularisierung** — 3 phases, 11 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v3.0-ROADMAP.md)
- ✅ **v4.0: Konfigurierbare RSS-Feeds** — 3 phases, 4 plans, 9 requirements (shipped 2026-03-26) → [archive](milestones/v4.0-ROADMAP.md)
- ✅ **v5.0: Schlagzeilen-Transparenz & Dashboard** — 4 phases, 7 plans, 12 requirements (shipped 2026-03-27) → [archive](milestones/v5.0-ROADMAP.md)
- ✅ **v6.0: Dynamische Bewertungsskala** — 3 phases, 6 plans, 11 requirements (shipped 2026-03-27) → [archive](milestones/v6.0-ROADMAP.md)

## Current Milestone: 🚧 v7.0 Dashboard-Einstellungen (Phases 19-21)

**Milestone Goal:** Alle Backend-Konfiguration über das Dashboard verwaltbar machen — Frequenz, Headlines, API Keys, manueller Trigger.

## Phases

- [x] **Phase 19: Einstellungs-Persistenz** - DB-Tabelle, Settings-API und Runtime-Reload (completed 2026-03-27)
- [x] **Phase 20: Manueller Analyse-Trigger** - Trigger-Endpoint mit Worker-Integration und Feedback (completed 2026-03-27)
- [ ] **Phase 21: Dashboard Einstellungs-UI** - Einstellungs-Tab mit allen konfigurierbaren Feldern

## Phase Details

### Phase 19: Einstellungs-Persistenz
**Goal**: Das Backend speichert alle Konfigurationsparameter in PostgreSQL und lädt sie beim Start; Änderungen werden sofort ohne Neustart wirksam
**Depends on**: Phase 18 (v6.0)
**Requirements**: CFG-01, CFG-02, CFG-03, API-01
**Success Criteria** (what must be TRUE):
  1. Eine `settings`-Tabelle in PostgreSQL speichert Analyse-Frequenz, Headlines-Anzahl und Anthropic API Key
  2. Beim Container-Start liest das Backend Einstellungen aus der DB; fehlen sie, greift es auf Umgebungsvariablen zurück
  3. Ein PUT-Aufruf auf `/api/moodlight/settings` ändert den laufenden Background Worker ohne Neustart
  4. GET `/api/moodlight/settings` liefert aktuelle Werte als JSON zurück
**Plans**: 3 plans
Plans:
- [x] 19-01-PLAN.md — settings-Tabelle in init.sql + migrate_settings.sql für Produktions-DB
- [x] 19-02-PLAN.md — Database-Methoden (get/set/get_all) + App-Startup auf DB-First umstellen
- [x] 19-03-PLAN.md — Worker reconfigure() + GET/PUT /api/moodlight/settings Endpoints

### Phase 20: Manueller Analyse-Trigger
**Goal**: Benutzer können über das Dashboard eine sofortige Sentiment-Analyse auslösen und erhalten visuelles Feedback über den Fortschritt
**Depends on**: Phase 19
**Requirements**: CTRL-01, CTRL-02, CTRL-03, API-02
**Success Criteria** (what must be TRUE):
  1. POST `/api/moodlight/analyze/trigger` startet eine Analyse synchron und gibt das Ergebnis zurück
  2. Während der Analyse läuft, zeigt das Dashboard einen Lade-Indikator (Button deaktiviert, Spinner sichtbar)
  3. Nach Abschluss der Analyse aktualisiert sich der Sentiment-Bereich des Dashboards automatisch mit dem neuen Score
**Plans**: 2 plans
Plans:
- [x] 20-01-PLAN.md — trigger()-Methode im Worker + POST /api/moodlight/analyze/trigger Endpoint
- [x] 20-02-PLAN.md — Dashboard-Button mit Spinner und Auto-Refresh nach Abschluss

### Phase 21: Dashboard Einstellungs-UI
**Goal**: Das Dashboard enthält einen vollständigen Einstellungs-Tab, über den alle konfigurierbaren Parameter änderbar sind
**Depends on**: Phase 20
**Requirements**: UI-01, UI-02, UI-03, UI-04, UI-05
**Success Criteria** (what must be TRUE):
  1. Ein „Einstellungen"-Tab im Dashboard zeigt Analyse-Frequenz, Headlines pro Quelle, API Key und Admin-Passwort
  2. Analyse-Frequenz und Headlines-Anzahl sind über Eingabefelder änderbar und werden nach Speichern sofort aktiv
  3. Der Anthropic API Key wird maskiert dargestellt und ist nur beim aktiven Editieren als Klartext sichtbar
  4. Das Admin-Passwort ist änderbar; dabei wird das alte Passwort zur Bestätigung abgefragt
**Plans**: TBD
**UI hint**: yes

## Progress

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 19. Einstellungs-Persistenz | v7.0 | 3/3 | Complete    | 2026-03-27 |
| 20. Manueller Analyse-Trigger | v7.0 | 2/2 | Complete   | 2026-03-27 |
| 21. Dashboard Einstellungs-UI | v7.0 | 0/? | Not started | - |
