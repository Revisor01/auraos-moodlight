---
phase: 01-firmware-stabilit-t
plan: 01
subsystem: firmware
tags: [buffer-overflow, memory-leak, led-array, json-pool, raii, esp32]
dependency_graph:
  requires: []
  provides: [MAX_LEDS, statusLedIndex, JsonBufferGuard, release-fix]
  affects: [firmware/src/config.h, firmware/src/moodlight.cpp]
tech_stack:
  added: []
  patterns: [RAII-Guard fuer JSON-Buffer, constrain() fuer Bounds-Checking]
key_files:
  created: []
  modified:
    - firmware/src/config.h
    - firmware/src/moodlight.cpp
decisions:
  - "MAX_LEDS auf 64 gesetzt — deckt alle realistischen LED-Strip-Groessen ab ohne exzessiven RAM-Verbrauch"
  - "statusLedIndex als dynamische Variable statt Konstante — ermoeglicht korrekte Position auch bei nicht-12 LEDs"
  - "delete[] ausserhalb Mutex-Block — IMMER ausfuehren, unabhaengig ob Mutex-Timeout aufgetreten ist"
  - "JsonBufferGuard nach jsonPool-Deklaration platziert — direkter Zugriff auf globale Variable ohne Forward-Declaration"
metrics:
  duration: 311s
  completed: "2026-03-25"
  tasks_completed: 2
  files_modified: 2
---

# Phase 01 Plan 01: Buffer-Overflow Fix und JSON Memory-Leak Fix Summary

**One-liner:** Buffer-Overflow-Schutz fuer LED-Array (MAX_LEDS=64, constrain) und Memory-Leak-Fix fuer JSON-Pool (delete[] ausserhalb Mutex-Block) mit JsonBufferGuard RAII-Klasse.

## Tasks Completed

| Task | Description | Commit | Status |
|------|-------------|--------|--------|
| 1 | LED-01: Buffer-Overflow Fix und dynamischer statusLedIndex | 4c33c75 | Done |
| 2 | MEM-01: JsonBufferPool release() Fix und RAII Guard | bcff9f4 | Done |

## What Was Built

### Task 1: LED-01
- `firmware/src/config.h`: `#define MAX_LEDS 64` nach `DEFAULT_NUM_LEDS` hinzugefuegt
- `firmware/src/moodlight.cpp`: `ledColors[DEFAULT_NUM_LEDS]` zu `ledColors[MAX_LEDS]` geaendert
- `firmware/src/moodlight.cpp`: `const int STATUS_LED_INDEX = DEFAULT_NUM_LEDS - 1` ersetzt durch `int statusLedIndex = DEFAULT_NUM_LEDS - 1`
- Alle 4 Verwendungsstellen von `STATUS_LED_INDEX` durch `statusLedIndex` ersetzt (Zeilen ~504, ~547, ~550, ~3858)
- `numLeds = constrain(..., 1, MAX_LEDS)` + `statusLedIndex = numLeds - 1` in JSON-Load (loadSettingsFromJSON)
- `numLeds = constrain(..., 1, MAX_LEDS)` + `statusLedIndex = numLeds - 1` in Preferences-Load (loadSettings Fallback)

### Task 2: MEM-01
- `release()` Bug gefixt: `delete[] buffer` steht jetzt AUSSERHALB des Mutex-Blocks
- `if (!buffer) return;` als Null-Guard am Anfang von `release()` hinzugefuegt
- `JsonBufferGuard` RAII-Klasse nach `jsonPool`-Deklaration hinzugefuegt
- Klasse hat deleted copy-Konstruktor und copy-Assignment Operator

## Verification Results

### Automated Checks
- `MAX_LEDS` in config.h: PASS
- `ledColors[MAX_LEDS]` in moodlight.cpp: PASS
- `statusLedIndex` vorhanden: PASS
- `STATUS_LED_INDEX` entfernt (0 Vorkommen): PASS
- 2x `constrain.*MAX_LEDS` in moodlight.cpp: PASS

### Build Verification
`pio run` kompiliert NICHT ohne Fehler — aber der Fehler ist pre-existing und nicht durch diesen Plan verursacht:

```
src/moodlight.cpp:4039:23: error: invalid conversion from 'int' to 'const esp_task_wdt_config_t*'
    esp_task_wdt_init(30, false);
```

Dies ist ein API-Inkompatibilitaetsfehler durch die Arduino ESP32 Core-Version (bekannter Blocker aus STATE.md: "Arduino ESP32 Core-Version in platformio.ini pruefen"). Die Zeile 4039 (`esp_task_wdt_init(30, false)`) ist unveraenderter pre-existing Code. Meine Aenderungen betreffen ausschliesslich Array-Groessen, eine Variablen-Deklaration und die release()-Methode — keiner dieser Bereiche steht in der Compiler-Fehlerausgabe.

## Deviations from Plan

### Pre-existing Build Failure (Out of Scope)
- **Found during:** Gesamt-Verifikation
- **Issue:** `esp_task_wdt_init(30, false)` API-Signatur inkompatibel mit installierter Arduino ESP32 Core-Version (3.x)
- **Scope:** Pre-existing vor diesem Plan, in STATE.md als bekannter Blocker dokumentiert
- **Action:** In deferred-items.md dokumentiert, nicht gefixt (ausserhalb dieses Plans)
- **Impact auf Plan-Ziele:** Null — die LED-Buffer und Memory-Leak Fixes sind korrekt implementiert und syntaktisch valide

None — plan executed exactly as written regarding LED-01 and MEM-01 requirements.

## Known Stubs

None — alle implementierten Aenderungen sind vollstaendig und funktional.

## Self-Check: PASSED

- [x] `firmware/src/config.h` enthaelt `MAX_LEDS 64` ✓
- [x] `firmware/src/moodlight.cpp` enthaelt `ledColors[MAX_LEDS]` ✓
- [x] `statusLedIndex` vorhanden, `STATUS_LED_INDEX` entfernt ✓
- [x] 2x `constrain(..., 1, MAX_LEDS)` mit `statusLedIndex = numLeds - 1` ✓
- [x] `JsonBufferGuard` Klasse mit `char* buf`, Konstruktor, Destruktor ✓
- [x] `delete[] buffer` ausserhalb Mutex-Block ✓
- [x] Commit 4c33c75 vorhanden ✓
- [x] Commit bcff9f4 vorhanden ✓
