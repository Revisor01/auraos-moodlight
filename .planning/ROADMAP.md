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
- ✅ **v9.0: Sentiment-Trend pro Feed** — 2 phases, 3 plans, 6 requirements (shipped 2026-03-27) → [archive](milestones/v9.0-ROADMAP.md)

## Current Milestone: v10.0 Perzentil-Transparenz & Firmware-Stabilität

**Goal:** Die Perzentil-basierte LED-Zuordnung wird auf ESP32 mood.html und GitHub Page sichtbar und verständlich — identisch zum Backend-Dashboard. Firmware-Stabilitätsfixes werden committed und deployed.

**Requirements:** 11 (5 FW + 6 PZ) → see [REQUIREMENTS.md](REQUIREMENTS.md)

### Phase Overview

| Phase | Name | Reqs | Status |
|-------|------|------|--------|
| 26 | firmware-stabilitaet | FW-01..FW-05 | **done** |
| 27 | perzentil-mood-html | PZ-01..PZ-03, PZ-05, PZ-06 | **done** |
| 28 | perzentil-github-page | PZ-04, PZ-06 | **done** |

### Phase 26: firmware-stabilitaet
**Goal:** Alle Firmware-Stabilitätsfixes aus der Debug-Session sind committed und auf dem Gerät deployed.
**Requirements:** FW-01, FW-02, FW-03, FW-04, FW-05
**Depends on:** —
**Estimated plans:** 1

### Phase 27: perzentil-mood-html
**Goal:** ESP32 mood.html zeigt den Skalierungs-Kontext (Perzentil-Badge, historischer Bereich-Balken mit Schwellwerten, Min/Median/Max, erklärenden Text) — übernommen vom Backend-Dashboard-Design.
**Requirements:** PZ-01, PZ-02, PZ-03, PZ-05, PZ-06
**Depends on:** —
**Estimated plans:** 1

### Phase 28: perzentil-github-page
**Goal:** GitHub Page (docs/index.html) zeigt identische Perzentil-Visualisierung wie mood.html.
**Requirements:** PZ-04, PZ-06
**Depends on:** Phase 27 (CSS/JS-Vorlage)
**Estimated plans:** 1
