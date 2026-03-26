---
phase: 10-feed-api
plan: 01
subsystem: api
tags: [flask, postgresql, psycopg2, rest-api, feeds, crud]

# Dependency graph
requires:
  - phase: 09-db-schema
    provides: feeds-Tabelle in PostgreSQL, get_active_feeds() Methode in database.py

provides:
  - "get_all_feeds(): alle Feeds (aktiv + inaktiv) mit allen Feldern aus PostgreSQL"
  - "add_feed(name, url): neuen Feed anlegen mit UniqueViolation-Handling"
  - "delete_feed(feed_id): Feed per ID loeschen, True/False Rueckgabe"
  - "GET /api/moodlight/feeds: Feed-Liste als JSON-Array"
  - "POST /api/moodlight/feeds: neuen Feed anlegen mit URL-Erreichbarkeitspruefung (5s Timeout)"
  - "DELETE /api/moodlight/feeds/<id>: Feed loeschen, 204/404"

affects: [11-web-interface, future-feed-management]

# Tech tracking
tech-stack:
  added: ["requests als http_requests fuer URL-Validierung in moodlight_extensions.py"]
  patterns:
    - "RETURNING-Clause fuer direkte Rueckgabe von INSERT-Ergebnis"
    - "ValueError fuer UniqueViolation als saubere API-Abstraktion (psycopg2 -> Python)"
    - "stream=True + resp.close() fuer Erreichbarkeitspruefung ohne Body-Download"
    - "http_requests Alias um Namenskollision mit Flasks request-Objekt zu vermeiden"

key-files:
  created: []
  modified:
    - "sentiment-api/database.py"
    - "sentiment-api/moodlight_extensions.py"

key-decisions:
  - "Legacy /api/feedconfig war bereits vor dieser Phase entfernt worden — kein Cleanup noetig"
  - "URL-Validierung mit 5s Timeout und HTTP-Status >= 400 Pruefung, stream=True verhindert Body-Download"
  - "409 fuer Duplikat-URL (UniqueViolation), 422 fuer nicht erreichbare URL — klare HTTP-Semantik"

patterns-established:
  - "Feed-CRUD: get_all_feeds/add_feed/delete_feed in database.py, Endpoints in moodlight_extensions.py"
  - "Timestamp-Konvertierung: isinstance(x, datetime) -> isoformat() Pattern"

requirements-completed: [FEED-02, FEED-03, FEED-04, FEED-05]

# Metrics
duration: 8min
completed: 2026-03-26
---

# Phase 10 Plan 01: Feed-CRUD-API Summary

**Feed-CRUD REST-API: GET/POST/DELETE /api/moodlight/feeds mit PostgreSQL-Backend und URL-Erreichbarkeitspruefung (5s Timeout)**

## Performance

- **Duration:** ~8 min
- **Started:** 2026-03-26T22:18:00Z
- **Completed:** 2026-03-26T22:26:46Z
- **Tasks:** 2 (Task 3 uebersprungen — Deployment durch User)
- **Files modified:** 2

## Accomplishments

- Drei neue DB-Methoden in database.py: `get_all_feeds()`, `add_feed()`, `delete_feed()` mit korrektem commit/rollback-Pattern
- Drei REST-Endpoints in moodlight_extensions.py: GET liefert alle Feeds mit Timestamps, POST validiert URL-Erreichbarkeit (5s Timeout), DELETE gibt 204/404
- UniqueViolation wird sauber als ValueError abstrahiert und im Endpoint als 409 zurueckgegeben

## Task Commits

1. **Task 1: DB-Methoden get_all_feeds, add_feed, delete_feed** - `4c4f7aa` (feat)
2. **Task 2: Feed-Endpoints GET/POST/DELETE in moodlight_extensions.py** - `3696efc` (feat)
3. **Task 3: Deployment-Verifikation** - Uebersprungen (Deployment durch User via Portainer Webhook nach git push)

## Files Created/Modified

- `sentiment-api/database.py` - get_all_feeds(), add_feed(), delete_feed() nach get_active_feeds() eingefuegt
- `sentiment-api/moodlight_extensions.py` - drei Feed-Endpoints + import requests as http_requests

## Decisions Made

- `configure_feeds()` / `/api/feedconfig` aus app.py war bereits vor dieser Phase nicht mehr vorhanden — Legacy-Cleanup entfiel
- URL-Validierung mit `stream=True` und `resp.close()` vermeidet Download des gesamten Feed-Inhalts bei der Pruefung
- Alias `http_requests` statt `requests` um Namenskollision mit Flasks `request`-Objekt zu vermeiden

## Deviations from Plan

### Planabweichung (nicht automtisch gefixt, irrelevant)

**Beobachtung:** Plan beschreibt in Task 2 Teil B das Entfernen von `configure_feeds()` aus app.py. Diese Funktion existierte zum Zeitpunkt der Ausfuehrung nicht mehr in app.py — war bereits in einer frueheren Phase entfernt worden.

**Auswirkung:** Kein Handlungsbedarf. Verifikations-Check bestaetigt Abwesenheit des Legacy-Endpoints.

---

**Total deviations:** 0 auto-fixes — eine Plan-Aktion entfiel da Legacy bereits bereinigt war

## Issues Encountered

Keine.

## User Setup Required

None — keine externe Service-Konfiguration erforderlich.

**Deployment:** User pusht via `git push`. GitHub Actions baut Docker-Image, Portainer Webhook deployt automatisch auf analyse.godsapp.de.

**Verifikation nach Deployment (curl-Tests aus Task 3):**
```bash
# GET — Feed-Liste
curl -s http://analyse.godsapp.de/api/moodlight/feeds | python3 -m json.tool

# POST — Gueltiger Feed
curl -s -X POST http://analyse.godsapp.de/api/moodlight/feeds \
  -H "Content-Type: application/json" \
  -d '{"url": "https://www.tagesschau.de/xml/rss2/", "name": "Tagesschau Test"}' \
  | python3 -m json.tool
# Erwartet: status=success, HTTP 201

# POST — Ungueltige URL (erwartet: 422)
curl -s -o /dev/null -w "%{http_code}" -X POST http://analyse.godsapp.de/api/moodlight/feeds \
  -H "Content-Type: application/json" \
  -d '{"url": "https://nicht-erreichbar.example.invalid/feed", "name": "Test"}'

# DELETE — <ID> aus POST-Test einsetzen (erwartet: 204)
curl -s -o /dev/null -w "%{http_code}" -X DELETE http://analyse.godsapp.de/api/moodlight/feeds/<ID>

# DELETE — Unbekannte ID (erwartet: 404)
curl -s -o /dev/null -w "%{http_code}" -X DELETE http://analyse.godsapp.de/api/moodlight/feeds/99999
```

## Next Phase Readiness

- Feed-CRUD-API vollstaendig implementiert und lokal verifiziert
- Bereit fuer Phase 11: Web-Interface Feed-Management Tab in setup.html
- Nach Deployment: curl-Tests aus Task 3 durchfuehren um Produktions-Verifikation abzuschliessen

---
*Phase: 10-feed-api*
*Completed: 2026-03-26*
