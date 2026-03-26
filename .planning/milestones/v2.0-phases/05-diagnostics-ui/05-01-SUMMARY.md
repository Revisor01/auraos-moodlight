---
phase: 05-diagnostics-ui
plan: 01
subsystem: web-ui
tags: [html, javascript, ota, update-ui, combined-update]
dependency_graph:
  requires: [04-02-PLAN.md]
  provides: [combined-update-ui]
  affects: [firmware/data/diagnostics.html]
tech_stack:
  added: []
  patterns: [xhr-upload, multi-step-progress, collapsible-details]
key_files:
  created: []
  modified:
    - firmware/data/diagnostics.html
decisions:
  - "Checkpoint:human-verify auto-approved (YOLO/auto mode)"
metrics:
  duration: ~90s
  completed: "2026-03-26"
  tasks_completed: 1
  files_modified: 1
requirements: [UI-01, UI-02, UI-03]
---

# Phase 5 Plan 1: Combined Update UI Summary

**One-liner:** Combined Update UI in diagnostics.html — File-Picker, zweistufige Fortschrittsanzeige (UI dann Firmware), Versionsanzeige via /api/firmware-version, Legacy-Fallback eingeklappt.

## What Was Built

Die `diagnostics.html` wurde um einen vollstaendigen Combined Update Flow erweitert:

1. **Versionsanzeige (UI-03):** `loadVersion()` laedt beim Seitenaufruf die Firmware-Version von `/api/firmware-version` und befuellt `#version` im Header dynamisch.

2. **Combined Update Sektion (UI-01):** Neuer `dashboard-container` mit `id="combined-update-section"` enthaelt:
   - File-Picker fuer `.tgz`/`.tar.gz` Dateien
   - "Full Update starten" Button (initial deaktiviert, wird nach Dateiauswahl aktiviert)
   - Dateiname + Groesse werden bei Auswahl angezeigt

3. **Zweistufige Fortschrittsanzeige (UI-02):**
   - "Schritt 1/2: UI-Installation..." mit XHR-Progress-Bar fuer `/update/combined-ui`
   - "Schritt 2/2: Firmware-Flash..." mit XHR-Progress-Bar fuer `/update/combined-firmware`
   - Bei Firmware-Reboot: Connection-Abbruch wird als Erfolg erkannt
   - Automatischer Page-Reload nach 15 Sekunden nach Erfolg

4. **Legacy-Fallback (UI-01):** Bestehende Einzel-Upload-Inputs fuer `/ui-upload` und `/update` in `<details>`-Element eingeklappt unter "Advanced: Einzelne Uploads".

5. **Dark-Mode CSS:** 2 Zeilen CSS fuer `details summary` und `details > div` im Dark-Mode.

## Tasks

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Combined Update UI implementieren | 7707c87 | firmware/data/diagnostics.html |
| 2 | Visual Verify (Checkpoint) | auto-approved | — |

## Deviations from Plan

None - plan executed exactly as written.

Checkpoint Task 2 (human-verify) wurde automatisch genehmigt (YOLO-Modus).

## Known Stubs

None. Alle UI-Elemente sind mit echten Endpoints verdrahtet (`/api/firmware-version`, `/update/combined-ui`, `/update/combined-firmware`).

## Self-Check: PASSED

- [x] `firmware/data/diagnostics.html` existiert und enthaelt alle required Patterns
- [x] Commit `7707c87` existiert
- [x] `combined-update-section` vorhanden
- [x] `startCombinedUpdate` vorhanden
- [x] `loadVersion` + `/api/firmware-version` vorhanden
- [x] `Schritt 1/2` und `Schritt 2/2` vorhanden
- [x] `<details>` Legacy-Fallback vorhanden
