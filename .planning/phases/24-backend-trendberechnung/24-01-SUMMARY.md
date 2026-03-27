---
phase: 24-backend-trendberechnung
plan: 01
subsystem: api
tags: [postgres, flask, sentiment, feed-ranking, rss]

# Dependency graph
requires:
  - phase: 23-feed-verwaltung
    provides: headlines-Tabelle mit feed_id FK, feeds-Tabelle mit active-Flag
provides:
  - "Database-Methode get_feed_trends(days) mit SQL-Aggregation per Feed"
  - "GET /api/moodlight/feeds/trends Endpoint — öffentlich, sortiert nach avg_score DESC"
affects:
  - 25-visualisierung
  - github-page-feed-ranking

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "SQL-Aggregation via GROUP BY f.id, f.name mit AVG/COUNT/MIN/MAX auf JOIN headlines/feeds"
    - "days-Parameter-Validierung: nur 7 und 30 erlaubt, Fallback auf 7"

key-files:
  created: []
  modified:
    - sentiment-api/database.py
    - sentiment-api/moodlight_extensions.py

key-decisions:
  - "Kein Redis-Cache für den Trends-Endpoint (Daten ändern sich alle 30 Min, Latenz tolerierbar)"
  - "Kein @api_login_required — GitHub Page braucht den Endpoint ohne Authentifizierung"
  - "days-Parameter auf 7 und 30 eingeschränkt statt beliebige Werte zuzulassen"

patterns-established:
  - "Feed-Aggregation-Pattern: JOIN feeds f ON h.feed_id = f.id + GROUP BY f.id, f.name + HAVING COUNT > 0"

requirements-completed: [TREND-01, TREND-02]

# Metrics
duration: 2min
completed: 2026-03-27
---

# Phase 24 Plan 01: Backend-Trendberechnung Summary

**PostgreSQL-Aggregation per Feed mit get_feed_trends(days) in database.py und öffentlichem GET /api/moodlight/feeds/trends Endpoint als sortiertes Feed-Ranking**

## Performance

- **Duration:** ~2 min
- **Started:** 2026-03-27T13:43:02Z
- **Completed:** 2026-03-27T13:44:19Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments

- Neue Methode `get_feed_trends(days=7)` in der `Database`-Klasse mit SQL-Aggregation (AVG, COUNT, MIN, MAX) per Feed über konfigurierbare Zeitfenster
- Neuer öffentlicher Endpoint `GET /api/moodlight/feeds/trends?days=7` liefert JSON-Array mit Feed-Ranking sortiert nach avg_score absteigend
- Feeds ohne Headlines im Zeitfenster werden durch `HAVING COUNT(h.id) > 0` automatisch ausgeschlossen

## Task Commits

Jeder Task wurde atomisch committed:

1. **Task 1: get_feed_trends() in Database-Klasse** — `d0c8025` (feat)
2. **Task 2: GET /api/moodlight/feeds/trends in moodlight_extensions.py** — `eede91f` (feat)

**Plan-Metadaten:** (folgt in Final Commit)

## Files Created/Modified

- `sentiment-api/database.py` — Neue Methode `get_feed_trends(days: int = 7)` nach `get_score_percentiles()`, vor `check_connection_health()`
- `sentiment-api/moodlight_extensions.py` — Neuer Endpoint `GET /api/moodlight/feeds/trends` als innere Funktion in `register_moodlight_endpoints()`, vor dem abschließenden logger.info

## Decisions Made

- Kein Redis-Cache für den Trends-Endpoint: Daten ändern sich nur alle 30 Minuten durch den Background-Worker, DB-Latenz ist tolerierbar
- Kein `@api_login_required`: Der Endpoint ist für die GitHub Page bestimmt, die ohne Login-Kontext aufgerufen wird
- `days`-Parameter auf 7 und 30 beschränkt: Verhindert beliebig große Zeitfenster mit hoher DB-Last; ungültige Werte fallen still auf 7 zurück

## Deviations from Plan

Keine — Plan exakt wie beschrieben ausgeführt.

## Issues Encountered

Keine. `python -c "import ast, sys; ..."` schlug fehl, weil macOS `/Library/Frameworks/Python.framework/Versions/2.7/` als Standard-Python verwendet — `python3` explizit verwendet, Syntax-Check bestand problemlos.

## User Setup Required

Keine externe Service-Konfiguration erforderlich. Der Endpoint wird nach dem nächsten Deployment auf `analyse.godsapp.de` verfügbar sein.

## Next Phase Readiness

- `GET /api/moodlight/feeds/trends?days=7` und `?days=30` sind implementiert und können nach Deployment genutzt werden
- Phase 25 (Visualisierung) und die GitHub Page können diesen Endpoint als Datenquelle für das Feed-Ranking verwenden
- Kein Blocker — ausreichend historische Daten vorausgesetzt (11 aktive Feeds seit v5.0)

---
*Phase: 24-backend-trendberechnung*
*Completed: 2026-03-27*
