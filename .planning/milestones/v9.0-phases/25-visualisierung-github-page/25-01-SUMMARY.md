---
phase: 25-visualisierung-github-page
plan: 01
subsystem: backend-frontend
tags: [dashboard, frontend, visualization, feed-trends]
dependency_graph:
  requires: [24-01]
  provides: [tab-trends, feed-ranking-ui, zeitfenster-umschalter]
  affects: [sentiment-api/templates/dashboard.html]
tech_stack:
  added: []
  patterns: [lazy-tab-loading, score-bar-visualization, no-reload-filter]
key_files:
  created: []
  modified:
    - sentiment-api/templates/dashboard.html
decisions:
  - "Farbkodierung über scoreToBarColor() mit 5 diskreten Schwellwerten statt CSS-Gradient — einfacher wartbar"
  - "trendsLoaded-Flag verhindert doppeltes Laden beim Tab-Wechsel — konsistent mit bestehenden Tabs"
  - "switchWindow() setzt trendsLoaded=false und ruft loadFeedTrends() direkt auf — kein Seiten-Reload nötig"
metrics:
  duration: "ca. 5 Minuten"
  completed: "2026-03-26"
  tasks_completed: 2
  tasks_total: 2
  files_changed: 1
requirements_satisfied: [VIS-01, VIS-02, VIS-03]
---

# Phase 25 Plan 01: Feed-Trends Dashboard-Tab Summary

**One-liner:** Tab "Feed-Trends" mit sortierter Ranking-Tabelle, farbkodierten Score-Balken und Zeitfenster-Umschalter (7/30 Tage ohne Seiten-Reload) in dashboard.html integriert.

## Was wurde gebaut

Tab "Feed-Trends" im AuraOS-Dashboard: eine sortierte Tabelle aller RSS-Feeds nach Durchschnitts-Score (positivster zuoberst), mit einem farbkodierten Balken pro Feed (rot bei negativem, grün bei positivem Score) und einem Toggle-Button-Paar zum Wechsel zwischen 7- und 30-Tage-Zeitfenster. Daten kommen aus dem in Phase 24 implementierten Endpunkt `/api/moodlight/feeds/trends?days=N`.

## Tasks

| # | Name | Status | Commit |
|---|------|--------|--------|
| 1 | CSS für Feed-Trends-Tab ergänzen | Abgeschlossen | f347e40 |
| 2 | HTML-Tab und JavaScript einbauen | Abgeschlossen | e89649b |

## Technische Details

### CSS (Task 1)
- `.feed-rank-section` — weisse Card-Sektion mit Schatten, analog zu anderen Dashboard-Sektionen
- `.feed-rank-table` mit `.rank-nr`, `.rank-name`, `.rank-bar-cell`, `.rank-score`, `.rank-count` — Tabellenlayout
- `.rank-bar-track` / `.rank-bar-fill` — horizontaler Fortschrittsbalken, Score [-1,+1] auf Breite 0-100% gemappt
- `.window-toggle` / `.window-btn` — Segmented-Control-Muster für Zeitfenster-Auswahl
- Responsive: `.rank-bar-cell` und `.rank-count` auf Geräten ≤600px ausgeblendet

### JavaScript (Task 2)
- `scoreToBarColor(score)`: 5 diskrete Schwellwerte (-0.30, -0.10, +0.10, +0.30) → Rot/Orange/Blau/Lila/Grün
- `scoreToBarWidth(score)`: `(score + 1) / 2 * 100` → prozentuale Balkenbreite
- `loadFeedTrends(days)`: async fetch von `/api/moodlight/feeds/trends?days=N`, rendert Tabelle oder Fehlermeldung
- `switchWindow(days)`: setzt `trendsLoaded=false`, aktualisiert Button-Status, ruft `loadFeedTrends()` auf
- `showTab()`-Dispatcher: um `if (tabId === 'trends' && !trendsLoaded) loadFeedTrends(currentTrendDays)` erweitert
- Lazy-Loading: Tab-Inhalt wird erst beim ersten Klick auf den Tab geladen

## Deviations from Plan

None — Plan wurde exakt wie spezifiziert umgesetzt.

## Known Stubs

None — alle Daten kommen aus dem echten `/api/moodlight/feeds/trends`-Endpunkt (Phase 24).

## Self-Check

- [x] `data-tab="trends"` Tab-Button vorhanden
- [x] `id="tab-trends"` Tab-Pane vorhanden
- [x] `loadFeedTrends` und `switchWindow` im Script vorhanden (6 Treffer)
- [x] `feeds/trends` API-Endpoint referenziert
- [x] Commits f347e40 und e89649b existieren
