---
phase: 04-combined-update-handler
plan: 01
subsystem: firmware/build
tags: [esp32, freertos, ota, stream, build-script]
dependency_graph:
  requires: [03-build-fundament-02]
  provides: [ChunkStream.h, VERSION.txt-in-TGZ]
  affects: [build-release.sh, firmware/src/ChunkStream.h]
tech_stack:
  added: [ChunkStream (FreeRTOS Ring-Buffer Stream-Adapter)]
  patterns: [Producer-Consumer mit Binary Semaphores, Ring-Buffer mit wrap-around]
key_files:
  created:
    - firmware/src/ChunkStream.h
  modified:
    - build-release.sh
decisions:
  - "available() gibt (int)_available zurueck (nicht -1 bei leer) — Arduino Stream-Interface erwartet >= 0"
  - "4KB Ring-Buffer als Default — konservativ sicher bei ~320KB ESP32 RAM"
  - "Worktree auf main gemergt um Phase-3-Fixes (WDT-API, build-release.sh) zu erhalten"
metrics:
  duration: "191s"
  completed: "2026-03-26T07:52:35Z"
  tasks_completed: 2
  files_changed: 2
---

# Phase 04 Plan 01: ChunkStream + VERSION.txt Integration Summary

## One-Liner

FreeRTOS Ring-Buffer Stream-Adapter (ChunkStream) und VERSION.txt im Combined-TGZ als Fundament fuer den Combined-Update-Handler in Plan 02.

## What Was Built

### Task 1: ChunkStream-Klasse (firmware/src/ChunkStream.h)

Neue Header-only Datei. ChunkStream erbt von `Stream` (Arduino) und verbindet das Push-Modell des WebServer-Upload-Callbacks mit dem Pull-Modell der ESP32-targz-Library.

Kern-Architektur:
- Ring-Buffer (4KB default, heap-alloziert)
- Zwei Binary Semaphores: `_dataReady` (feed->read) und `_spaceReady` (read->feed)
- `feed()`: Schreibt Chunks blockierend bis Platz vorhanden
- `setEOF()`: Setzt EOF-Flag und weckt wartende Reader auf
- `reset()`: Setzt Buffer + Semaphores zurueck fuer Wiederverwendung
- `readBytes()`: Blockiert bis Daten oder Timeout (10s, passend zu targz_read_timeout)
- `available()`: Gibt `(int)_available` zurueck — korrekte Arduino Stream-Semantik

### Task 2: VERSION.txt in build-release.sh

Drei Aenderungen an build-release.sh (nach dem Phase-3-Stand):
1. Schritt 2b: `echo "${NEW_VERSION}" > /tmp/VERSION.txt` + `tar -rf ... VERSION.txt`
2. Verifikation: `grep -q "VERSION.txt"` Check nach index.html-Check
3. Cleanup: `/tmp/VERSION.txt` in `rm -f` Zeile aufgenommen

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Worktree war hinter main (fehlte Phase-3-Fixes)**
- **Found during:** Task 1 Verifikation (pio run)
- **Issue:** Worktree `agent-acc91d8b` war bei `fa17d23` (pre-Phase-3), `esp_task_wdt_init(30, false)` verursachte Build-Fehler
- **Fix:** `git merge main --no-edit` — fast-forward auf `5326072` mit WDT-Fix und neuem build-release.sh
- **Files modified:** firmware/src/moodlight.cpp (WDT-Fix), build-release.sh (Combined-TGZ-Script)
- **Commit:** Merge (fast-forward, kein separater Commit)

## Commits

| Task | Commit | Message |
|------|--------|---------|
| Task 1 | c172e04 | feat(04-01): ChunkStream-Klasse implementieren |
| Task 2 | a089534 | feat(04-01): VERSION.txt ins Combined-TGZ aufnehmen |

## Verification Results

- `pio run` im firmware-Verzeichnis: SUCCESS (67.8% Flash, 28.3% RAM)
- `grep -c "VERSION.txt" build-release.sh`: 7 (> 4 required)
- Alle Acceptance Criteria: PASS

## Known Stubs

None — ChunkStream ist vollstaendig implementiert und bereit fuer Plan 02. VERSION.txt wird korrekt ins TGZ aufgenommen.

## Self-Check: PASSED
