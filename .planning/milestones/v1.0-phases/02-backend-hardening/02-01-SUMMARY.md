---
phase: 02-backend-hardening
plan: 01
subsystem: backend
tags: [refactor, config, cleanup]
dependency_graph:
  requires: []
  provides: [shared_config.py]
  affects: [sentiment-api/app.py, sentiment-api/background_worker.py, sentiment-api/moodlight_extensions.py]
tech_stack:
  added: [shared_config.py]
  patterns: [single-source-of-truth, module-import-alias]
key_files:
  created:
    - sentiment-api/shared_config.py
  modified:
    - sentiment-api/app.py
    - sentiment-api/background_worker.py
    - sentiment-api/moodlight_extensions.py
decisions:
  - "Thresholds 0.30/0.10/-0.20/-0.50 aus background_worker.py als kanonische Werte gewaehlt (nicht die abweichenden 0.85/0.2 aus app.py)"
  - "rss_feeds = RSS_FEEDS Alias in app.py behalten fuer Kompatibilitaet mit /api/feedconfig Route (nutzt globale Variable)"
metrics:
  duration: 159s
  completed_date: "2026-03-25"
  tasks_completed: 2
  files_changed: 4
---

# Phase 02 Plan 01: Shared Config und Cleanup Summary

**One-liner:** RSS_FEEDS und get_sentiment_category() in shared_config.py konsolidiert; tote /api/dashboard und /api/logs Endpoints aus app.py entfernt.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | shared_config.py erstellen und Imports einrichten | 70ef7e6 | shared_config.py (new), app.py, background_worker.py, moodlight_extensions.py |
| 2 | Tote API-Endpoints entfernen | e92792d | app.py |

## What Was Built

**Task 1:** Neue Datei `sentiment-api/shared_config.py` als einzige Definition von `RSS_FEEDS` (12 deutsche Nachrichtenquellen) und `get_sentiment_category()` (Thresholds 0.30/0.10/-0.20/-0.50). Alle drei Consumer importieren ab sofort von dort:
- `app.py`: `from shared_config import RSS_FEEDS, get_sentiment_category` — lokales Dict durch Alias ersetzt, Kategorie-Inline-Logik durch Funktionsaufruf ersetzt
- `background_worker.py`: `from shared_config import RSS_FEEDS, get_sentiment_category` — lokales Dict in `_fetch_headlines()` und `_get_category()` Methode entfernt
- `moodlight_extensions.py`: `from shared_config import get_sentiment_category as get_category_from_score` — lokale Funktion entfernt

**Task 2:** `/api/dashboard` und `/api/logs` Stub-Endpoints aus `app.py` entfernt. Beide URLs geben nun automatisch 404 zurueck.

## Decisions Made

1. **Threshold-Wahl:** Die kanonischen Werte (0.30/0.10/-0.20/-0.50) aus `background_worker.py` und `moodlight_extensions.py` wurden uebernommen. Die abweichenden Werte in `app.py` (0.85/0.2) waren offensichtlich veraltet und wuerden bei Score 0.35 "positiv" statt "sehr positiv" liefern.

2. **rss_feeds Alias:** In `app.py` wird `rss_feeds = RSS_FEEDS` als Modul-Level-Alias behalten, da die `/api/feedconfig` Route die globale Variable `rss_feeds` ueberschreibt. Das erhalt die Funktionalitaet dieser Route.

## Deviations from Plan

None — Plan executed exactly as written.

## Known Stubs

None.

## Self-Check: PASSED

- sentiment-api/shared_config.py: FOUND
- sentiment-api/app.py: modified (shared_config import present, local dict removed)
- sentiment-api/background_worker.py: modified (shared_config import present, _get_category removed)
- sentiment-api/moodlight_extensions.py: modified (shared_config import present, local function removed)
- Commit 70ef7e6: FOUND
- Commit e92792d: FOUND
