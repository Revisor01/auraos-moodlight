---
phase: 03-build-fundament
plan: 02
subsystem: build-tooling
tags: [build-script, version-bump, combined-tgz, release-automation]
dependency_graph:
  requires: [03-01]
  provides: [build-release.sh]
  affects: [firmware/src/config.h, releases/]
tech_stack:
  added: []
  patterns: [bash-set-e-trap, two-step-tar-gzip-macos, esp32-targz-naming-convention]
key_files:
  created: []
  modified:
    - build-release.sh
decisions:
  - "BSD tar (macOS): --exclude Flags muessen vor den Dateien stehen — not nach (GNU tar erlaubt beides)"
  - "WDT-Fix aus 03-01 muss im Worktree vorhanden sein vor erstem Build-Test"
metrics:
  duration: 222s
  completed: "2026-03-26T07:26:59Z"
  tasks_completed: 2
  files_modified: 1
requirements:
  - BUILD-01
  - BUILD-02
  - BUILD-03
  - BUILD-04
---

# Phase 03 Plan 02: Build-Release-Script Summary

**One-liner:** build-release.sh komplett ersetzt — major/minor Version-Bump, Combined-TGZ mit UI+firmware.ino.bin, Auto-Commit nach erfolgreichem pio run, Rollback bei Build-Fehler.

## What Was Done

Bestehendes build-release.sh (5 Bugs: kein Version-Bump-Argument, kein Combined-TGZ, kein Auto-Commit, `|| true` verschluckte Fehler, kein Empty-VERSION-Guard) wurde komplett durch ein neues Script ersetzt.

**Task 1: Script-Ersatz**
- Argument-Validierung: nur `major`/`minor` akzeptiert, alle anderen Argumente → Exit 1
- Version-Bump-Logik: liest `MOODLIGHT_VERSION` aus `firmware/src/config.h`, bumpt, schreibt zurück
- `set -e` + `trap rollback ERR`: Rollback der config.h bei Build-Fehler
- `trap cleanup EXIT`: /tmp-Dateien werden aufgeräumt
- `pio run` direkt (kein `|| true`, kein Fehler-Unterdrücken)
- Empty-VERSION-Guard verhindert silent fail wenn grep nichts findet
- `sed -i ''` für macOS BSD sed Kompatibilität

**Task 2: Smoke-Test**
- Build erfolgreich durchgeführt (9.1 -> 9.2, da vorheriger Rollback config.h auf 9.1 gelassen hatte)
- Combined-TGZ `AuraOS-9.2.tgz` (825K) enthält: index.html, setup.html, mood.html, diagnostics.html, js/, css/, firmware.ino.bin
- Auto-Commit "Release: AuraOS v9.2" von Script erstellt
- Usage-Fehler (kein Argument / ungültiges Argument) beides mit Exit 1 bestätigt

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking Issue] WDT-Fix aus 03-01 nicht im Worktree**
- **Found during:** Task 2 (erster Build-Versuch)
- **Issue:** `esp_task_wdt_init(timeoutSeconds, panicOnTimeout)` inkompatibel mit Arduino ESP32 Core 3.x — identischer Pre-existing-Bug den 03-01 gefixt hat, aber der Fix war nicht in diesem Worktree
- **Fix:** Cherry-pick von Commit `0fe4221` (03-01 WDT-Fix) ins Worktree, dann Build erneut
- **Files modified:** firmware/src/MoodlightUtils.cpp, firmware/src/MoodlightUtils.h, firmware/src/moodlight.cpp
- **Commit:** ba2d804 (als Teil des Auto-Commits "Release: AuraOS v9.2")

**2. [Rule 1 - Bug] macOS BSD tar --exclude Argument-Reihenfolge**
- **Found during:** Task 2 (zweiter Build-Versuch)
- **Issue:** `tar: --exclude=*.tmp.*: Cannot stat: No such file or directory` — BSD tar auf macOS erwartet `--exclude` Flags VOR den zu archivierenden Dateien, nicht danach (GNU tar erlaubt beides)
- **Fix:** `--exclude` Flags in build-release.sh vor die Dateiliste verschoben
- **Files modified:** build-release.sh
- **Commit:** f961222

**3. [Deviation] Version 9.1 -> 9.2 statt 9.0 -> 9.1**
- **Grund:** Erster fehlgeschlagener Build-Versuch hatte Rollback-Pfad-Fehler (`sed -i ''` im falschen CWD nach `cd firmware`). config.h zeigte danach 9.1 statt 9.0.
- **Auswirkung:** Smoke-Test lief auf 9.1 -> 9.2 statt 9.0 -> 9.1. Funktionalität identisch — alle Kriterien erfüllt.

## Commits

| Hash | Message |
|------|---------|
| 19d21df | feat(03-02): replace build-release.sh with combined TGZ builder |
| ba2d804 | Release: AuraOS v9.2 (Auto-Commit durch Script) |
| f961222 | fix(03-02): BSD tar --exclude Reihenfolge fuer macOS Kompatibilitaet |

## Known Stubs

None.

## Success Criteria Verification

- [x] build-release.sh akzeptiert major/minor und lehnt andere Argumente ab (BUILD-01)
- [x] Combined-TGZ enthaelt UI-Dateien UND firmware.ino.bin (BUILD-02)
- [x] Git commit erfolgt automatisch nach erfolgreichem Build (BUILD-03)
- [x] Kein `|| true` im Script, pio run bricht bei Fehler ab (BUILD-04)
- [x] Version-Rollback bei Build-Fehler funktioniert (trap ERR)

## Self-Check: PASSED

- FOUND: 03-02-SUMMARY.md
- FOUND: build-release.sh
- FOUND: releases/v9.2/AuraOS-9.2.tgz
- FOUND: commit 19d21df
- FOUND: commit ba2d804
- FOUND: commit f961222
