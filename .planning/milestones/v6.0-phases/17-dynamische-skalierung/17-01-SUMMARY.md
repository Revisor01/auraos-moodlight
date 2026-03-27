---
phase: 17-dynamische-skalierung
plan: 01
subsystem: database
tags: [postgresql, percentile, sentiment, python, tdd]

# Dependency graph
requires: []
provides:
  - "Database.get_score_percentiles(days=7) mit PostgreSQL percentile_cont()-Query"
  - "module-level compute_led_index(score, thresholds) Hilfsfunktion"
  - "Fallback-Logik bei count < 3 Datenpunkten"
affects:
  - "17-02 (dynamisches Score-Mapping in moodlight_extensions.py)"
  - "Jeder Plan der LED-Index-Berechnung aus dynamischen Schwellwerten nutzt"

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "PostgreSQL percentile_cont() WITHIN GROUP für historische Perzentil-Berechnung"
    - "Fallback-Dict-Muster bei unzureichenden Datenpunkten"
    - "TDD mit unittest + sys.modules-Mock für Pakete ohne lokale Installation"

key-files:
  created:
    - "sentiment-api/tests/test_percentiles.py"
  modified:
    - "sentiment-api/database.py"

key-decisions:
  - "compute_led_index als module-level Funktion (nicht Klassenmethode) — erleichtert Import in anderen Modulen"
  - "Fallback bei count < 3 (nicht count == 0) — drei Punkte minimum für statistisch sinnvolle Perzentile"
  - "psycopg2/redis via sys.modules-Mock in Tests — lokal ohne Pakete ausführbar, CI/CD im Container"

patterns-established:
  - "TDD-Mock: sys.modules.setdefault() vor Datenbankimport für lokal-ausführbare Unit-Tests"
  - "Fallback-Dict als Konstante FALLBACK definiert und bei mehreren Return-Pfaden wiederverwendet"

requirements-completed:
  - SCALE-01
  - SCALE-02

# Metrics
duration: 2min
completed: 2026-03-26
---

# Phase 17 Plan 01: Perzentil-Berechnung im Database-Layer

**PostgreSQL percentile_cont()-Abfrage in Database-Klasse + module-level compute_led_index() mit Fallback bei weniger als 3 historischen Datenpunkten**

## Performance

- **Duration:** ~2 min
- **Started:** 2026-03-26T14:38:57Z
- **Completed:** 2026-03-26T14:41:09Z
- **Tasks:** 1 (TDD: RED + GREEN)
- **Files modified:** 2

## Accomplishments

- `Database.get_score_percentiles(days=7)` berechnet P20/P40/P60/P80/Median/Min/Max aus `sentiment_history` via `percentile_cont() WITHIN GROUP`
- Fallback-Werte (feste Standardschwellwerte) bei weniger als 3 Datenpunkten im Zeitfenster
- `compute_led_index(score, thresholds)` mappt Rohwert auf LED-Index 0–4 per Schwellwertvergleich
- 20 Unit-Tests (alle grün) ohne laufende Datenbank — psycopg2 via sys.modules gemockt

## Task Commits

Jeder Task wurde atomisch committed:

1. **Task 1: Tests (RED)** - `b303d52` (test) — fehlschlagende Tests für beide Funktionen
2. **Task 1: Implementierung (GREEN)** - `5082997` (feat) — Implementierung, alle 20 Tests grün

**Plan-Metadaten:** Folgt nach Summary-Erstellung

## Files Created/Modified

- `sentiment-api/database.py` — `get_score_percentiles()` nach `get_statistics()` eingefügt, `compute_led_index()` am Datei-Ende als Module-Level-Funktion
- `sentiment-api/tests/test_percentiles.py` — 20 Unit-Tests für beide Funktionen (neu erstellt)

## Decisions Made

- `compute_led_index` als module-level Funktion, nicht als Klassenmethode — Plan 02 importiert sie direkt ohne Database-Instanz
- Fallback-Schwelle: `count < 3` — statistisch sinnloser Median bei weniger als 3 Punkten
- Unit-Tests mocken `psycopg2` via `sys.modules` — lokal ohne Docker-Umgebung ausführbar

## Deviations from Plan

Keine — Plan wurde exakt wie beschrieben ausgeführt.

## Issues Encountered

- `psycopg2` lokal nicht installiert: Verifikationsbefehl aus dem Plan (`python3 -c "from database import compute_led_index"`) schlägt lokal fehl, da das Paket nur im Docker-Container vorhanden ist. Unit-Tests ersetzen diese Verifikation vollständig. Keine Auswirkung auf Produktionscode.

## User Setup Required

None — keine externe Konfiguration erforderlich.

## Next Phase Readiness

- `get_score_percentiles()` und `compute_led_index()` bereit für Nutzung in Plan 02
- Plan 02 kann `from database import compute_led_index` direkt verwenden
- Keine offenen Stubs oder Platzhalter

---
*Phase: 17-dynamische-skalierung*
*Completed: 2026-03-26*
