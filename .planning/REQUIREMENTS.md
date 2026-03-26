# Requirements: AuraOS Moodlight

**Defined:** 2026-03-26
**Core Value:** Die Firmware ist modular aufgebaut — jedes Modul hat eine klare Verantwortung, ist einzeln lesbar und änderbar, ohne den Rest des Systems zu verstehen.

## v5.0 Requirements

Requirements für Milestone v5.0: Schlagzeilen-Transparenz & Dashboard.

### Headline-Transparenz

- [x] **HEAD-01**: Backend speichert bei jeder Analyse die einzelnen Headlines mit ihren Einzel-Scores in der DB
- [x] **HEAD-02**: API-Endpoint liefert die letzten analysierten Headlines mit Einzel-Scores, Feed-Zuordnung und Zeitstempel
- [x] **HEAD-03**: Visualisierung zeigt wie der Gesamt-Score aus den Einzelwerten berechnet wird

### Backend-Dashboard

- [x] **DASH-01**: Übersichtsseite zeigt aktuellen Sentiment-Score, Anzahl Feeds, letzten Analyse-Zeitpunkt
- [x] **DASH-02**: Headlines-Ansicht zeigt letzte analysierte Schlagzeilen mit Einzel-Scores und Feed-Zuordnung
- [x] **DASH-03**: Feed-Verwaltung ist ins Dashboard integriert (bestehende /feeds Funktionalität)
- [x] **DASH-04**: Navigation zwischen Dashboard-Bereichen (Übersicht, Headlines, Feeds)

### Authentifizierung

- [x] **AUTH-01**: Backend-Interface ist durch einfachen Passwort-Login geschützt
- [x] **AUTH-02**: API-Endpoints für Schreiboperationen (POST/DELETE) erfordern Authentifizierung
- [x] **AUTH-03**: Lese-Endpoints (GET /api/moodlight/current, /history) bleiben öffentlich (ESP32 braucht sie)

### ESP32 Integration

- [x] **ESP-01**: mood.html auf dem ESP32 zeigt die letzten Headlines mit Einzel-Scores (fetch vom Backend)

### GitHub Page

- [ ] **PAGE-01**: GitHub Page zeigt Headline-Darstellung mit Einzel-Scores und Feed-Zuordnung

## Future Requirements

### Erweiterte Analyse (v6.0+)

- **ANAL-01**: Sentiment-Trend pro Feed (welcher Feed ist tendenziell positiver/negativer)
- **ANAL-02**: Historische Headline-Suche

## Out of Scope

| Feature | Reason |
|---------|--------|
| OAuth/Authentik-Integration | Over-Engineering für privates Projekt, einfacher Passwort-Schutz reicht |
| Benutzer-Verwaltung (mehrere Accounts) | Ein User, ein Passwort |
| ESP32 Firmware-Änderungen außer mood.html | Feeds sind Backend-Concern |
| Echtzeit-Updates (WebSocket) | Polling alle 30 Minuten reicht |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| HEAD-01 | Phase 12 | Complete |
| HEAD-02 | Phase 14 | Complete |
| HEAD-03 | Phase 14 | Complete |
| DASH-01 | Phase 14 | Complete |
| DASH-02 | Phase 14 | Complete |
| DASH-03 | Phase 14 | Complete |
| DASH-04 | Phase 14 | Complete |
| AUTH-01 | Phase 13 | Complete |
| AUTH-02 | Phase 13 | Complete |
| AUTH-03 | Phase 13 | Complete |
| ESP-01 | Phase 15 | Complete |
| PAGE-01 | Phase 15 | Pending |

**Coverage:**
- v5.0 requirements: 12 total
- Mapped to phases: 12
- Unmapped: 0 ✓

---
*Requirements defined: 2026-03-26*
*Last updated: 2026-03-26 after v5.0 roadmap creation (all 12 requirements mapped)*
