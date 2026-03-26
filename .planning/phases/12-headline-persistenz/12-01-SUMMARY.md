---
phase: 12-headline-persistenz
plan: 01
subsystem: database
tags: [postgresql, python, flask, background-worker, sentiment-analysis]

# Dependency graph
requires: []
provides:
  - headlines-Tabelle in PostgreSQL mit FK zu sentiment_history und feeds
  - Database.save_headlines() Bulk-INSERT Methode in database.py
  - Background Worker persistiert Headlines nach jeder automatischen Analyse
  - feed_id wird lückenlos durch analyze_headlines_openai_batch() durchgereicht
  - migration_phase12.sql für idempotente Migration auf Produktionssystem
affects: [dashboard, api-endpoints, headline-transparency]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Sekundäre Persistenz: save_headlines() Fehler bricht save_sentiment() nicht ab (try/except isolation)"
    - "Bulk-INSERT via executemany für effiziente Batch-Speicherung"
    - "feed_id transparent durch gesamten Datenpfad mitführen (fetch → analyze → persist)"

key-files:
  created:
    - sentiment-api/migration_phase12.sql
  modified:
    - sentiment-api/init.sql
    - sentiment-api/database.py
    - sentiment-api/background_worker.py
    - sentiment-api/app.py

key-decisions:
  - "save_headlines() Fehler wird nicht weitergeworfen — Sentiment-Score bleibt in jedem Fall gespeichert"
  - "feed_id in headlines-Tabelle NULLABLE (ON DELETE SET NULL) — historische Headlines bleiben erhalten wenn Feed gelöscht wird"
  - "executemany für Bulk-INSERT statt einzelne INSERTs pro Headline (Performance)"
  - "migration_phase12.sql separat von init.sql — init.sql läuft auf Produktionssystem nicht erneut"

patterns-established:
  - "Sekundäre Persistenz-Isolation: kritische Operation (save_sentiment) von optionaler Operation (save_headlines) via try/except entkoppeln"
  - "feed_id Durchreichung: numerische DB-ID statt String-Name für referentielle Integrität"

requirements-completed: [HEAD-01]

# Metrics
duration: 15min
completed: 2026-03-26
---

# Phase 12 Plan 01: Headline-Persistenz — DB-Schema, save_headlines(), Worker-Erweiterung Summary

**PostgreSQL headlines-Tabelle mit save_headlines() Bulk-INSERT und Background Worker Integration — Einzelscores werden nach jeder automatischen Sentiment-Analyse dauerhaft gespeichert**

## Performance

- **Duration:** ~15 min
- **Started:** 2026-03-26T23:10:00Z
- **Completed:** 2026-03-26T23:25:00Z
- **Tasks:** 3
- **Files modified:** 4 (+ 1 neu erstellt)

## Accomplishments
- headlines-Tabelle mit korrekten FK-Constraints in init.sql und migration_phase12.sql definiert
- Database.save_headlines() als Bulk-INSERT-Methode mit executemany implementiert
- Background Worker persistiert nach jedem Analyse-Zyklus alle Einzel-Headlines mit Scores
- feed_id wird lückenlos von _fetch_headlines() über analyze_headlines_openai_batch() bis save_headlines() durchgereicht

## Task Commits

Jeder Task wurde atomisch committed:

1. **Task 1: DB-Schema headlines-Tabelle** - `ec25f59` (feat)
2. **Task 2: database.py save_headlines()** - `cbce808` (feat)
3. **Task 3: feed_id durchreichen + Headline-Persistenz** - `4d4a5f5` (feat)

## Files Created/Modified
- `sentiment-api/init.sql` - headlines-Tabelle mit 9 Spalten, 3 Indizes und FK-Constraints ergänzt
- `sentiment-api/migration_phase12.sql` - Neu: Idempotentes Migration-SQL für Produktionssystem
- `sentiment-api/database.py` - save_headlines() Methode nach save_sentiment() eingefügt
- `sentiment-api/background_worker.py` - _fetch_headlines() mit feed_id, _perform_update() mit save_headlines()-Aufruf
- `sentiment-api/app.py` - feed_id in results.append von analyze_headlines_openai_batch() ergänzt

## Produktionsmigration

Die headlines-Tabelle muss einmalig auf dem Produktionssystem angelegt werden:

```bash
# SSH zum Server
ssh root@server.godsapp.de

# Postgres Container ID ermitteln
docker ps | grep postgres

# Migration ausführen (Container-Name ggf. anpassen)
docker exec -i <postgres-container-name> psql -U moodlight -d moodlight < /opt/auraos-moodlight/sentiment-api/migration_phase12.sql
```

Die Migration ist idempotent (IF NOT EXISTS Guards) — Mehrfachausführung ist sicher.

## Decisions Made
- save_headlines() Fehler wird nicht weitergeworfen: Der Sentiment-Score in sentiment_history ist die primäre Invariante. Headline-Persistenz ist sekundär — ein Fehler dort soll den gesamten Update-Zyklus nicht abbrechen.
- feed_id NULLABLE in headlines: Wenn ein Feed später gelöscht wird (ON DELETE SET NULL), bleiben die historischen Headlines erhalten und sind weiterhin über sentiment_history_id auffindbar.
- executemany statt Einzel-INSERTs: Ein einzelner DB-Roundtrip für alle Headlines einer Analyse-Runde.

## Deviations from Plan

None — Plan wurde exakt wie spezifiziert ausgeführt.

## Issues Encountered
None.

## Bekannte Limitierungen
- Die `/api/news` Route in app.py (Legacy-Endpunkt) ruft analyze_headlines_openai_batch() direkt auf ohne anschließend save_headlines() zu rufen. Headlines aus manuellen API-Aufrufen werden nicht in der headlines-Tabelle gespeichert. feed_id ist in diesem Fall None. Dies ist laut PLAN.md akzeptabel — die Persistenz ist auf den Background Worker beschränkt.

## Next Phase Readiness
- headlines-Tabelle ist bereit für spätere Dashboard- und API-Endpunkte (Phase 13+)
- Alle Einzel-Headlines inkl. Scores werden nach dem nächsten automatischen Analyse-Zyklus (30 Min) in der DB verfügbar sein
- Keine Blocker für Folge-Phasen

---
*Phase: 12-headline-persistenz*
*Completed: 2026-03-26*
