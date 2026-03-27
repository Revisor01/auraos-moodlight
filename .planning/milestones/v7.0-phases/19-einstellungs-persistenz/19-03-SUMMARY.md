---
phase: 19-einstellungs-persistenz
plan: "03"
subsystem: api
tags: [flask, background-worker, runtime-reload, settings-api, sentiment]

# Dependency graph
requires:
  - phase: 19-02
    provides: "get_setting / set_setting / get_all_settings auf Database-Klasse"
provides:
  - "SentimentUpdateWorker.reconfigure(interval_seconds, headlines_per_source) — Runtime-Reload ohne Container-Neustart"
  - "get_background_worker() Accessor-Funktion (Singleton-Zugriff)"
  - "GET /api/moodlight/settings — Einstellungen maskiert zurückgeben"
  - "PUT /api/moodlight/settings — Einstellungen ändern und sofort wirksam machen"
affects:
  - 21-dashboard-ui
  - alle Phasen die Worker-Intervall oder headlines_per_source lesen

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Runtime-Reconfigure: Worker-Parameter via Instanzmethode ändern ohne Thread-Neustart"
    - "API Key Masking: erste 10 Zeichen + '****' für sichere Anzeige"
    - "Passwort-Änderung mit current_password-Verifikation via werkzeug check_password_hash"

key-files:
  created: []
  modified:
    - sentiment-api/background_worker.py
    - sentiment-api/moodlight_extensions.py

key-decisions:
  - "anthropic_client wird bei API-Key-Änderung sofort in app.py neu initialisiert (über 'import app as main_app')"
  - "admin_password_hash wird nie zurückgegeben — nur has_admin_password: bool"
  - "PUT /api/moodlight/settings aktualisiert DB und ruft worker.reconfigure() auf — beide Ebenen synchron"

patterns-established:
  - "get_background_worker() als Accessor für API-Endpoints statt globalem Import"
  - "Einstellungsänderungen: zuerst DB schreiben, dann Worker rekonfigurieren"

requirements-completed: [CFG-03, API-01]

# Metrics
duration: 15min
completed: 2026-03-26
---

# Phase 19 Plan 03: Worker-Reconfigure und Settings-API Summary

**SentimentUpdateWorker.reconfigure() für Runtime-Reload + GET/PUT /api/moodlight/settings mit sofortiger Worker-Synchronisation**

## Performance

- **Duration:** 15 min
- **Started:** 2026-03-26T10:00:00Z
- **Completed:** 2026-03-26T10:15:00Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments

- `reconfigure(interval_seconds, headlines_per_source)` in `SentimentUpdateWorker` — Intervall und Headlines-Anzahl änderbar ohne Thread-Neustart
- `_fetch_headlines()` liest nun `self.headlines_per_source` statt hartcodierter `1`
- `get_background_worker()` Accessor-Funktion für API-Endpoints
- `GET /api/moodlight/settings` gibt alle konfigurierbaren Einstellungen zurück (API Key maskiert, Passwort-Hash nie)
- `PUT /api/moodlight/settings` aktualisiert DB und rekonfiguriert Worker sofort; Passwort-Änderung erfordert current_password-Verifikation

## Task Commits

Jeder Task wurde atomar committed:

1. **Task 1: reconfigure() in SentimentUpdateWorker + dynamisches headlines_per_source** - `29ae1bf` (feat)
2. **Task 2: GET/PUT /api/moodlight/settings in moodlight_extensions.py** - `563ffc7` (feat)

## Files Created/Modified

- `sentiment-api/background_worker.py` — `self.headlines_per_source` als Instanzvariable, `reconfigure()`-Methode nach `stop()`, `_fetch_headlines()` liest `self.headlines_per_source`, `get_background_worker()` am Dateiende
- `sentiment-api/moodlight_extensions.py` — Import von `get_background_worker` und `anthropic_sdk` ergänzt, zwei neue Endpoints am Ende der `register_moodlight_endpoints()`-Funktion

## Decisions Made

- `anthropic_client` wird bei API-Key-Änderung über `import app as main_app` sofort neu initialisiert — kein Container-Neustart nötig
- `admin_password_hash` wird nie in der API-Response zurückgegeben, nur `has_admin_password: bool`
- PUT schreibt zuerst in DB, dann `worker.reconfigure()` — beide Ebenen bleiben synchron

## Deviations from Plan

Keine — Plan wurde exakt wie beschrieben ausgeführt.

## Issues Encountered

Keine.

## User Setup Required

Keine — keine externen Dienste erfordern manuelle Konfiguration.

## Next Phase Readiness

- Phase 21 (Dashboard-UI) kann `GET /api/moodlight/settings` und `PUT /api/moodlight/settings` direkt konsumieren
- Worker-Rekonfiguration ist vollständig implementiert — Intervall- und Headlines-Änderungen über die API werden sofort wirksam

---
*Phase: 19-einstellungs-persistenz*
*Completed: 2026-03-26*
