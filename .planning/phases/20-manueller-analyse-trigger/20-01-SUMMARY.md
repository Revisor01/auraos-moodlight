---
phase: 20-manueller-analyse-trigger
plan: 01
subsystem: api
tags: [flask, background-worker, sentiment, redis, postgresql]

# Dependency graph
requires:
  - phase: 19-einstellungs-persistenz
    provides: SentimentUpdateWorker.reconfigure(), api_login_required decorator, register_moodlight_endpoints() Muster
provides:
  - SentimentUpdateWorker.trigger() — synchrone Methode für sofortige Sentiment-Analyse mit Rückgabe-Dict
  - POST /api/moodlight/analyze/trigger — geschützter Endpoint für manuellen Analyse-Start vom Dashboard
affects:
  - 20-02 (Dashboard-Button der diesen Endpoint aufruft)

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Worker-Methode mit Rückgabewert neben void _perform_update() — trigger() gibt dict zurück"
    - "App-Context-Wrapping bei synchronem Worker-Aufruf aus Request-Handler: with worker.app.app_context()"

key-files:
  created: []
  modified:
    - sentiment-api/background_worker.py
    - sentiment-api/moodlight_extensions.py

key-decisions:
  - "trigger() als separate public Methode statt _perform_update() mit Rückgabewert refactorn — minimale Änderung, keine Regression im Background Loop"
  - "RuntimeError bei fehlenden Headlines/Analyse-Ergebnis (kein silent return) — Endpoint gibt 422 zurück, klar kommuniziertes Fehlerverhalten"
  - "Beide Redis-Cache-Keys invalidiert (moodlight:current:v2 + moodlight:current) — konsistent mit Background Worker Invalidierung"

patterns-established:
  - "trigger()-Methode delegiert an _fetch_headlines() + analyze_function wie _perform_update(), ohne Code-Duplizierung der Worker-Logik"

requirements-completed: [API-02, CTRL-01]

# Metrics
duration: 8min
completed: 2026-03-26
---

# Phase 20 Plan 01: Manueller Analyse-Trigger Summary

**POST /api/moodlight/analyze/trigger Endpoint mit SentimentUpdateWorker.trigger() — synchroner manueller Analyse-Auslöser mit vollständiger DB-Persistenz, Cache-Invalidierung und strukturierten Fehler-Responses**

## Performance

- **Duration:** ~8 min
- **Started:** 2026-03-26T00:00:00Z
- **Completed:** 2026-03-26
- **Tasks:** 2/2
- **Files modified:** 2

## Accomplishments

- `SentimentUpdateWorker.trigger()` — neue public Methode, die synchron Headlines holt, Sentiment analysiert, in DB speichert und dict zurückgibt
- `POST /api/moodlight/analyze/trigger` in `register_moodlight_endpoints()` registriert, geschützt mit `@api_login_required`
- Redis-Cache-Invalidierung nach manuellem Trigger (beide Keys: `moodlight:current:v2` + `moodlight:current`)
- Saubere Fehlerbehandlung: 503 (Worker nicht verfügbar), 422 (RuntimeError), 500 (unerwartete Exception)

## Task Commits

Jeder Task wurde atomar committed:

1. **Task 1: trigger()-Methode zum SentimentUpdateWorker hinzufügen** - `b1fc11d` (feat)
2. **Task 2: POST /api/moodlight/analyze/trigger Endpoint registrieren** - `8f3c350` (feat)

## Files Created/Modified

- `sentiment-api/background_worker.py` — `trigger()` Methode nach `reconfigure()` eingefügt (69 neue Zeilen)
- `sentiment-api/moodlight_extensions.py` — neuer Endpoint-Block nach `clear_moodlight_cache` (38 neue Zeilen)

## Decisions Made

- `trigger()` als separate public Methode statt `_perform_update()` zu refactorn: minimale invasive Änderung, Background Loop läuft weiterhin unverändert
- `RuntimeError` werfen statt stillen `return` bei Analyse-Problemen: Endpoint kann korrekt 422 zurückgeben und Fehler an Dashboard kommunizieren
- `with worker.app.app_context()` im Endpoint-Handler: konsistent mit `_worker_loop`, stellt DB-Zugriff sicher

## Deviations from Plan

Keine — Plan exakt wie spezifiziert umgesetzt.

## Issues Encountered

- `python --version` liefert Python 2.7 auf dem lokalen System. `python3 -m py_compile` mit Python 3.14 verwendt. Kein Problem für Deployment (Docker-Container nutzt Python 3.12).

## User Setup Required

Kein manueller Setup erforderlich — Endpoint ist direkt nach Deploy aktiv.

## Next Phase Readiness

- Trigger-API ist bereit für Plan 20-02 (Dashboard-Button)
- Endpoint erfordert aktive Session (`@api_login_required`) — passt zu bestehendem Login-System aus Phase 13

---
*Phase: 20-manueller-analyse-trigger*
*Completed: 2026-03-26*
