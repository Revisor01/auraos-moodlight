---
phase: 07-modul-extraktion
plan: 02
subsystem: firmware/wifi
tags: [wifi, refactor, modularization, esp32]
dependency_graph:
  requires: ["07-01 (app_state.h)"]
  provides: ["wifi_manager.h", "wifi_manager.cpp"]
  affects: ["firmware/src/moodlight.cpp"]
tech_stack:
  added: []
  patterns: ["extern const fuer linkage-sichtbare Konstanten aus C++-Modulen"]
key_files:
  created:
    - firmware/src/wifi_manager.h
    - firmware/src/wifi_manager.cpp
  modified:
    - firmware/src/moodlight.cpp
decisions:
  - "extern const fuer DNS_PORT, MAX_RECONNECT_DELAY, STATUS_LED_GRACE_MS, AP_TIMEOUT in moodlight.cpp — C++ const hat standardmaessig interne Verlinkung, extern macht sie cross-TU sichtbar"
  - "Adafruit_NeoPixel-Header in wifi_manager.cpp inkludiert — startAPMode() braucht pixels direkt fuer LED-Animation"
metrics:
  duration: "8min"
  completed: "2026-03-26T13:09:19Z"
  tasks_completed: 1
  files_changed: 3
---

# Phase 07 Plan 02: WiFi-Manager Extraktion Summary

WiFi-Funktionen (AP-Mode, Captive Portal, Station-Connect, NTP, Reconnect-Backoff) aus moodlight.cpp in eigenstaendiges wifi_manager.h/.cpp Modul extrahiert.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | wifi_manager.h/.cpp erstellen und WiFi-Funktionen verschieben | 84c66e5 | wifi_manager.h, wifi_manager.cpp, moodlight.cpp |

## What Was Built

`wifi_manager.h` deklariert 7 oeffentliche Funktionen:
- `initTime()` — NTP-Zeitsynchronisation
- `safeWiFiConnect()` — Verbindung mit Timeout und Watchdog-yield
- `startAPMode()` — AP-Modus mit LED-Animation (loop-Kontext)
- `startWiFiStation()` — Station-Modus mit NTP-Init nach Connect
- `scanWiFiNetworks()` — JSON-Scan fuer Setup-Seite
- `processDNS()` — Captive Portal DNS-Verarbeitung
- `checkAndReconnectWifi()` — Periodischer Reconnect mit exponentiellem Backoff
- `startAPModeWithServer()` — AP-Modus mit Server-Start (Setup-Phase)

`wifi_manager.cpp` enthaelt zusaetzlich die `CaptiveRequestHandler`-Klasse (innerer RequestHandler fuer WebServer), die nur von `startAPMode()` und `startAPModeWithServer()` benoetigt wird.

`moodlight.cpp` inkludiert jetzt `wifi_manager.h` und enthaelt keine WiFi-Funktionsdefinitionen mehr.

## Decisions Made

1. **extern const fuer linkage-sichtbare Konstanten**: C++ `const`-Variablen haben standardmaessig interne Verlinkung — sie werden nicht vom Linker exportiert. `DNS_PORT`, `MAX_RECONNECT_DELAY`, `STATUS_LED_GRACE_MS` und `AP_TIMEOUT` in moodlight.cpp wurden auf `extern const` geaendert, damit wifi_manager.cpp sie aufloesen kann.

2. **Adafruit_NeoPixel-Include in wifi_manager.cpp**: `startAPMode()` macht direkt `pixels.fill()` und `pixels.show()` fuer die gelbe LED-Animation beim AP-Start. Der Header musste in wifi_manager.cpp eingefuegt werden, da die `extern Adafruit_NeoPixel pixels`-Deklaration den vollstaendigen Typ braucht.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fehlender Adafruit_NeoPixel-Include**
- **Found during:** Task 1 (pio run, erster Build-Versuch)
- **Issue:** `extern Adafruit_NeoPixel pixels` erfordert vollstaendigen Typ — ohne `#include <Adafruit_NeoPixel.h>` Kompilierfehler "does not name a type"
- **Fix:** `#include <Adafruit_NeoPixel.h>` in wifi_manager.cpp hinzugefuegt
- **Files modified:** firmware/src/wifi_manager.cpp
- **Commit:** 84c66e5 (im selben Commit)

**2. [Rule 3 - Blocking] Linker-Fehler durch interne C++-Konstanten**
- **Found during:** Task 1 (pio run, zweiter Build-Versuch)
- **Issue:** `const` in C++ hat interne Verlinkung — `DNS_PORT`, `MAX_RECONNECT_DELAY`, `STATUS_LED_GRACE_MS`, `AP_TIMEOUT` waren fuer den Linker nicht sichtbar, obwohl als `extern` in wifi_manager.cpp deklariert
- **Fix:** In moodlight.cpp auf `extern const` umgestellt
- **Files modified:** firmware/src/moodlight.cpp
- **Commit:** 84c66e5 (im selben Commit)

## Known Stubs

None.

## Self-Check: PASSED

- firmware/src/wifi_manager.h: FOUND
- firmware/src/wifi_manager.cpp: FOUND
- Commit 84c66e5: FOUND
- `grep -c "void checkAndReconnectWifi..."` in wifi_manager.cpp: 5 (4 Hauptfunktionen + startAPModeWithServer)
- `grep -c "..."` in moodlight.cpp: 0 (keine WiFi-Definitionen mehr)
- pio run: SUCCESS
