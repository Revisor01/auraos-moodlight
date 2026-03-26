---
phase: 09-db-schema-worker-integration
plan: 01
subsystem: backend/database
tags: [sql, postgresql, schema, migration, feeds]
dependency_graph:
  requires: []
  provides: [feeds-table-schema, migration-001]
  affects: [sentiment-api/init.sql, sentiment-api/migrations/]
tech_stack:
  added: [migrations directory]
  patterns: [idempotent-migration, CREATE-IF-NOT-EXISTS, ON-CONFLICT-DO-NOTHING]
key_files:
  created:
    - sentiment-api/migrations/001_add_feeds_table.sql
  modified:
    - sentiment-api/init.sql
decisions:
  - "Focus.de als Feed ausgeschlossen (FEED-06: gibt 404 zurück) — dokumentiert im Kommentar"
  - "Migrations-Datei ist idempotent — kann mehrfach ausgeführt werden ohne Fehler"
  - "RAISE NOTICE Bestätigung nach Ausführung — macht Erfolg sichtbar im psql-Output"
metrics:
  duration: ~5 min
  completed_date: "2026-03-26"
  tasks_completed: 2
  tasks_total: 2
  files_created: 1
  files_modified: 1
requirements_fulfilled:
  - FEED-01
  - FEED-06
---

# Phase 09 Plan 01: DB-Schema feeds-Tabelle Summary

feeds-Tabelle in PostgreSQL definiert: init.sql erweitert und idempotentes Migrations-SQL für das laufende Produktionssystem erstellt.

## What Was Built

PostgreSQL-Schema für konfigurierbare RSS-Feeds: `sentiment-api/init.sql` erhielt eine neue `feeds`-Tabellensection mit 7 Feldern (id, name, url, active, last_fetched_at, error_count, created_at/updated_at), zwei Performance-Indizes und 11 Default-Feeds als idempotenten INSERT (Focus.de ausgeschlossen wegen 404). Parallel wurde `sentiment-api/migrations/001_add_feeds_table.sql` erstellt — identisches idempotentes Schema für das laufende Produktionssystem ohne Datenverlust.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | feeds-Tabelle in init.sql ergänzen | 135a3e7 | sentiment-api/init.sql |
| 2 | Migrations-SQL für Produktionssystem erstellen | d1731fb | sentiment-api/migrations/001_add_feeds_table.sql |

## Decisions Made

- **Focus.de ausgeschlossen**: Feed gibt 404 zurück (FEED-06) — nicht in Default-Feeds aufgenommen, Grund im SQL-Kommentar dokumentiert.
- **Idempotenz als Pflicht**: Beide SQL-Dateien verwenden `CREATE TABLE IF NOT EXISTS` und `ON CONFLICT (url) DO NOTHING` — sicheres Re-Run ohne Seiteneffekte.
- **RAISE NOTICE Bestätigung**: Migrations-Datei gibt nach Ausführung die Anzahl der Feeds aus — erleichtert manuelles Debugging auf dem Server.

## Deviations from Plan

None — Plan executed exactly as written.

Der einzige potenzielle Hinweis: `grep "Focus"` gibt 1 Treffer in jedem File, aber ausschließlich im Kommentar (`-- ohne Focus.de — FEED-06`). Kein Focus.de als Feed-URL enthalten. Acceptance Criteria korrekt erfüllt.

## Deployment Note

Das Migrations-Skript muss einmalig manuell auf dem Produktionssystem ausgeführt werden:

```bash
docker exec -i moodlight-postgres psql -U moodlight -d moodlight < migrations/001_add_feeds_table.sql
```

Dieser Schritt liegt außerhalb dieser Phase (Plan 02 übernimmt die Python-Integration; die manuelle Ausführung kann parallel oder danach erfolgen).

## Known Stubs

None — reine SQL-Schema-Dateien ohne Stub-Muster.

## Self-Check: PASSED

- FOUND: sentiment-api/init.sql
- FOUND: sentiment-api/migrations/001_add_feeds_table.sql
- FOUND: commit 135a3e7 (Task 1)
- FOUND: commit d1731fb (Task 2)
