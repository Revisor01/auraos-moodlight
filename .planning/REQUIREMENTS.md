# Requirements: AuraOS Moodlight

**Defined:** 2026-03-27
**Core Value:** Die Firmware ist modular aufgebaut — jedes Modul hat eine klare Verantwortung, ist einzeln lesbar und änderbar, ohne den Rest des Systems zu verstehen.

## v7.0 Requirements

Requirements für Milestone v7.0: Dashboard-Einstellungen.

### Einstellungs-Persistenz

- [x] **CFG-01**: Einstellungen (Frequenz, Headlines-Anzahl, API Keys) werden in PostgreSQL gespeichert
- [x] **CFG-02**: Beim Start liest das Backend Einstellungen aus der DB, Umgebungsvariablen dienen als Fallback
- [x] **CFG-03**: Änderungen an Einstellungen werden sofort wirksam ohne Container-Neustart

### Dashboard-UI

- [x] **UI-01**: Einstellungs-Tab im Dashboard zeigt alle konfigurierbaren Parameter
- [x] **UI-02**: Analyse-Frequenz ist über ein Eingabefeld änderbar (in Minuten)
- [x] **UI-03**: Headlines pro Quelle ist über ein Eingabefeld änderbar
- [x] **UI-04**: Anthropic API Key ist änderbar (maskiert angezeigt, Klartext nur beim Editieren)
- [x] **UI-05**: Admin-Passwort ist änderbar (altes Passwort zur Bestätigung erforderlich)

### Manuelle Steuerung

- [x] **CTRL-01**: Button im Dashboard löst sofortige Sentiment-Analyse aus
- [x] **CTRL-02**: Während der manuellen Analyse zeigt das Dashboard einen Lade-Indikator
- [x] **CTRL-03**: Nach Abschluss der manuellen Analyse aktualisiert sich das Dashboard automatisch

### API-Endpoints

- [x] **API-01**: GET/PUT /api/moodlight/settings für Einstellungs-CRUD
- [x] **API-02**: POST /api/moodlight/analyze/trigger für manuellen Analyse-Start

## Out of Scope

| Feature | Reason |
|---------|--------|
| Mehrere Benutzer-Accounts | Ein Admin reicht |
| Einstellungs-Historie/Audit-Log | Over-Engineering |
| Scheduler (Cron-basiert statt Intervall) | Intervall reicht |
| ESP32 Einstellungen im Backend | ESP32 hat eigenes Web-Interface |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| CFG-01 | Phase 19 | Complete |
| CFG-02 | Phase 19 | Complete |
| CFG-03 | Phase 19 | Complete |
| API-01 | Phase 19 | Complete |
| CTRL-01 | Phase 20 | Complete |
| CTRL-02 | Phase 20 | Complete |
| CTRL-03 | Phase 20 | Complete |
| API-02 | Phase 20 | Complete |
| UI-01 | Phase 21 | Complete |
| UI-02 | Phase 21 | Complete |
| UI-03 | Phase 21 | Complete |
| UI-04 | Phase 21 | Complete |
| UI-05 | Phase 21 | Complete |

**Coverage:**
- v7.0 requirements: 13 total
- Mapped to phases: 13
- Unmapped: 0 ✓

---
*Requirements defined: 2026-03-27*
*Last updated: 2026-03-27 after roadmap creation (v7.0)*
