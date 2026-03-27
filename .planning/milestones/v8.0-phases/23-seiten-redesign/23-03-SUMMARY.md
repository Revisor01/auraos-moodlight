---
phase: 23-seiten-redesign
plan: "03"
subsystem: firmware/web-interface
tags: [mood.html, css-klassen, headlines, sentiment, inline-style-cleanup]
dependency_graph:
  requires: [22-01-SUMMARY.md]
  provides: [MOOD-01, MOOD-02]
  affects: [firmware/data/mood.html]
tech_stack:
  added: []
  patterns: [score-css-classes, css-variables-dark-mode]
key_files:
  created: []
  modified:
    - firmware/data/mood.html
decisions:
  - "scoreClass() gibt CSS-Klassenstrings zurück statt Hex-Farben — Dark-Mode-kompatibel durch style.css"
  - "headline-* Hilfsklassen mit CSS-Variablen im <head> definiert statt in mood.css/style.css — minimaler LittleFS-Footprint"
metrics:
  duration_minutes: 5
  completed_date: "2026-03-27"
  tasks_completed: 1
  tasks_total: 1
  files_changed: 1
requirements:
  - MOOD-01
  - MOOD-02
---

# Phase 23 Plan 03: mood.html Headlines — scoreClass() und CSS-Klassen Summary

**One-liner:** Inline-Farb-Styles in renderHeadlines() durch score-CSS-Klassen ersetzt; headline-* Hilfsklassen mit CSS-Variablen für Dark-Mode-Kompatibilität ergänzt.

## Was wurde gebaut

mood.html wurde komplett neu geschrieben. Die zentrale Änderung betrifft die Headlines-Darstellung im Inline-Script:

- `scoreColor()` (gab Hex-Farben zurück) durch `scoreClass()` ersetzt (gibt CSS-Klassenstrings zurück)
- `renderHeadlines()` nutzt jetzt `.headline-item`, `.headline-score`, `.headline-text`, `.headline-feed` Klassen statt `style="..."` Attributen
- Die score-Farbklassen (`score-sehr-negativ`, `score-negativ`, `score-neutral`, `score-positiv`, `score-sehr-positiv`) stammen aus `style.css`
- Ein `<style>`-Block im `<head>` definiert die vier `.headline-*` Hilfsklassen mit CSS-Variablen (`var(--border)`, `var(--text-muted)`) — dadurch funktioniert der Dark Mode korrekt ohne Extra-CSS-Datei
- `style="margin-top: 12px;"` auf `#headlines-list` entfernt

Der gesamte Chart-Code, alle IDs (`#all-chart`, `#week-chart`, `#hourly-chart`, `#weekday-chart`, `#distribution-chart`, `#trend-chart`, `#avg-value`, `#min-value`, `#max-value`, `#count-value`, `#filtered-badge`, `#summary-text`, `#headlines-list`, `#headlines-section`, `#loading-message` etc.) und die Statistik-Grid-Struktur (`.stat-card`, `.stats-grid`) bleiben unverändert erhalten.

## Tasks

| Task | Name | Commit | Dateien |
|------|------|--------|---------|
| 1 | mood.html neu schreiben — score-Klassen, headline CSS-Klassen | 52eef9f | firmware/data/mood.html |

## Verification

```
grep -c "scoreClass\|score-sehr-negativ\|score-neutral" firmware/data/mood.html
# => 4  (scoreClass-Funktion + score-sehr-negativ-Klasse + score-neutral-Klasse x2)

grep -c "scoreColor\|style=\"color:" firmware/data/mood.html
# => 0  (vollständig entfernt)
```

## Deviations from Plan

Keine — Plan wurde exakt wie beschrieben umgesetzt.

## Known Stubs

Keine — renderHeadlines() lädt echte Daten von `https://analyse.godsapp.de/api/moodlight/headlines?limit=20`.

## Self-Check: PASSED

- firmware/data/mood.html: vorhanden
- Commit 52eef9f: vorhanden
- scoreColor: 0 Treffer
- scoreClass: vorhanden
- score-sehr-negativ: vorhanden
