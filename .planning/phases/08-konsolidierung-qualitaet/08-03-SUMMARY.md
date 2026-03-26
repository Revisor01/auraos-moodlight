---
phase: 08-konsolidierung-qualitaet
plan: 03
subsystem: firmware-core
tags: [refactoring, modularization, led-controller, web-server, wifi-manager, mqtt-handler]
dependency_graph:
  requires: ["08-02"]
  provides: ["moodlight.cpp <= 200 Zeilen", "reine Orchestrierungsdatei"]
  affects: ["firmware/src/moodlight.cpp", "firmware/src/led_controller", "firmware/src/web_server", "firmware/src/mqtt_handler", "firmware/src/wifi_manager"]
tech_stack:
  added: []
  patterns: ["Extraktion in Modul-Funktionen", "Orchestrierungsmuster in setup()/loop()"]
key_files:
  created: []
  modified:
    - firmware/src/moodlight.cpp
    - firmware/src/led_controller.h
    - firmware/src/led_controller.cpp
    - firmware/src/web_server.h
    - firmware/src/web_server.cpp
    - firmware/src/mqtt_handler.h
    - firmware/src/mqtt_handler.cpp
    - firmware/src/wifi_manager.h
    - firmware/src/wifi_manager.cpp
decisions:
  - "updatePulse() in led_controller — Pulse-Animation gehoert semantisch zum LED-Controller"
  - "runSystemHealthCheck() und initWatchdog() in web_server — MoodlightUtils-Instanzen bereits via extern dort verfuegbar"
  - "connectMQTTOnStartup() in mqtt_handler — MQTT-Init-Logik gehoert in den MQTT-Handler"
  - "connectWiFiAndStartServices() in wifi_manager — WiFi+NTP+mDNS+Server-Start als eine zusammenhaengende Einheit"
  - "initFirstLEDUpdate() in led_controller — LED-Initialisierung gehoert zum LED-Controller"
  - "Stack-Check-Block entfernt — reiner Debug-Output beim Start, kein Laufzeiteffekt"
  - "Preferences-Instanzen in runSystemHealthCheck() als lokale Prefs-Objekte — vermeidet globale extern-Abhaengigkeit auf preferences"
metrics:
  duration: "8min"
  completed: "2026-03-26T14:26:00Z"
  tasks_completed: 1
  files_modified: 9
---

# Phase 08 Plan 03: moodlight.cpp Finale Reduktion auf <= 200 Zeilen Summary

**One-liner:** moodlight.cpp auf 197 Zeilen reduziert durch Extraktion von 6 Funktionen in bestehende Module — reine setup()/loop()-Orchestrierung, Build gruen.

## What Was Built

moodlight.cpp ist von 524 Zeilen auf 197 Zeilen reduziert worden. Die Datei enthaelt nur noch setup() und loop() mit reinen Modul-Aufrufen, keine Business-Logik inline.

**Extrahierte Funktionen:**

| Funktion | Ziel-Modul | Beschreibung |
|---|---|---|
| `updatePulse()` | led_controller.h/.cpp | Pulse-Animation (~38 Zeilen) |
| `runSystemHealthCheck()` | web_server.h/.cpp | Health-Check-Block (~95 Zeilen) |
| `initWatchdog()` | web_server.h/.cpp | Watchdog-Timer-Init (~14 Zeilen) |
| `connectMQTTOnStartup()` | mqtt_handler.h/.cpp | MQTT-Init mit 5s Timeout (~35 Zeilen) |
| `connectWiFiAndStartServices()` | wifi_manager.h/.cpp | WiFi + NTP + mDNS + Server-Start (~40 Zeilen) |
| `initFirstLEDUpdate()` | led_controller.h/.cpp | Erster LED-Init nach Setup (~12 Zeilen) |

Stack-Check-Block (12 Zeilen) entfernt — reiner Debug-Output ohne Laufzeiteffekt.

## Verification

```
wc -l firmware/src/moodlight.cpp  => 197 (PASS <= 200)
grep -c "^void " moodlight.cpp    => 2   (nur setup + loop)
pio run                           => SUCCESS
```

## Commits

| Task | Name | Commit | Files |
|---|---|---|---|
| 1 | Finale Reduktion moodlight.cpp | 8c31d19 | 9 Dateien |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing safety] Lokale Preferences-Instanzen in runSystemHealthCheck()**
- **Found during:** Task 1
- **Issue:** runSystemHealthCheck() in web_server.cpp braucht Zugriff auf Preferences. Globale `preferences`-Variable liegt in settings_manager.cpp, ein weiteres extern wuerde die Abhaengigkeitskette verlängern.
- **Fix:** Lokale `Preferences prefs;`-Instanzen innerhalb der Funktion — korrekte RAII-Nutzung, kein Namespace-Konflikt.
- **Files modified:** firmware/src/web_server.cpp

**2. [Rule 1 - Cleanup] `<ArduinoJson.h>` und `<HTTPClient.h>` aus moodlight.cpp entfernt**
- **Found during:** Task 1 (Step 5 — Include-Bereinigung)
- **Issue:** Nach Extraktion der grossen Bloecke werden ArduinoJson und HTTPClient nicht mehr direkt in moodlight.cpp genutzt.
- **Fix:** Includes entfernt — werden in den jeweiligen Modulen inkludiert.
- **Files modified:** firmware/src/moodlight.cpp

## Known Stubs

Keine Stubs. Alle extrahierten Funktionen sind vollstaendig implementiert.

## Self-Check: PASSED

- firmware/src/moodlight.cpp: FOUND (197 Zeilen)
- firmware/src/led_controller.cpp: FOUND (updatePulse, initFirstLEDUpdate)
- firmware/src/web_server.cpp: FOUND (runSystemHealthCheck, initWatchdog)
- firmware/src/mqtt_handler.cpp: FOUND (connectMQTTOnStartup)
- firmware/src/wifi_manager.cpp: FOUND (connectWiFiAndStartServices)
- Commit 8c31d19: FOUND
- pio run: SUCCESS
