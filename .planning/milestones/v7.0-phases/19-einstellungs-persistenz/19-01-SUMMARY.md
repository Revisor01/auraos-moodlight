---
phase: 19-einstellungs-persistenz
plan: "01"
subsystem: backend-database
tags: [postgresql, schema, migration, settings, phase19]
dependency_graph:
  requires: []
  provides: [settings-table, migrate_settings_sql]
  affects: [19-02, 19-03]
tech_stack:
  added: []
  patterns: [key-value-store, idempotent-migration, insert-on-conflict-do-nothing]
key_files:
  created:
    - sentiment-api/migrate_settings.sql
  modified:
    - sentiment-api/init.sql
decisions:
  - "settings als Key-Value-Tabelle (VARCHAR PRIMARY KEY) statt eigenem Konfigurations-ORM — minimal, direkt via psycopg2 abfragbar"
  - "ON CONFLICT DO NOTHING für Default-Einträge — bestehende Produktionswerte bleiben beim Re-Deploy erhalten"
  - "Trigger update_updated_at_column() wird wiederverwendet — kein Duplikat nötig (bereits in init.sql definiert)"
metrics:
  duration: "4 minutes"
  completed: "2026-03-27T10:46:00Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 1
  files_modified: 1
---

# Phase 19 Plan 01: settings-Tabelle (PostgreSQL Key-Value-Store) Summary

PostgreSQL Key-Value-Tabelle `settings` als Fundament für Einstellungs-Persistenz — im Schema (init.sql) und als idempotentes Migrations-Script (migrate_settings.sql) für die laufende Produktions-DB.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | settings-Tabelle in init.sql einfügen | 4f7bb6b | sentiment-api/init.sql |
| 2 | migrate_settings.sql für Produktions-DB erstellen | 74b265f | sentiment-api/migrate_settings.sql |

## What Was Built

### settings-Tabelle
- Spalten: `key VARCHAR(100) PRIMARY KEY`, `value TEXT NOT NULL`, `updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()`
- Trigger `trigger_update_settings` für automatisches `updated_at`-Update
- 4 Default-Einträge: `analysis_interval` (1800s), `headlines_per_source` (1), `anthropic_api_key` (leer), `admin_password_hash` (leer)
- `INSERT ON CONFLICT DO NOTHING` — bestehende Werte bleiben beim Re-Deploy erhalten

### migrate_settings.sql
- Identischer DDL-Block wie in init.sql
- Idempotent: `CREATE TABLE IF NOT EXISTS` + `ON CONFLICT DO NOTHING`
- Anwendungsbefehl: `docker exec -i moodlight-postgres psql -U moodlight moodlight < migrate_settings.sql`
- Abschluss-Bestätigung via `RAISE NOTICE`

## Decisions Made

1. **Key-Value statt eigenem ORM:** `settings` als einfache Key-Value-Tabelle — direkt via psycopg2 abfragbar, kein zusätzlicher Layer nötig.
2. **ON CONFLICT DO NOTHING:** Default-Einträge überschreiben keine bestehenden Produktionswerte beim erneuten Ausführen.
3. **Funktion wiederverwendet:** `update_updated_at_column()` ist bereits in init.sql definiert — kein Duplikat für settings-Trigger nötig.

## Deviations from Plan

None — Plan exakt wie beschrieben ausgeführt.

## Known Stubs

None — diese Phase legt nur das DB-Schema an. Die Python-Integration (Lesen/Schreiben aus `settings`) erfolgt in Plan 02 und 03.

## Self-Check: PASSED

- `sentiment-api/init.sql`: FOUND, enthält `CREATE TABLE IF NOT EXISTS settings`
- `sentiment-api/migrate_settings.sql`: FOUND, enthält `ON CONFLICT DO NOTHING`
- Commit 4f7bb6b: FOUND
- Commit 74b265f: FOUND
