---
phase: 07-modul-extraktion
plan: "06"
subsystem: firmware/web-server
tags: [extraction, web-server, module, refactoring]
dependency_graph:
  requires: [07-05, 07-04, 07-03, 07-02, 07-01]
  provides: [web_server.h, web_server.cpp]
  affects: [firmware/src/moodlight.cpp]
tech_stack:
  added: []
  patterns: [extern-const cross-TU, JsonBufferPool isolation, initJsonPool wrapper]
key_files:
  created:
    - firmware/src/web_server.h
    - firmware/src/web_server.cpp
  modified:
    - firmware/src/moodlight.cpp
decisions:
  - "extern const fuer REBOOT_DELAY/LOG_BUFFER_SIZE/STATUS_LOG_INTERVAL/SOFTWARE_VERSION — C++ const hat interne Verlinkung, extern macht Konstanten cross-TU sichtbar"
  - "initJsonPool() wrapper in web_server.h — JsonBufferPool struct bleibt intern in web_server.cpp, kein Struct-Typ im Header"
  - "DEST_FS_USES_LITTLEFS + ESP32-targz aus moodlight.cpp entfernt — nur noch in web_server.cpp benoetigt"
metrics:
  duration: "16min"
  completed: "2026-03-26T13:48:07Z"
  tasks_completed: 1
  files_changed: 3
---

# Phase 07 Plan 06: web_server.h/.cpp Extraktion Summary

Web-Server-Modul aus moodlight.cpp extrahiert: setupWebServer (~1170 Zeilen), alle API-Handler, FileHandler, UiUpload, JsonBufferPool und Datei-Hilfsfunktionen in web_server.h/.cpp verschoben — moodlight.cpp von 2590 auf 677 Zeilen reduziert.

## What Was Built

- `firmware/src/web_server.h`: Header mit Funktionsdeklarationen fuer initFS, handleStaticFile, setupWebServer, handleApiStatus, handleApiDeleteDataPoint, handleApiResetAllData, handleApiBackendStats, handleUiUpload, getStorageInfo, getCurrentUiVersion, getCurrentFirmwareVersion, logSystemStatus, initJsonPool
- `firmware/src/web_server.cpp`: Vollstaendige Implementierung aller Web-Server-Funktionen inkl. JsonBufferPool struct, jsonPool Instanz, JsonBufferGuard, Datei-Hilfsfunktionen (copyFile, moveFile, copyDir, deleteDir), handleApiStorageInfo, handleApiStats
- `firmware/src/moodlight.cpp`: Reduziert auf setup(), loop(), debug(), Globals, Konstanten und Utility-Instanzen (677 Zeilen)

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | web_server.h/.cpp erstellen und Web-Server-Code verschieben | 1c7a908 | firmware/src/web_server.h, web_server.cpp, moodlight.cpp |

## Decisions Made

1. **extern const fuer REBOOT_DELAY, LOG_BUFFER_SIZE, STATUS_LOG_INTERVAL, SOFTWARE_VERSION** — C++ `const` hat interne Verlinkung; ohne `extern` sind diese Konstanten in anderen Translation Units undefiniert. Konsistentes Muster aus Phase 07 (bereits bei MAX_RECONNECT_DELAY, AP_TIMEOUT, MQTT_HEARTBEAT_INTERVAL etc. angewendet).

2. **initJsonPool() Wrapper-Funktion** — JsonBufferPool struct und jsonPool-Instanz bleiben intern in web_server.cpp (nicht im Header exponiert). Der `jsonPool.init()` Aufruf in setup() wird durch `initJsonPool()` ersetzt — kleaneres Interface ohne Struct-Leak.

3. **DEST_FS_USES_LITTLEFS + ESP32-targz aus moodlight.cpp entfernt** — Define und Include wurden nur fuer handleUiUpload() benoetigt. Beide liegen jetzt ausschliesslich in web_server.cpp wo sie hingehoeren.

## Deviations from Plan

None - Plan executed exactly as written.

The extern const pattern for REBOOT_DELAY, LOG_BUFFER_SIZE, STATUS_LOG_INTERVAL, and SOFTWARE_VERSION is consistent with the approach established in earlier tasks of this phase (AP_TIMEOUT, MQTT_HEARTBEAT_INTERVAL, SENTIMENT_FALLBACK_TIMEOUT etc.) and was therefore anticipated.

## Verification

- `grep -c "void setupWebServer\|void handleStaticFile\|void handleApiStatus\|void handleUiUpload\|void initFS" firmware/src/web_server.cpp` → 5
- `grep "JsonBufferPool" firmware/src/web_server.cpp` → struct definition found
- `wc -l firmware/src/moodlight.cpp` → 677 lines (target: under 1000)
- `pio run` → SUCCESS (25.07 seconds)
- All 6 modules exist: settings_manager, wifi_manager, led_controller, sensor_manager, mqtt_handler, web_server

## Known Stubs

None.

## Self-Check: PASSED

- firmware/src/web_server.h: FOUND
- firmware/src/web_server.cpp: FOUND
- firmware/src/moodlight.cpp: FOUND (677 lines)
- Commit 1c7a908: FOUND
