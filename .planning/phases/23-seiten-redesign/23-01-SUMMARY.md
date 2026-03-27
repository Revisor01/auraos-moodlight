---
phase: 23-seiten-redesign
plan: 01
subsystem: ui
tags: [html, css, esp32, dashboard, score-classes, dark-mode]

# Dependency graph
requires:
  - phase: 22-css-fundament
    provides: style.css mit CSS-Variablen, .card, .score-*, .mood Klassen
provides:
  - firmware/data/index.html mit CSS-Klassen statt Inline-Styles
  - Score-Farbkodierung via .score-neutral/.score-sehr-negativ etc.
  - 4 Karten mit class="card" Layout
affects: [24-seiten-redesign-setup, mood.html, diagnostics.html]

# Tech tracking
tech-stack:
  added: []
  patterns: ["Score-Farbklassen dynamisch via JS gesetzt, HTML-Default: score-neutral"]

key-files:
  created: []
  modified: [firmware/data/index.html]

key-decisions:
  - "style='text-align:center' auf .section bleibt — kein separater CSS-Helper noetig"
  - "Titel von 'Moodlight' auf 'AuraOS Moodlight' aktualisiert"
  - "#mode-text nutzt class='version' statt Inline-Style (gleiches Appearance)"

patterns-established:
  - "Score-Farb-Default in HTML: class='score-neutral', JS aendert dynamisch"
  - "Buttons-Wrapper: class='buttons' statt style='margin-top:16px'"
  - "Icons fa-* in h2-Elemente fuer konsistente Karten-Koepfe"

requirements-completed: [IDX-01, IDX-02, IDX-03]

# Metrics
duration: 5min
completed: 2026-03-27
---

# Phase 23 Plan 01: index.html Redesign Summary

**index.html von Inline-Styles auf CSS-Klassen umgestellt: Score-Farbkodierung (.score-neutral), 4 .card-Karten, Icons in h2, alle JS-Hooks erhalten**

## Performance

- **Duration:** ~5 min
- **Started:** 2026-03-27T12:37:00Z
- **Completed:** 2026-03-27T12:42:00Z
- **Tasks:** 1
- **Files modified:** 1

## Accomplishments

- Alle Inline-Styles entfernt (`font-size`, `color:#`, `margin-top`)
- Score-Anzeige (#mood-value) nutzt `class="stat-value score-neutral"` statt Inline-Style
- `#mood-gauge` hat kein `style="width:50%"` mehr — JS setzt Breite dynamisch
- Alle 4 Karten mit `class="card"`, Button-Wrapper mit `class="buttons"`
- Icons (fa-globe-europe, fa-sliders-h, fa-heartbeat, fa-terminal) in h2-Koepfe eingefuegt
- `#mode-text` nutzt `class="version"` statt `style="font-size:0.8rem;color:#6c757d;margin-left:4px"`

## Task Commits

1. **Task 1: index.html komplett neu schreiben** - `cd74b32` (feat)

## Files Created/Modified

- `firmware/data/index.html` - Hauptseite neu geschrieben: Inline-Styles → CSS-Klassen, Icons, AuraOS-Titel

## Decisions Made

- `style="text-align:center"` auf `.section` behalten — kein Utility-Helper `.text-center` in style.css vorhanden, akzeptabel laut Plan
- `class="version"` fuer `#mode-text` gewaehlt — gleiche Optik wie die Versions-Anzeige im Header (kleine graue Schrift)
- Titel von "Moodlight" auf "AuraOS Moodlight" aktualisiert (entspricht Produktname aus CLAUDE.md)

## Deviations from Plan

Keine — Plan exakt wie spezifiziert ausgefuehrt.

## Issues Encountered

Keine.

## User Setup Required

Keine — reine HTML-Datei, kein externes Setup noetig.

## Next Phase Readiness

- index.html fertig, bereit fuer setup.html (Plan 23-02)
- Alle CSS-Klassen aus style.css korrekt referenziert
- JS-Hooks unveraendert, kein Funktionstest noetig

## Self-Check

- [x] `firmware/data/index.html` existiert und enthaelt score-neutral Klasse
- [x] Commit `cd74b32` vorhanden

---
*Phase: 23-seiten-redesign*
*Completed: 2026-03-27*
