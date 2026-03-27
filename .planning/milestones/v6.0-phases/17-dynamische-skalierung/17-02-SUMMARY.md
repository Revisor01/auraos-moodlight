---
phase: 17-dynamische-skalierung
plan: 02
subsystem: api
tags: [flask, redis, postgresql, sentiment, python, percentile]

# Dependency graph
requires:
  - phase: 17-01
    provides: "Database.get_score_percentiles(days=7) und compute_led_index(score, thresholds)"
provides:
  - "GET /api/moodlight/current mit raw_score, percentile, led_index, thresholds, historical"
  - "Redis-Cache mit versioniertem Key moodlight:current:v2"
  - "Dynamisches LED-Index-Mapping basierend auf historischen Perzentil-Schwellwerten"
affects:
  - "Phase 18 (ESP32 Firmware — nutzt neue Response-Felder für dynamisches LED-Mapping)"
  - "Dashboard (nutzt thresholds.fallback für Statusanzeige)"

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Versionierter Redis-Cache-Key verhindert Stale-Responses nach Schema-Änderungen"
    - "Lineare Interpolation für Perzentil-Position zwischen historischem Min/Max"
    - "Fallback-Signalisierung via thresholds.fallback im API-Response"

key-files:
  created: []
  modified:
    - "sentiment-api/moodlight_extensions.py"

key-decisions:
  - "CURRENT_CACHE_KEY = 'moodlight:current:v2' — expliziter Versionssprung statt Cache-Flush, verhindert Stale-Format bei alten Clients"
  - "Perzentil als lineare Interpolation (min/max) — einfach, deterministisch, keine zusätzliche DB-Abfrage"
  - "sentiment-Feld bleibt erhalten (Rückwärtskompatibilität) und entspricht raw_score — keine Breaking Change"

patterns-established:
  - "Cache-Key-Versionierung: Bei Response-Schema-Änderungen neuen Key-Suffix vergeben statt Cache-Flush"
  - "Alten Cache-Key im clear-Endpoint zusätzlich löschen für saubere Migration"

requirements-completed:
  - SCALE-03

# Metrics
duration: 3min
completed: 2026-03-27
---

# Phase 17 Plan 02: Dynamisches Scoring im API-Endpoint

**`/api/moodlight/current` liefert jetzt raw_score, led_index via compute_led_index(), percentile via linearer Interpolation, thresholds (p20/p40/p60/p80/fallback) und historical (min/max/median/count) mit Redis-Cache auf Key moodlight:current:v2**

## Performance

- **Duration:** ~3 min
- **Started:** 2026-03-27T09:43:24Z
- **Completed:** 2026-03-27T09:46:00Z
- **Tasks:** 1
- **Files modified:** 1

## Accomplishments

- `compute_led_index()` aus Plan 01 in `moodlight_extensions.py` importiert und aufgerufen
- `get_score_percentiles(days=7)` liefert dynamische Schwellwerte aus 7-Tage-DB-Fenster
- Response um `raw_score`, `led_index`, `percentile`, `thresholds`, `historical` erweitert
- Redis-Cache mit versioniertem Key `moodlight:current:v2` — kein Stale-Format nach Schema-Änderung
- Alten Key `moodlight:current` wird bei clear-Aufruf zusätzlich gelöscht (saubere Migration)
- `sentiment`-Feld bleibt für Rückwärtskompatibilität erhalten

## Task Commits

Jeder Task wurde atomisch committed:

1. **Task 1: /api/moodlight/current um Skalierungs-Kontext erweitern** - `c8052b1` (feat)

**Plan-Metadaten:** Folgt nach Summary-Erstellung

## Files Created/Modified

- `sentiment-api/moodlight_extensions.py` — Import compute_led_index, CURRENT_CACHE_KEY-Konstante, erweiterter Response-Block mit thresholds/historical/percentile/led_index, Cache-Key-Migration in clear_moodlight_cache()

## Decisions Made

- Cache-Key `moodlight:current:v2` statt Cache-Flush: ESP32-Geräte, die den alten Key haben, erhalten beim nächsten Request einen Cache-Miss und bekommen das neue Format — kein Koordinationsproblem
- `sentiment`-Feld (gerundet auf 4 Stellen) bleibt im Response — bestehende Clients, die nur `sentiment` lesen, funktionieren weiter
- Perzentil als lineare Interpolation: `(score - min) / (max - min)` — kein separater DB-Query nötig, da min/max bereits in `thresholds` vorhanden

## Deviations from Plan

Keine — Plan wurde exakt wie beschrieben ausgeführt.

## Issues Encountered

Keine.

## User Setup Required

None — keine externe Konfiguration erforderlich. Deploy wie gewohnt: git push → SSH → git pull → docker-compose build && up -d

## Next Phase Readiness

- `GET /api/moodlight/current` ist bereit für Phase 18 (ESP32-Firmware-Anpassung)
- ESP32 erhält jetzt `led_index` direkt vom Backend — kein statisches Score-Mapping mehr auf dem Gerät nötig
- `thresholds.fallback: true` signalisiert dem ESP32, dass Fallback-Werte genutzt werden (z.B. bei weniger als 3 historischen Datenpunkten)
- Keine offenen Stubs oder Platzhalter

---
*Phase: 17-dynamische-skalierung*
*Completed: 2026-03-27*

## Self-Check: PASSED

- `sentiment-api/moodlight_extensions.py`: FOUND
- `.planning/phases/17-dynamische-skalierung/17-02-SUMMARY.md`: FOUND
- Commit `c8052b1`: FOUND
