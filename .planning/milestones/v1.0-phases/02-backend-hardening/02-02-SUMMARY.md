---
phase: 02-backend-hardening
plan: 02
subsystem: backend
tags: [gunicorn, production-server, timeouts, flask, requests]
dependency_graph:
  requires: [02-01]
  provides: [gunicorn-production-server, per-connection-timeouts]
  affects: [sentiment-api/Dockerfile, sentiment-api/app.py, sentiment-api/background_worker.py]
tech_stack:
  added: [gunicorn==25.2.0]
  patterns: [requests.get(timeout=15) statt socket.setdefaulttimeout(), Gunicorn -w 1 --threads 4]
key_files:
  created: []
  modified:
    - sentiment-api/Dockerfile
    - sentiment-api/requirements.txt
    - sentiment-api/app.py
    - sentiment-api/background_worker.py
decisions:
  - "Gunicorn -w 1 --threads 4 — Background Worker laeuft im Prozess, kein Multi-Worker"
  - "requests.get(timeout=15) als Ersatz fuer socket.setdefaulttimeout() — per-Connection statt global"
metrics:
  duration: 152s
  completed_date: "2026-03-25"
  tasks_completed: 2
  files_modified: 4
---

# Phase 02 Plan 02: Gunicorn-Migration und Per-Connection Timeouts Summary

## One-Liner

Gunicorn als Production-Server mit `-w 1 --threads 4`, Background Worker auf Modul-Level verschoben und `socket.setdefaulttimeout()` durch `requests.get(timeout=15)` in beiden Feed-Fetch-Funktionen ersetzt.

## What Was Built

- **Gunicorn als Production-Server**: `Dockerfile` CMD umgestellt von `python app.py` auf `gunicorn -w 1 --threads 4 -b 0.0.0.0:6237 --timeout 120 --access-logfile - app:app`
- **gunicorn==25.2.0** in `requirements.txt` hinzugefuegt
- **Background Worker auf Modul-Level**: `start_background_worker()` aus dem `if __name__ == '__main__':` Block herausgeloest — Gunicorn fuehrt `__main__` nicht aus, daher muss der Aufruf auf Modul-Level stehen
- **Per-Connection Timeouts in app.py**: `socket.setdefaulttimeout()` und der Ruecksetze-Block entfernt; `feedparser.parse(url)` durch `requests.get(url, timeout=15)` + `feedparser.parse(response.content)` ersetzt; `import socket` entfernt
- **Per-Connection Timeouts in background_worker.py**: Identische Transformation — `socket.setdefaulttimeout()` entfernt, `requests.get(timeout=15)` eingefuehrt

## Commits

| Task | Commit | Message |
|------|--------|---------|
| 1: Gunicorn-Migration | decc17c | feat(02-02): Gunicorn-Migration und Background-Worker auf Modul-Level |
| 2: Per-Connection Timeouts | 303f3d5 | fix(02-02): socket.setdefaulttimeout() durch requests.get(timeout=15) ersetzen |

## Deviations from Plan

None - Plan executed exactly as written.

## Known Stubs

None.

## Self-Check: PASSED

- sentiment-api/Dockerfile: FOUND, contains gunicorn CMD with -w 1
- sentiment-api/requirements.txt: FOUND, contains gunicorn==25.2.0
- sentiment-api/app.py: FOUND, start_background_worker on module level, no setdefaulttimeout
- sentiment-api/background_worker.py: FOUND, requests.get(timeout=15), no setdefaulttimeout
- Commit decc17c: FOUND
- Commit 303f3d5: FOUND
