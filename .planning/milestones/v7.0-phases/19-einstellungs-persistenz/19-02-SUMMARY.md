---
phase: 19-einstellungs-persistenz
plan: "02"
subsystem: backend-database
tags: [postgresql, settings, database, startup, db-first, phase19]
dependency_graph:
  requires: [19-01]
  provides: [settings-crud-methods, db-first-startup]
  affects: [19-03]
tech_stack:
  added: []
  patterns: [db-first-config, env-fallback, upsert, singleton]
key_files:
  created: []
  modified:
    - sentiment-api/database.py
    - sentiment-api/app.py
decisions:
  - "load_settings_from_db() lokal mit `from database import get_database` — vermeidet zirkulaere Imports"
  - "Logging-Konfiguration (basicConfig) wird vor load_settings_from_db() aufgerufen — damit DB-Warnings sichtbar sind"
  - "login_required-Decorator nach dem Config-Block platziert — keine Aenderung an Logik, nur Reihenfolge angepasst"
metrics:
  duration: "3 minutes"
  completed: "2026-03-27T10:48:48Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 0
  files_modified: 2
---

# Phase 19 Plan 02: DB-First Startup-Logik Summary

Settings CRUD-Methoden in Database-Klasse + DB-First Startup in app.py: Beim Container-Start werden Einstellungen aus PostgreSQL geladen, Env-Variablen dienen als Fallback.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | get_setting / set_setting / get_all_settings in Database-Klasse | fef2351 | sentiment-api/database.py |
| 2 | Startup-Logik in app.py auf DB-First umstellen | 747243a | sentiment-api/app.py |

## What Was Built

### database.py — Neue Methoden

- `get_setting(key, default=None)`: SELECT aus `settings`-Tabelle, gibt String oder Default zurueck, try/except mit logger.error
- `set_setting(key, value)`: UPSERT via `INSERT ... ON CONFLICT DO UPDATE SET value=EXCLUDED.value, updated_at=NOW()`, rollback bei Fehler
- `get_all_settings()`: Gibt alle Einstellungen als `Dict[str, str]` zurueck, leer bei Fehler

### app.py — DB-First Startup

- Neue Funktion `load_settings_from_db()` laedt beim App-Start alle 4 Einstellungen aus der DB
- Einstellungen: `analysis_interval`, `headlines_per_source`, `anthropic_api_key`, `admin_password_hash`
- Fallback auf Env-Variablen wenn DB nicht erreichbar oder Wert leer/fehlt
- `_startup_settings = load_settings_from_db()` ersetzt den alten try/except-Block fuer DEFAULT_HEADLINES_FROM_ENV und die direkten os.environ.get()-Aufrufe fuer API-Key und Admin-Passwort
- Logging-Konfiguration vor load_settings_from_db() verschoben — Warnings sind damit sichtbar

## Decisions Made

1. **Lokaler Import in load_settings_from_db():** `from database import get_database` ist lokal in der Funktion definiert — vermeidet zirkulaere Imports da database.py nichts aus app.py importiert.
2. **basicConfig vor load_settings_from_db():** Logging muss initialisiert sein bevor die DB-Verbindung aufgebaut wird, damit Warn- und Fehlermeldungen beim Startup erscheinen.
3. **login_required bleibt unveraendert:** Decorator wurde lediglich nach dem Config-Block platziert — keine Logik-Aenderung.

## Deviations from Plan

**1. [Rule 3 - Blocking] Reihenfolge: logging.basicConfig vor load_settings_from_db()**
- **Found during:** Task 2
- **Issue:** Im Plan stand `load_settings_from_db()` direkt nach `app.permanent_session_lifetime`. Die Logging-Konfiguration (basicConfig) stand urspruenglich nach den Admin/API-Key-Zeilen — waere damit erst NACH dem DB-Aufruf aktiv gewesen.
- **Fix:** basicConfig-Block direkt nach `app.permanent_session_lifetime` platziert, dann erst `load_settings_from_db()`.
- **Files modified:** sentiment-api/app.py
- **Commit:** 747243a

## Known Stubs

None — alle Methoden sind vollstaendig implementiert. Die Settings-Endpoints (GET/POST /api/moodlight/settings) folgen in Plan 03.

## Self-Check: PASSED

- `sentiment-api/database.py`: FOUND, enthaelt get_setting, set_setting, get_all_settings
- `sentiment-api/app.py`: FOUND, enthaelt load_settings_from_db (Definition + Aufruf)
- Commit fef2351: FOUND
- Commit 747243a: FOUND
