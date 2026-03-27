---
phase: 18-esp32-dashboard-integration
plan: "02"
subsystem: backend-dashboard
tags: [dashboard, visualization, percentile, scaling, VIS-01]
dependency_graph:
  requires: [17-02]
  provides: [VIS-01-dashboard]
  affects: [sentiment-api/templates/dashboard.html]
tech_stack:
  added: []
  patterns: [CSS custom properties, DOM manipulation, fetch API]
key_files:
  created: []
  modified:
    - sentiment-api/templates/dashboard.html
decisions:
  - "Skalierungs-Kontext als eigenständige .scale-section nach score-bar-container — optisch klar getrennt, nicht vermischt"
  - "Schwellwert-Ticks P20/P40/P60/P80 per JS dynamisch erzeugt — sauberer als statisches HTML mit bedingter Sichtbarkeit"
  - "histBarRange positioniert im globalen [-1,+1]-Koordinatensystem — konsistent mit scoreToPercent()"
metrics:
  duration_seconds: 122
  completed_date: "2026-03-27"
  tasks_completed: 2
  tasks_total: 2
  files_changed: 1
---

# Phase 18 Plan 02: Dashboard Skalierungs-Kontext (VIS-01) Summary

**One-liner:** Neuer Skalierungs-Kontext-Block im Dashboard-Übersichts-Tab zeigt Perzentil-Badge, Farbverlauf-Balken mit historischem Min/Max-Fenster, P20/P40/P60/P80-Schwellwert-Ticks und Fallback-Warnung.

## What Was Built

Eine neue `.scale-section` im Dashboard-Übersichts-Tab (nach dem bestehenden Score-Balken-Container) visualisiert alle Skalierungs-Metadaten aus dem `/api/moodlight/current`-Endpunkt:

- **Perzentil-Badge** (`id="percentileVal"`): Zeigt z. B. "47. Pzt." — aktueller Score im Vergleich zur letzten 7-Tage-Historie
- **Beschreibungstext** (`id="percentileDesc"`): "Der aktuelle Score liegt über X % der Werte der letzten 7 Tage."
- **Historischer Farbverlauf-Balken** (`id="histBarRange"`): Linke/rechte Kante = Min/Max der letzten 7 Tage, positioniert im globalen [-1,+1]-Koordinatensystem
- **Score-Nadel** (`id="histNeedle"`): Zeigt aktuellen Score innerhalb des Balkens
- **P20/P40/P60/P80-Ticks**: Werden per JavaScript als `.threshold-tick` + `.threshold-label` dynamisch in den Balken eingefügt
- **Min/Median/Max-Werte** (`id="histMin"`, `id="histMedian"`, `id="histMax"`): Numerisch als Monospace-Werte
- **Datenpunkt-Zähler** (`id="histCount"`): "N Datenpunkte in den letzten 7 Tagen"
- **Fallback-Hinweis** (`id="fallbackHint"`): Gelber Warnblock erscheint bei `thresholds.fallback === true`

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | CSS-Stile für Skalierungs-Sektion | 8ea08a3 | dashboard.html (+125 Zeilen CSS) |
| 2 | HTML-Sektion + JavaScript | 095644d | dashboard.html (+90 Zeilen HTML/JS) |

## Deviations from Plan

None — Plan exakt wie beschrieben ausgeführt.

## Known Stubs

None — alle IDs werden durch `loadOverview()` mit echten API-Daten befüllt. Der Block rendert nur wenn `current.percentile !== undefined && current.thresholds && current.historical` zutrifft (kein leerer State sichtbar).

## Self-Check: PASSED

- `sentiment-api/templates/dashboard.html` — FOUND (modifiziert)
- Commit 8ea08a3 — FOUND
- Commit 095644d — FOUND
- Alle 10 IDs vorhanden (scaleSection, percentileVal, percentileDesc, histBarWrap, histBarRange, histNeedle, histMin, histMedian, histMax, fallbackHint)
- `grep -c "percentile"` = 21 — PASSED
