# Requirements: AuraOS Moodlight

**Defined:** 2026-03-26
**Core Value:** Die Firmware ist modular aufgebaut — jedes Modul hat eine klare Verantwortung, ist einzeln lesbar und änderbar, ohne den Rest des Systems zu verstehen.

## v4.0 Requirements

Requirements für Milestone v4.0: Konfigurierbare RSS-Feeds.

### Feed-Management

- [ ] **FEED-01**: Feed-Liste in PostgreSQL persistieren statt hardcoded in Python-Dateien
- [ ] **FEED-02**: GET /api/moodlight/feeds liefert aktuelle Feed-Liste mit Status
- [ ] **FEED-03**: POST /api/moodlight/feeds fügt neuen Feed hinzu
- [ ] **FEED-04**: DELETE /api/moodlight/feeds/<id> entfernt Feed
- [ ] **FEED-05**: Feed-URL wird beim Hinzufügen auf Erreichbarkeit validiert
- [ ] **FEED-06**: Focus.de Feed (404) aus Default-Liste entfernen

### Web-Interface

- [ ] **UI-01**: setup.html zeigt Feed-Management-Sektion mit aktueller Feed-Liste
- [ ] **UI-02**: User kann Feed über Web-Interface hinzufügen und entfernen
- [ ] **UI-03**: Feed-Status (letzter Fetch, Fehler-Count) ist pro Feed sichtbar

## Future Requirements

### Schlagzeilen-Transparenz (v5.0)

- **TRANS-01**: Headlines + Einzel-Scores in DB speichern
- **TRANS-02**: API-Endpoint für aktuelle Headlines mit Einzel-Scores
- **TRANS-03**: ESP32 mood.html zeigt letzte Headlines mit Scores
- **TRANS-04**: GitHub Page erweitern mit Headline-Darstellung

## Out of Scope

| Feature | Reason |
|---------|--------|
| ESP32 Firmware-Änderungen | Feeds sind Backend-only, kein Firmware-Change nötig |
| Feed-Import/Export | Kein Bedarf bei ~12 Feeds |
| Feed-Kategorien/Tags | Over-Engineering für privates Projekt |
| Automatische Feed-Discovery | Manuelle Verwaltung reicht |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| FEED-01 | — | Pending |
| FEED-02 | — | Pending |
| FEED-03 | — | Pending |
| FEED-04 | — | Pending |
| FEED-05 | — | Pending |
| FEED-06 | — | Pending |
| UI-01 | — | Pending |
| UI-02 | — | Pending |
| UI-03 | — | Pending |

**Coverage:**
- v4.0 requirements: 9 total
- Mapped to phases: 0
- Unmapped: 9 ⚠️

---
*Requirements defined: 2026-03-26*
*Last updated: 2026-03-26 after initial definition*
