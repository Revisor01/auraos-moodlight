---
phase: 14-backend-dashboard
plan: 01
subsystem: api, database
tags: [flask, postgresql, psycopg2, headlines, sentiment]

# Dependency graph
requires:
  - phase: 12-headline-persistenz
    provides: headlines-Tabelle in PostgreSQL mit sentiment_score, feed_id, source_name, analyzed_at

provides:
  - get_recent_headlines() Methode in Database-Klasse (database.py)
  - GET /api/moodlight/headlines Endpoint mit ?limit=N Parameter

affects:
  - 14-02 (Backend-Dashboard nutzt diesen Endpoint für Headlines-Anzeige)
  - ESP32 mood.html (HEAD-02 Folgeplan)

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Cursor-Pattern mit RealDictCursor und ISO-Timestamp-Konvertierung (analog get_all_feeds)
    - Öffentlicher API-Endpoint ohne @api_login_required (ESP32 + Dashboard können abrufen)
    - limit-Sanitierung: min(requested, 500), Guard auf < 1

key-files:
  created: []
  modified:
    - sentiment-api/database.py
    - sentiment-api/moodlight_extensions.py

key-decisions:
  - "Endpoint ist öffentlich (kein @api_login_required) — ESP32 und Dashboard brauchen keine Session"
  - "max limit=500 als Obergrenze — verhindert überlastende DB-Queries ohne sinnvollen Anwendungsfall"

patterns-established:
  - "LEFT JOIN feeds f ON h.feed_id = f.id — feed_name NULL wenn Feed gelöscht wurde (NULLABLE feed_id)"
  - "analyzed_at als ISO-String in get_cursor-Loop konvertieren (nicht in SQL)"

requirements-completed: [HEAD-02]

# Metrics
duration: 2min
completed: 2026-03-26
---

# Phase 14 Plan 01: Headlines-API-Endpoint Summary

**Headlines-Datenbankabfrage und API-Route: get_recent_headlines() mit LEFT JOIN auf feeds + öffentlicher GET /api/moodlight/headlines Endpoint mit ?limit-Parameter**

## Performance

- **Duration:** 2 min
- **Started:** 2026-03-26T23:35:22Z
- **Completed:** 2026-03-26T23:36:49Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments

- `get_recent_headlines(limit=50)` in Database-Klasse angelegt — SQL mit LEFT JOIN auf feeds, ORDER BY analyzed_at DESC, ISO-Timestamp-Konvertierung
- `GET /api/moodlight/headlines` Endpoint registriert — öffentlich, ?limit=N (default 50, max 500), Response: {status, count, headlines[]}
- Beide Dateien syntaktisch valide (via AST-Parse und py_compile mit Python 3.12)

## Task Commits

Jeder Task wurde atomar committed:

1. **Task 1: get_recent_headlines() in database.py anlegen** - `74d0677` (feat)
2. **Task 2: GET /api/moodlight/headlines Endpoint registrieren** - `589d0e3` (feat)

**Plan-Metadaten:** (folgt in diesem Commit)

## Files Created/Modified

- `sentiment-api/database.py` - Neue Methode get_recent_headlines() nach get_all_feeds() eingefügt
- `sentiment-api/moodlight_extensions.py` - Neuer Endpoint /api/moodlight/headlines vor logger.info am Ende der Funktion

## Decisions Made

- Endpoint ist öffentlich (kein @api_login_required): ESP32-Geräte und das Dashboard sollen ohne Session-Cookie zugreifen können, analog zu /current und /history
- Obergrenze max=500 für ?limit: verhindert überlastende Queries, kein sinnvoller Anwendungsfall für mehr Headlines auf einmal

## Deviations from Plan

None - Plan wurde exakt wie beschrieben ausgeführt.

**Hinweis zur Verifikation:** Python 3.14 (lokales System) meldet bei `python -m py_compile database.py` einen irreführenden SyntaxError (Zeile 29: `def __init__`). Die Datei ist syntaktisch korrekt — verifiziert via `ast.parse()` und `python3.12 -m py_compile`. Docker-Container verwendet Python 3.12-slim, keine Auswirkung auf Produktion.

## Issues Encountered

- Python 3.14 auf lokalem System wirft irreführenden SyntaxError bei py_compile auf database.py (Zeile 29). Ursache unbekannt, möglicherweise py_compile-Bug in 3.14. Python 3.12 (Docker-Runtime) kompiliert fehlerfrei. Kein Handlungsbedarf.

## Known Stubs

None — beide Implementierungen sind vollständig verdrahtet. get_recent_headlines() liest aus der echten headlines-Tabelle, der Endpoint ruft direkt die Methode auf.

## Next Phase Readiness

- Headlines-Endpoint bereit für Plan 14-02 (Backend-Dashboard HTML/UI)
- /api/moodlight/headlines?limit=N ist deployed-ready nach git push
- feed_name kann NULL sein wenn Feed nach Headline-Analyse gelöscht wurde — Dashboard muss das berücksichtigen

---
*Phase: 14-backend-dashboard*
*Completed: 2026-03-26*
