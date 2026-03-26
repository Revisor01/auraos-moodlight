---
phase: 15-client-erweiterungen
plan: "02"
subsystem: docs/index.html (GitHub Page)
tags: [frontend, github-page, sentiment, headlines, PAGE-01]
dependency_graph:
  requires: [14-backend-dashboard-02]
  provides: [headlines-display-docs]
  affects: [docs/index.html]
tech_stack:
  added: []
  patterns: [fetch-api, iife-module-pattern, css-custom-properties]
key_files:
  created: []
  modified:
    - docs/index.html
decisions:
  - "Vanilla JS IIFE statt ES6-Modul — maximale Browser-Kompatibilität ohne Build-Step"
  - "scoreToColor() spiegelt exakt die bestehenden mood-Farben der Seite (-0.4/0/+0.4 Schwellwerte)"
metrics:
  duration: "~5 min"
  completed: "2026-03-26T23:55:32Z"
  tasks_completed: 2
  files_modified: 1
requirements_met:
  - PAGE-01
---

# Phase 15 Plan 02: Headlines-Sektion in GitHub Page Summary

**One-liner:** Neue "Aktuelle Schlagzeilen"-Sektion in docs/index.html mit fetch() gegen api/moodlight/headlines, farbkodierten Einzel-Scores und Feed-Namen, Fallback bei Backend-Ausfall.

## Objective

docs/index.html (GitHub Page) erhält eine neue Headlines-Sektion zwischen #statistics und #screenshots, die die letzten 20 analysierten Headlines mit farbkodierten Einzel-Scores und Feed-Namen anzeigt.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | CSS für Headlines-Sektion inline ergänzen | c0470c2 | docs/index.html |
| 2 | Headlines-Sektion HTML und JavaScript einfügen | 703ed96 | docs/index.html |

## Implementation Details

### CSS (Task 1)
Sechs neue CSS-Klassen im bestehenden `<style>`-Block:
- `.headlines-list` — reset list-style
- `.headline-item` — flex layout mit gap=12px, border-bottom
- `.headline-score` — bold, min-width 46px, flex-shrink:0
- `.headline-text` — flex:1, text-primary Farbe
- `.headline-feed` — klein, text-secondary, white-space:nowrap
- `.headlines-error` — zentriert, text-secondary, padding

### HTML (Task 2)
Sektion `id="headlines"` zwischen Zeile 726 (#statistics) und Zeile 834 (#screenshots).
Enthält `<ul id="headlines-list">` als Einfügepunkt für dynamisch gerenderte Items.

### JavaScript (Task 2)
IIFE vor `</body>`, ruft `https://analyse.godsapp.de/api/moodlight/headlines?limit=20` auf.

Farbkodierung konsistent mit bestehenden mood-Farben:
- score ≤ -0.4 → `#dc3545` (sehr negativ / rot)
- score < 0 → `#fd7e14` (negativ / orange)
- score === 0 → `#17a2b8` (neutral / cyan)
- score < 0.4 → `#6f42c1` (positiv / indigo)
- score ≥ 0.4 → `#8A2BE2` (sehr positiv / violett)

Positive Scores werden mit `+` Präfix formatiert. Fallback: "Backend nicht erreichbar — Schlagzeilen nicht verfügbar."

## Verification

```
api/moodlight/headlines: 1 occurrence
id="headlines": 1 occurrence
headlines-list: 5 occurrences (HTML + JS references)
Backend nicht erreichbar: 1 occurrence
Section order: #statistics (726) → #headlines (818) → #screenshots (834)
```

## Deviations from Plan

None — plan executed exactly as written.

## Self-Check: PASSED

- docs/index.html modified: confirmed (wc -l shows 1653 lines after additions)
- commit c0470c2: confirmed
- commit 703ed96: confirmed
- id="headlines" between statistics and screenshots: confirmed (lines 726, 818, 834)
