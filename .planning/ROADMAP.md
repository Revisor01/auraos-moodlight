# Roadmap: AuraOS Moodlight

## Completed Milestones

- ✅ **v1.0: Stabilisierung** — 2 phases, 6 plans, 13 requirements (shipped 2026-03-25) → [archive](milestones/v1.0-ROADMAP.md)
- ✅ **v2.0: Combined Update + Build Automation** — 3 phases, 4 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v2.0-ROADMAP.md)
- ✅ **v3.0: Firmware-Modularisierung** — 3 phases, 11 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v3.0-ROADMAP.md)
- ✅ **v4.0: Konfigurierbare RSS-Feeds** — 3 phases, 4 plans, 9 requirements (shipped 2026-03-26) → [archive](milestones/v4.0-ROADMAP.md)
- ✅ **v5.0: Schlagzeilen-Transparenz & Dashboard** — 4 phases, 7 plans, 12 requirements (shipped 2026-03-27) → [archive](milestones/v5.0-ROADMAP.md)
- ✅ **v6.0: Dynamische Bewertungsskala** — 3 phases, 6 plans, 11 requirements (shipped 2026-03-27) → [archive](milestones/v6.0-ROADMAP.md)
- ✅ **v7.0: Dashboard-Einstellungen** — 3 phases, 6 plans, 13 requirements (shipped 2026-03-27) → [archive](milestones/v7.0-ROADMAP.md)
- ✅ **v8.0: ESP32 UI-Redesign** — 2 phases, 5 plans, 11 requirements (shipped 2026-03-27) → [archive](milestones/v8.0-ROADMAP.md)

## Current Milestone

**v9.0: Sentiment-Trend pro Feed**

## Phases

- [ ] **Phase 24: Backend-Trendberechnung** — Durchschnitts-Score pro Feed berechnen und per API liefern
- [ ] **Phase 25: Visualisierung & GitHub Page** — Feed-Ranking im Dashboard und auf der GitHub Page darstellen

## Phase Details

### Phase 24: Backend-Trendberechnung
**Goal**: Das Backend kann den durchschnittlichen Sentiment-Score pro Feed für 7-Tage- und 30-Tage-Fenster berechnen und als sortiertes Feed-Ranking per API liefern
**Depends on**: Nichts (Backend-only, bestehende headlines-Tabelle mit feed_id FK)
**Requirements**: TREND-01, TREND-02
**Success Criteria** (what must be TRUE):
  1. Ein GET-Request auf /api/moodlight/feeds/trends liefert eine JSON-Liste aller Feeds sortiert nach ihrem Durchschnitts-Score
  2. Der Endpoint akzeptiert einen days-Parameter (7 oder 30) und gibt korrekte Zeitfenster-Ergebnisse zurück
  3. Feeds ohne Daten im Zeitfenster werden sauber ausgeschlossen oder mit null-Score zurückgegeben
**Plans**: TBD

### Phase 25: Visualisierung & GitHub Page
**Goal**: Nutzer können im Dashboard und auf der GitHub Page sehen, welche Feeds tendenziell positiver oder negativer bewertet werden — als Ranking mit farbkodierten Balken und Zeitfenster-Umschalter
**Depends on**: Phase 24
**Requirements**: VIS-01, VIS-02, VIS-03, PAGE-01
**Success Criteria** (what must be TRUE):
  1. Das Dashboard zeigt eine Tabelle aller Feeds von positivstem bis negativstem Score
  2. Jeder Feed hat einen farbkodierten Balken, der die relative Position im Score-Bereich visualisiert
  3. Ein Umschalter wechselt zwischen 7-Tage- und 30-Tage-Ansicht ohne Seiten-Reload
  4. Die GitHub Page zeigt dieselben Feed-Trenddaten in einer vergleichenden Ansicht
**Plans**: TBD
**UI hint**: yes

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 24. Backend-Trendberechnung | 0/? | Not started | - |
| 25. Visualisierung & GitHub Page | 0/? | Not started | - |
