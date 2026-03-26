---
phase: 04-combined-update-handler
plan: 02
subsystem: firmware/ota
tags: [esp32, freertos, ota, stream, firmware-update]
dependency_graph:
  requires: [04-01]
  provides: [handleCombinedUiUpload, handleCombinedFirmwareUpload, /update/combined-ui, /update/combined-firmware]
  affects: [firmware/src/moodlight.cpp, firmware/src/ChunkStream.h]
tech_stack:
  added: [FreeRTOS multi-core task pattern for OTA streaming]
  patterns: [tarGzStreamExpander mit ChunkStream, tarGzStreamUpdater mit ChunkStream, WDT-Deaktivierung fuer langwierige OTA-Operationen]
key_files:
  created: []
  modified:
    - firmware/src/moodlight.cpp
    - firmware/src/ChunkStream.h
decisions:
  - "FreeRTOS-Task auf Core 0 fuer UI-Extraktion und Firmware-Flash — verhindert Deadlock durch synchrones tarGzStreamExpander/Updater-Blocking auf WebServer-Core"
  - "Geteilte globale State-Variablen fuer beide Handler (combinedStream, extractTaskHandle, etc.) — sicher da Browser-UI sequentiell arbeitet"
  - "tarGzStreamUpdater braucht keinen Exclude-Filter — erkennt .ino.bin automatisch via tarHeaderUpdateCallBack, andere Dateien werden silent uebersprungen"
  - "VERSION.txt wird nach UI-Upload gelesen und auf firmware-version.txt + ui-version.txt verteilt — kein LittleFS-Zugriff im Firmware-Handler noetig"
metrics:
  duration: "~150s"
  completed: "2026-03-26T07:59:33Z"
  tasks_completed: 2
  files_changed: 2
---

# Phase 04 Plan 02: Combined Update Handler Summary

## One-Liner

Zwei neue OTA-HTTP-Endpoints (`/update/combined-ui`, `/update/combined-firmware`) mit FreeRTOS-Task-Streaming ueber ChunkStream in tarGzStreamExpander und tarGzStreamUpdater.

## What Was Built

### Task 1: Combined-UI Upload-Handler (OTA-01, OTA-03, OTA-04)

Neue globale State-Variablen vor `handleUiUpload()`:
- `combinedUiError`, `combinedFwError`, `extractTaskSuccess` — Flag-State fuer beide Handler
- `combinedStream` (ChunkStream*), `extractTaskHandle` (TaskHandle_t), `extractDoneSemaphore` (SemaphoreHandle_t)

Neue Funktion `uiExtractTask(void* param)` (FreeRTOS-Task, Core 0):
- Erstellt `TarGzUnpacker`, konfiguriert Callbacks
- Setzt `setTarExcludeFilter` der `.bin`-Dateien ueberspringt (OTA-01)
- Ruft `tarGzStreamExpander(stream, LittleFS, "/")` auf — extrahiert direkt nach LittleFS-Root
- Signalisiert Completion via `xSemaphoreGive(extractDoneSemaphore)`

Neue Funktion `handleCombinedUiUpload()` (WebServer-Handler):
- `UPLOAD_FILE_START`: State-Reset, ChunkStream-Allokation/Reset, FreeRTOS-Task starten auf Core 0, WDT deaktivieren
- `UPLOAD_FILE_WRITE`: `combinedStream->feed()` fuer jeden Chunk
- `UPLOAD_FILE_END`: `combinedStream->setEOF()`, Semaphore-Wait (60s Timeout), VERSION.txt lesen + verteilen auf firmware-version.txt + ui-version.txt (OTA-03), WDT reaktivieren
- `UPLOAD_FILE_ABORTED`: Sauberer State-Reset inkl. Task-Terminate (OTA-04)

Route `/update/combined-ui` registriert nach bestehender `/ui-upload` Route.

### Task 2: Combined-Firmware Upload-Handler (OTA-02, OTA-04)

Neue Funktion `firmwareFlashTask(void* param)` (FreeRTOS-Task, Core 0):
- Analog zu `uiExtractTask` aber ohne Exclude-Filter
- Ruft `tarGzStreamUpdater(stream)` auf — flasht `.ino.bin` automatisch in OTA-Partition

Neue Funktion `handleCombinedFirmwareUpload()` (WebServer-Handler):
- Analog zu `handleCombinedUiUpload`, aber 120s Timeout (statt 60s) fuer Flash
- `setStatusLED(3)` fuer visuelles Update-Feedback
- Kein VERSION.txt-Handling — wurde bereits im UI-Pass erledigt
- WDT wird nach UPLOAD_FILE_END NICHT reaktiviert (ESP wird sowieso restartet)

Route `/update/combined-firmware` registriert mit `ESP.restart()` nach Erfolg.

**Bestehende Endpoints `/ui-upload` und `/update` bleiben als Fallback unberuehrt.**

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] ChunkStream min() Typ-Mismatch bei volatile size_t**
- **Found during:** Task 1 Verifikation (pio run)
- **Issue:** `min(len - totalRead, _available)` schlug fehl weil `_available` `volatile size_t` ist, aber `len - totalRead` plain `size_t` — GCC konnte Template-Argument nicht deduzieren
- **Fix:** Cast zu `(size_t)_available` in `readBytes()` in ChunkStream.h
- **Files modified:** firmware/src/ChunkStream.h
- **Commit:** 268fcd8 (zusammen mit Task 1)

## Commits

| Task | Commit | Message |
|------|--------|---------|
| Task 1 + Rule1-Fix | 268fcd8 | feat(04-02): Combined-UI Upload-Handler implementieren |
| Task 2 | 3423fad | feat(04-02): Combined-Firmware Upload-Handler implementieren |

## Verification Results

- `pio run` nach Task 1: SUCCESS (68.0% Flash, 28.3% RAM)
- `pio run` nach Task 2: SUCCESS (68.1% Flash, 28.3% RAM)
- Alle Acceptance Criteria Task 1: PASS
- Alle Acceptance Criteria Task 2: PASS
- Bestehende Endpoints `/ui-upload` und `/update`: unberuehrt (Fallback erhalten)

## Known Stubs

None — beide Handler sind vollstaendig implementiert. Die Streaming-Architektur via ChunkStream + FreeRTOS-Task ist betriebsbereit.

## Self-Check: PASSED
