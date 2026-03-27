# Requirements: AuraOS Moodlight

**Defined:** 2026-03-27
**Core Value:** Die Firmware ist modular aufgebaut — jedes Modul hat eine klare Verantwortung, ist einzeln lesbar und änderbar, ohne den Rest des Systems zu verstehen.

## v9.0 Requirements

Requirements für Milestone v9.0: Sentiment-Trend pro Feed.

### Backend-Analyse

- [x] **TREND-01**: Backend berechnet Durchschnitts-Score pro Feed für konfigurierbare Zeitfenster (7 Tage, 30 Tage)
- [x] **TREND-02**: API-Endpoint GET /api/moodlight/feeds/trends liefert Feed-Rankings sortiert nach durchschnittlichem Score

### Dashboard-Visualisierung

- [ ] **VIS-01**: Dashboard zeigt Feed-Ranking-Tabelle (positivster bis negativster Feed)
- [ ] **VIS-02**: Farbkodierte Score-Balken pro Feed zeigen relative Position
- [ ] **VIS-03**: Zeitfenster-Umschalter (7 Tage / 30 Tage) im Dashboard

### GitHub Page

- [ ] **PAGE-01**: GitHub Page zeigt Feed-Vergleichs-Ansicht mit Trend-Daten

## Out of Scope

| Feature | Reason |
|---------|--------|
| Feed-Score-Verlauf als Chart (Linie pro Feed) | Zu komplex für v9.0, evtl. v10.0 |
| ESP32 mood.html Feed-Trends | Zu viel Daten für das kleine Display |
| Automatische Feed-Bewertung (schlechte Feeds deaktivieren) | Manuelle Kontrolle reicht |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| TREND-01 | Phase 24 | Complete |
| TREND-02 | Phase 24 | Complete |
| VIS-01 | Phase 25 | Pending |
| VIS-02 | Phase 25 | Pending |
| VIS-03 | Phase 25 | Pending |
| PAGE-01 | Phase 25 | Pending |

**Coverage:**
- v9.0 requirements: 6 total
- Mapped to phases: 6
- Unmapped: 0 ✓

---
*Requirements defined: 2026-03-27*
*Last updated: 2026-03-27 after roadmap definition (v9.0)*
