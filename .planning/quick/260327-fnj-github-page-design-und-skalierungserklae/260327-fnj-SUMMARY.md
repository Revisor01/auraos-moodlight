---
phase: 260327-fnj
plan: 01
type: quick
subsystem: docs
tags: [github-page, sentiment, scaling, ux, api-v2]
dependency_graph:
  requires: [Phase 17 — /api/moodlight/current mit percentile/thresholds/historical/led_index]
  provides: [GitHub Page mit erweiterter Skalierungs-Darstellung]
  affects: [docs/index.html]
tech_stack:
  added: []
  patterns: [LED-Index-basiertes Theme-Switching, renderScaleSection(), hist-bar-CSS aus dashboard.html]
key_files:
  modified:
    - docs/index.html
decisions:
  - API-Endpoint von /api/news/total_sentiment auf /api/moodlight/current umgestellt — vollständiges Response-Objekt statt einzelnem Wert
  - updateTheme() per led_index statt fester Score-Grenzen — konsistent mit Backend-Logik
  - renderScaleSection() aufgerufen aus updateRealSentiment() — Skalierungs-Sektion aktualisiert bei jedem API-Poll
metrics:
  duration: ~25min
  completed: 2026-03-26
  tasks_completed: 2
  files_modified: 1
---

# Quick Task 260327-fnj: GitHub Page Design und Skalierungserklärung

**One-liner:** GitHub Page auf /api/moodlight/current umgestellt mit Hero-Erweiterung (led_index, category, percentile) und neuer Skalierungs-Sektion (Score-Berechnung + dynamisches 7-Tage-Fenster mit P20–P80 Schwellwerten).

## Tasks Completed

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | API-Endpoint umstellen und neue Felder einbinden | 7a1d4f7 | docs/index.html |
| 2 | Stat-Karten Design-Angleichung + Skalierungs-Sektion | 46a82d0 | docs/index.html |

## What Was Built

### Task 1: API-Endpoint und Hero-Erweiterung
- `fetchRealSentiment()` ruft jetzt `/api/moodlight/current` auf und gibt das vollständige Objekt zurück
- `updateRealSentiment()` wertet `category`, `led_index`, `percentile` aus dem Response aus
- Hero-Bereich: LED-Farb-Indikator (Kreis) in `LED_COLORS[led_index]` gefärbt
- Kategorie aus API direkt übernommen (kein lokales Mapping mehr)
- Neues `#hero-sentiment-percentile`-Element zeigt "P{N} im 7-Tage-Bereich"
- `updateTheme()` per `led_index` (0–4) statt fester Score-Schwellwerte

### Task 2: Design-Angleichung und Skalierungs-Sektion
- `.stat-card` kompakter: `border-radius 8px`, `padding 1.25rem 1.5rem`, `box-shadow 0 2px 8px`
- `.stat-label`: `font-size 0.78rem`, `uppercase`, `letter-spacing 0.06em`, `font-weight 600`
- `.stat-value`: `font-size 2rem`, `font-weight 700`, `line-height 1.2`
- Score-Farbklassen `.score-sehr-negativ` bis `.score-sehr-positiv` hinzugefügt
- Vollständiges CSS für Skalierungs-Sektion: `.scale-section`, `.hist-bar-*`, `.threshold-tick`, `.percentile-badge`, `.formula-row`, `.bar-track`, `.bar-needle`
- Neue `#scaling`-Sektion mit zwei Cards:
  - Score-Berechnungs-Balken mit tanh-Formel-Zeile (sfRaw, sfStretched, sfFinal)
  - Historischer Bereich: hist-bar mit farbigem Verlauf, dynamische Schwellwert-Ticks P20/P40/P60/P80, Min/Median/Max-Anzeige, Perzentil-Badge, Fallback-Hinweis
  - LED-Schritt-Karten (5 Stück) mit aktivem Highlight per `led_index`
- `renderScaleSection(data)` Funktion verarbeitet vollständiges API-Response

## Deviations from Plan

None — Plan executed exactly as written.

## Known Stubs

None — alle Felder werden aus echten API-Daten befüllt. Bei API-Fehler bleibt die Skalierungs-Sektion mit Dash-Werten (—) stehen.

## Self-Check: PASSED

- docs/index.html: FOUND (modifiziert)
- Commit 7a1d4f7: FOUND
- Commit 46a82d0: FOUND
- #scaling section: FOUND in HTML
- renderScaleSection function: FOUND in JS
- fetchRealSentiment() targeting /api/moodlight/current: FOUND
- LED_COLORS array: FOUND
- hero-sentiment-percentile element: FOUND
