---
phase: 08-konsolidierung-qualitaet
plan: "01"
subsystem: firmware
tags: [refactor, config, dead-code, magic-numbers]
dependency_graph:
  requires: []
  provides: [zentralisierte-timing-konstanten, bereinigtes-moodlight-cpp]
  affects: [firmware/src/config.h, firmware/src/moodlight.cpp, firmware/src/wifi_manager.cpp, firmware/src/mqtt_handler.cpp, firmware/src/web_server.cpp, firmware/src/sensor_manager.cpp]
tech_stack:
  added: []
  patterns: [config.h als einzige Wahrheitsquelle fuer Timing-Konstanten, #define statt extern const]
key_files:
  created: []
  modified:
    - firmware/src/config.h
    - firmware/src/moodlight.cpp
    - firmware/src/wifi_manager.cpp
    - firmware/src/mqtt_handler.cpp
    - firmware/src/web_server.cpp
    - firmware/src/sensor_manager.cpp
decisions:
  - "SOFTWARE_VERSION bleibt als extern const String in moodlight.cpp — String-Literale brauchen externe Verlinkung fuer cross-TU Sichtbarkeit"
  - "MAX_RECONNECT_DELAY #define benoetigt (unsigned long) Cast in min() — #define hat keinen Typ, implicit int-Promotion verursacht Template-Deduktionsfehler"
  - "CONFIG_FREERTOS_UNICORE Block umgewandelt statt entfernt — funktionaler Stack-Monitoring-Code war nur durch auskommentierten Define gegated"
metrics:
  duration: "8min"
  completed: "2026-03-26T14:10:07Z"
  tasks: 2
  files: 6
---

# Phase 08 Plan 01: Magic Numbers → config.h + Dead Code entfernen Summary

Alle verstreuten `extern const`-Timing-Definitionen in `moodlight.cpp` durch `#define`-Konstanten in `config.h` ersetzt. Tote Kommentarblöcke (v9.0-Referenzen, Phase-06-Migrationskommentare) und auskommentierten Code aus `moodlight.cpp` entfernt.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Magic Numbers nach config.h verschieben | 507619c | config.h, moodlight.cpp, wifi_manager.cpp, mqtt_handler.cpp, web_server.cpp, sensor_manager.cpp |
| 2 | Dead Code und v9.0-Kommentarreste entfernen | b6b4fc5 | moodlight.cpp |

## What Was Built

**config.h:** 22 neue `#define`-Konstanten hinzugefügt — vollständige Abdeckung aller Timing-Werte (STARTUP_GRACE_PERIOD, MAX_RECONNECT_DELAY, MQTT_HEARTBEAT_INTERVAL, SENTIMENT_FALLBACK_TIMEOUT, HEALTH_CHECK_INTERVAL, HEALTH_CHECK_SHORT_INTERVAL, alle LOOP_*_MS Werte, System-Health-Schwellwerte).

**moodlight.cpp:** 11 `extern const`-Definitionen entfernt (DNS_PORT, LOG_BUFFER_SIZE, MAX_RECONNECT_DELAY, STATUS_LED_GRACE_MS, MQTT_HEARTBEAT_INTERVAL, SENTIMENT_FALLBACK_TIMEOUT, MAX_SENTIMENT_FAILURES, STATUS_LOG_INTERVAL, AP_TIMEOUT, REBOOT_DELAY, HEALTH_CHECK_INTERVAL). NTP-Variablen entfernt (war bereits direkt in wifi_manager.cpp). Header auf aktuelle Version aktualisiert. Alle v9.0- und Migrationskommentare entfernt.

**Module:** `extern const`-Deklarationen aus wifi_manager.cpp (4), mqtt_handler.cpp (1), web_server.cpp (3), sensor_manager.cpp (2) entfernt — Konstanten jetzt über config.h → app_state.h Include-Kette verfügbar.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Typ-Mismatch bei MAX_RECONNECT_DELAY in min()**
- **Found during:** Task 1 — erster Build-Versuch
- **Issue:** `min(appState.wifiReconnectDelay * 2, MAX_RECONNECT_DELAY)` — `#define 300000` ist `int`, `wifiReconnectDelay` ist `unsigned long`, C++ Template-Deduktion schlägt fehl
- **Fix:** `(unsigned long)MAX_RECONNECT_DELAY` Cast in wifi_manager.cpp Zeile 325
- **Files modified:** firmware/src/wifi_manager.cpp
- **Commit:** 507619c (Teil des Task-1-Commits)

### Plan-Abweichungen

**SOFTWARE_VERSION nicht eliminiert:** Plan schlug vor, `SOFTWARE_VERSION` durch direktes `MOODLIGHT_FULL_VERSION` in web_server.cpp zu ersetzen. Da `MOODLIGHT_FULL_VERSION` ein String-Literal-Concat-Makro ist, nicht ein `String`-Objekt, würden alle Verwendungen als `String(SOFTWARE_VERSION)` ohne Änderung kompilieren. `extern const String SOFTWARE_VERSION` in moodlight.cpp mit extern-Verlinkung beibehalten — sauberste Lösung ohne Änderung in allen 5 Verwendungsstellen in web_server.cpp.

## Verification Results

```
grep -c "extern const" firmware/src/moodlight.cpp  → 1  (nur SOFTWARE_VERSION)
grep -c "v9.0" firmware/src/moodlight.cpp           → 0
grep -c "migriert" firmware/src/moodlight.cpp       → 0
grep -c "#define.*INTERVAL|...|MS" config.h         → 22  (> 10 ✓)
pio run                                              → SUCCESS
```

## Known Stubs

None.

## Self-Check: PASSED
