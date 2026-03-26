---
gsd_state_version: 1.0
milestone: v2.0
milestone_name: Combined Update + Build Automation
status: Phase complete — ready for verification
stopped_at: Completed 04-combined-update-handler-02-PLAN.md
last_updated: "2026-03-26T08:00:49.883Z"
progress:
  total_phases: 3
  completed_phases: 2
  total_plans: 4
  completed_plans: 4
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-26)

**Core value:** Ein einziger Klick baut ein Combined Update (UI + Firmware), bumpt die Version und committet.
**Current focus:** Phase 04 — combined-update-handler

## Current Position

Phase: 04 (combined-update-handler) — EXECUTING
Plan: 2 of 2

## Performance Metrics

**Velocity:**

- Total plans completed: 0
- Average duration: —
- Total execution time: —

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| - | - | - | - |

*Updated after each plan completion*
| Phase 03-build-fundament P01 | 5min | 1 tasks | 3 files |
| Phase 03-build-fundament P02 | 222s | 2 tasks | 1 files |
| Phase 04 P01 | 191s | 2 tasks | 2 files |
| Phase 04 P02 | 150s | 2 tasks | 2 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [v2.0 Roadmap]: Two-Route-Architektur (`/update/combined-ui` + `/update/combined-firmware`) — kein LittleFS-Staging der Firmware wegen 128 KB Limit
- [v2.0 Roadmap]: `tarGzStreamUpdater` mit `.ino.bin`-Naming-Convention statt eigenem Firmware-Handler
- [v2.0 Roadmap]: FIX-01 ist absoluter Blocker — Phase 3 muss mit `esp_task_wdt_init`-Fix beginnen
- [Phase 03-build-fundament]: esp_task_wdt_reconfigure nicht verwendet — gibt ESP_ERR_INVALID_STATE im setup()-Kontext zurueck
- [Phase 03-build-fundament]: Conditional-Compile-Pattern mit ESP_IDF_VERSION_VAL fuer ESP-IDF 5.x Rueckwaertskompatibilitaet
- [Phase 03-build-fundament]: macOS BSD tar: --exclude Flags muessen vor Dateien stehen (GNU tar erlaubt beides)
- [Phase 04]: available() gibt (int)_available zurueck — Arduino Stream-Interface erwartet >= 0, nicht -1
- [Phase 04]: ChunkStream 4KB Ring-Buffer als Default — konservativ sicher bei ~320KB ESP32 RAM
- [Phase 04]: FreeRTOS-Task auf Core 0 fuer OTA-Streaming — verhindert Deadlock durch synchrones tarGzStreamExpander-Blocking auf WebServer-Core
- [Phase 04]: tarGzStreamUpdater ohne Exclude-Filter — erkennt .ino.bin automatisch via tarHeaderUpdateCallBack
- [Phase 04]: VERSION.txt nur im UI-Handler lesen und verteilen — Firmware-Handler greift nicht auf LittleFS zu

### Pending Todos

None yet.

### Blockers/Concerns

- [Phase 3]: FIX-01 muss als allererstes umgesetzt werden — ohne `pio run` kein Build-Script-Test möglich
- [Phase 4]: `tarGzStreamUpdater` erwartet kontinuierlichen `Stream*`; ESP8266WebServer liefert Chunks — ChunkStream-Wrapper nötig, vor vollem Handler prototypisch validieren

## Session Continuity

Last session: 2026-03-26T08:00:49.880Z
Stopped at: Completed 04-combined-update-handler-02-PLAN.md
Resume file: None
