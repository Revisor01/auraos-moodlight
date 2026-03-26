# Roadmap: AuraOS Moodlight

## Completed Milestones

- ✅ **v1.0: Stabilisierung** — 2 phases, 6 plans, 13 requirements (shipped 2026-03-25) → [archive](milestones/v1.0-ROADMAP.md)
- ✅ **v2.0: Combined Update + Build Automation** — 3 phases, 4 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v2.0-ROADMAP.md)
- ✅ **v3.0: Firmware-Modularisierung** — 3 phases, 11 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v3.0-ROADMAP.md)
- ✅ **v4.0: Konfigurierbare RSS-Feeds** — 3 phases, 4 plans, 9 requirements (shipped 2026-03-26) → [archive](milestones/v4.0-ROADMAP.md)

## Current Milestone: v5.0 Schlagzeilen-Transparenz & Dashboard

## Phases

- [x] **Phase 12: Headline-Persistenz** - DB-Schema + Headline-Speicherung im Backend (completed 2026-03-26)
- [ ] **Phase 13: Authentifizierung** - Einfacher Passwort-Login für Backend-Interface
- [ ] **Phase 14: Backend-Dashboard** - Volles Dashboard mit Headlines, Scores, Feeds und Visualisierung
- [ ] **Phase 15: Client-Erweiterungen** - ESP32 mood.html und GitHub Page mit Headline-Darstellung

## Phase Details

### Phase 12: Headline-Persistenz
**Goal**: Das Backend speichert bei jeder Analyse die einzelnen Headlines mit Einzel-Scores dauerhaft in der Datenbank
**Depends on**: Nichts (Backend-Fundament)
**Requirements**: HEAD-01
**Success Criteria** (what must be TRUE):
  1. Nach einer Sentiment-Analyse sind die einzelnen Headlines mit Einzel-Scores, Feed-Zuordnung und Zeitstempel in der DB abrufbar
  2. Bestehende Analysen laufen weiterhin durch ohne Fehler oder Performance-Einbußen
  3. Die headlines-Tabelle enthält Fremdschlüssel zu feeds und sentiment_updates
**Plans**: 1 plan

Plans:
- [x] 12-01-PLAN.md — headlines-Tabelle (DB-Schema + migration_phase12.sql), save_headlines() in database.py, Background Worker Erweiterung

### Phase 13: Authentifizierung
**Goal**: Das Backend-Interface ist durch einen einfachen Passwort-Login geschützt, öffentliche ESP32-Endpunkte bleiben zugänglich
**Depends on**: Phase 12
**Requirements**: AUTH-01, AUTH-02, AUTH-03
**Success Criteria** (what must be TRUE):
  1. Ein nicht eingeloggter Benutzer wird beim Zugriff auf das Backend-Interface auf eine Login-Seite weitergeleitet
  2. Nach korrekter Passworteingabe kann der Benutzer alle geschützten Seiten aufrufen
  3. POST- und DELETE-Endpunkte geben 401 zurück wenn kein gültiger Session-Cookie vorhanden ist
  4. GET /api/moodlight/current und /api/moodlight/history funktionieren ohne Authentifizierung (ESP32-Zugriff)
**Plans**: 2 plans

Plans:
- [x] 13-01-PLAN.md — Auth-Infrastruktur: login_required Decorator, /login + /logout Routen, login.html Template, docker-compose Umgebungsvariablen
- [ ] 13-02-PLAN.md — Auth anwenden: @login_required auf /feeds, api_login_required auf POST/DELETE Endpoints, Abmelden-Link in feeds.html

### Phase 14: Backend-Dashboard
**Goal**: Benutzer können im Browser alle relevanten Informationen zum System sehen — aktueller Score, Headlines mit Einzel-Scores, Feeds — in einer zusammenhängenden Oberfläche
**Depends on**: Phase 13
**Requirements**: DASH-01, DASH-02, DASH-03, DASH-04, HEAD-02, HEAD-03
**Success Criteria** (what must be TRUE):
  1. Die Übersichtsseite zeigt aktuellen Sentiment-Score, Anzahl aktiver Feeds und den letzten Analyse-Zeitpunkt
  2. Die Headlines-Ansicht listet die letzten analysierten Schlagzeilen mit Einzel-Score, Feed-Name und Zeitstempel
  3. Eine Visualisierung macht sichtbar wie die Einzelwerte zum Gesamt-Score aggregiert werden
  4. Die bestehende Feed-Verwaltung ist über die Dashboard-Navigation erreichbar
  5. Eine Navigation ermöglicht den Wechsel zwischen Übersicht, Headlines und Feeds ohne Seitenreload
**Plans**: TBD
**UI hint**: yes

### Phase 15: Client-Erweiterungen
**Goal**: Sowohl das ESP32 mood.html als auch die GitHub Page zeigen Headlines mit Einzel-Scores an
**Depends on**: Phase 14
**Requirements**: ESP-01, PAGE-01
**Success Criteria** (what must be TRUE):
  1. mood.html auf dem ESP32 lädt beim Öffnen die letzten Headlines vom Backend und zeigt sie mit Einzel-Score und Feed-Zuordnung an
  2. Die GitHub Page listet Headlines mit Einzel-Scores und Feed-Namen neben dem aktuellen Sentiment-Score
  3. Beide Client-Ansichten funktionieren auch wenn der Backend-Endpoint kurzzeitig nicht erreichbar ist (Fallback/Fehlermeldung)
**Plans**: TBD
**UI hint**: yes

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 12. Headline-Persistenz | 1/1 | Complete    | 2026-03-26 |
| 13. Authentifizierung | 1/2 | In Progress|  |
| 14. Backend-Dashboard | 0/? | Not started | - |
| 15. Client-Erweiterungen | 0/? | Not started | - |
