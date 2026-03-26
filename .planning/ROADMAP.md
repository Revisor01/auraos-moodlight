# Roadmap: AuraOS Moodlight

## Completed Milestones

- ✅ **v1.0: Stabilisierung** — 2 phases, 6 plans, 13 requirements (shipped 2026-03-25) → [archive](milestones/v1.0-ROADMAP.md)
- ✅ **v2.0: Combined Update + Build Automation** — 3 phases, 4 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v2.0-ROADMAP.md)
- ✅ **v3.0: Firmware-Modularisierung** — 3 phases, 11 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v3.0-ROADMAP.md)

## Current Milestone

**v4.0: Konfigurierbare RSS-Feeds**

## Phases

- [x] **Phase 9: DB-Schema & Worker-Integration** - Feed-Liste in PostgreSQL, Background Worker liest Feeds aus DB, Focus-Feed entfernt (completed 2026-03-26)
- [ ] **Phase 10: Feed-API** - CRUD-Endpoints für Feed-Verwaltung mit Validierung
- [ ] **Phase 11: Feed-Management Web-Interface** - Setup-Interface mit Feed-Liste, Hinzufügen und Entfernen über Browser

## Phase Details

### Phase 9: DB-Schema & Worker-Integration
**Goal**: Feed-Liste ist in PostgreSQL persistiert und der Background Worker liest Feeds aus der DB statt aus hardcodierten Python-Listen
**Depends on**: Nothing (first phase of milestone)
**Requirements**: FEED-01, FEED-06
**Success Criteria** (what must be TRUE):
  1. Die feeds-Tabelle existiert in PostgreSQL mit Feldern für URL, Name, Aktiv-Status, letzter Fetch-Zeitpunkt und Fehler-Count
  2. Der Background Worker holt die Feed-Liste beim Start und bei jedem Analyse-Zyklus aus der DB (nicht aus shared_config.py oder hardcodierten Listen)
  3. Die 12 validen Standard-Feeds sind als Default-Daten in der DB vorhanden
  4. Focus.de ist nicht in der Default-Feed-Liste enthalten
**Plans**: 2 plans

Plans:
- [x] 09-01-PLAN.md — feeds-Tabelle in init.sql + Migrations-SQL für Produktionssystem
- [x] 09-02-PLAN.md — get_active_feeds() in database.py + Worker/app.py auf DB-Pfad umstellen

### Phase 10: Feed-API
**Goal**: Externe Clients (Browser, curl) können die Feed-Liste lesen sowie Feeds hinzufügen und entfernen
**Depends on**: Phase 9
**Requirements**: FEED-02, FEED-03, FEED-04, FEED-05
**Success Criteria** (what must be TRUE):
  1. GET /api/moodlight/feeds liefert JSON-Array mit allen Feeds inkl. URL, Name, Aktiv-Status, letztem Fetch-Zeitpunkt und Fehler-Count
  2. POST /api/moodlight/feeds mit einer gültigen, erreichbaren Feed-URL legt einen neuen Feed in der DB an und gibt 201 zurück
  3. POST /api/moodlight/feeds mit einer nicht erreichbaren URL gibt einen 4xx-Fehler mit Fehlermeldung zurück (keine stumme Persistierung)
  4. DELETE /api/moodlight/feeds/<id> entfernt den Feed aus der DB und gibt 204 zurück; unbekannte ID gibt 404
**Plans**: TBD

### Phase 11: Feed-Management Web-Interface
**Goal**: Der User kann Feeds im Browser verwalten ohne curl oder direkten DB-Zugriff
**Depends on**: Phase 10
**Requirements**: UI-01, UI-02, UI-03
**Success Criteria** (what must be TRUE):
  1. Eine Seite oder Tab im Backend-Webinterface zeigt die aktuelle Feed-Liste als Tabelle mit URL und Name
  2. User kann über ein Formular eine neue Feed-URL eintragen und abspeichern; Fehler (nicht erreichbar) werden inline angezeigt
  3. User kann einen Feed per Klick (Löschen-Button) entfernen ohne Seiten-Reload
  4. Pro Feed sind letzter Fetch-Zeitpunkt und Fehler-Count sichtbar
**Plans**: TBD
**UI hint**: yes

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 9. DB-Schema & Worker-Integration | 2/2 | Complete   | 2026-03-26 |
| 10. Feed-API | 0/? | Not started | - |
| 11. Feed-Management Web-Interface | 0/? | Not started | - |
