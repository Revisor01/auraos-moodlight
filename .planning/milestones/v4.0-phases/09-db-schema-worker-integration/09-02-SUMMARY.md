---
phase: 09-db-schema-worker-integration
plan: 02
subsystem: backend/python
tags: [python, postgresql, feeds, refactor, background-worker]
dependency_graph:
  requires: [09-01]
  provides: [get-active-feeds-method, worker-db-feed-path, app-lazy-feed-loading]
  affects: [sentiment-api/database.py, sentiment-api/background_worker.py, sentiment-api/app.py, sentiment-api/shared_config.py]
tech_stack:
  added: []
  patterns: [lazy-db-loading, singleton-db-access, RealDictCursor]
key_files:
  created: []
  modified:
    - sentiment-api/database.py
    - sentiment-api/background_worker.py
    - sentiment-api/app.py
    - sentiment-api/shared_config.py
decisions:
  - "RSS_FEEDS-Dict vollständig aus shared_config.py entfernt — erklärender Kommentar bleibt als Kontext"
  - "get_active_feeds() gibt leere Liste bei DB-Fehler zurück (kein raise) — Worker überspringt Update statt zu crashen"
  - "feed_count via separatem DB-Call in _perform_update() — einfachster Weg ohne _fetch_headlines() API zu ändern"
  - "app.py nutzt inline-Import von get_database (konsistent mit bestehendem Stil in app.py)"
metrics:
  duration: ~2 min
  completed_date: "2026-03-26"
  tasks_completed: 2
  tasks_total: 2
  files_created: 0
  files_modified: 4
requirements_fulfilled:
  - FEED-01
  - FEED-06
---

# Phase 09 Plan 02: Python-Integration DB-Feed-Pfad Summary

RSS_FEEDS-Dict aus shared_config.py entfernt — Background Worker und app.py lesen Feeds jetzt aus PostgreSQL via get_active_feeds().

## What Was Built

`database.py` erhielt eine neue `get_active_feeds()`-Methode (RealDictCursor-Abfrage auf feeds-Tabelle, WHERE active = TRUE, ORDER BY name ASC, leere Liste bei Exception). `background_worker.py` wurde vollständig auf den DB-Pfad umgestellt: `_fetch_headlines()` ruft jetzt `db.get_active_feeds()` auf und iteriert über `feed_row['name']`/`['url']` statt über den hardcodierten Dict. `_perform_update()` übergibt `source_count=feed_count` statt der hardcodierten 12. `app.py` lädt Feeds jetzt lazy pro Request via `get_database().get_active_feeds()` am Anfang der `get_news()`-Route — die bestehende Schleifen-Struktur bleibt unverändert. `shared_config.py` enthält nur noch `get_sentiment_category()` mit erklärendem Kommentar zur Entfernung von RSS_FEEDS.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | get_active_feeds() in database.py ergänzen | 50820e4 | sentiment-api/database.py |
| 2 | Background Worker und app.py auf DB-Pfad umstellen + shared_config.py bereinigen | 19fed67 | sentiment-api/background_worker.py, sentiment-api/app.py, sentiment-api/shared_config.py |

## Decisions Made

- **RSS_FEEDS entfernt, Kommentar erhalten**: shared_config.py enthält keinen RSS_FEEDS-Dict mehr — nur einen erklärenden Kommentar als Kontext für spätere Entwickler.
- **Leere Liste als Fehler-Fallback**: `get_active_feeds()` gibt bei DB-Exception eine leere Liste zurück. Background Worker erkennt dies und überspringt das Update (`"Keine aktiven Feeds in der Datenbank gefunden — Update übersprungen"`).
- **Separater feed_count-Call**: `_perform_update()` ruft `get_active_feeds()` ein zweites Mal für feed_count auf — einfachster Weg ohne _fetch_headlines() Rückgabetyp zu ändern.
- **Inline-Import in app.py**: `from database import get_database` direkt in der Route-Funktion — konsistent mit bestehendem Stil in app.py (andere Routen nutzen ebenfalls inline imports).

## Deviations from Plan

None — Plan executed exactly as written.

Einziger Hinweis: `grep -r "RSS_FEEDS" sentiment-api/*.py` findet 1 Treffer in shared_config.py — aber ausschließlich im erklärenden Kommentar (`# RSS_FEEDS wurde in Phase 9 entfernt`). Kein funktionaler RSS_FEEDS-Code ist mehr vorhanden.

## Known Stubs

None — keine hardcodierten Placeholder-Daten, keine leeren Datenstrukturen die zu UI fließen.

## Self-Check: PASSED

- FOUND: `def get_active_feeds` in sentiment-api/database.py (line 358)
- FOUND: `WHERE active = TRUE` in sentiment-api/database.py (line 369)
- FOUND: `ORDER BY name ASC` in sentiment-api/database.py (line 370)
- FOUND: `get_active_feeds()` in sentiment-api/background_worker.py (lines 77, 139)
- FOUND: `get_active_feeds()` in sentiment-api/app.py (line 332)
- FOUND: `source_count=feed_count` in sentiment-api/background_worker.py (line 108)
- FOUND: `def get_sentiment_category` in sentiment-api/shared_config.py (line 6)
- FOUND: commit 50820e4 (Task 1)
- FOUND: commit 19fed67 (Task 2)
- VERIFIED: 0 RSS_FEEDS occurrences in functional code (only comment in shared_config.py)
