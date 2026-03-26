---
phase: 08-konsolidierung-qualitaet
plan: 02
subsystem: firmware
tags: [esp32, debug, hardware-instances, module-extraction, c++]

# Dependency graph
requires:
  - phase: 08-01
    provides: Magic-Numbers und konstanten in config.h konsolidiert, extern const für Firmware-weite Konstanten

provides:
  - debug.h/.cpp Modul mit debug() und floatToString()
  - Hardware-Instanzen in jeweiligen Modulen (pixels, dht, wifiClientHTTP, server, dnsServer, preferences)
  - Alle extern void debug / extern String floatToString Deklarationen ersetzt durch #include "debug.h"
  - moodlight.cpp auf reine setup()/loop()-Orchestrierung reduziert

affects:
  - 08-03
  - alle zukünftigen Modul-Extraktionen in Phase 08

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "debug.h als zentrales Logging-Interface — alle Module inkludieren debug.h statt extern-Deklarationen"
    - "Hardware-Instanzen in eigenem Modul definiert, extern in zugehörigem Header deklariert"
    - "moodlight.cpp als Orchestrator ohne Hardware-Definitions-Verantwortung"

key-files:
  created:
    - firmware/src/debug.h
    - firmware/src/debug.cpp
  modified:
    - firmware/src/moodlight.cpp
    - firmware/src/MoodlightUtils.h
    - firmware/src/led_controller.cpp
    - firmware/src/led_controller.h
    - firmware/src/wifi_manager.cpp
    - firmware/src/wifi_manager.h
    - firmware/src/web_server.cpp
    - firmware/src/web_server.h
    - firmware/src/sensor_manager.cpp
    - firmware/src/sensor_manager.h
    - firmware/src/mqtt_handler.cpp
    - firmware/src/settings_manager.cpp
    - firmware/src/settings_manager.h

key-decisions:
  - "debug.cpp definiert #define DEBUG_MODE als erste Zeile — Präprozessor-Schutz für beide debug()-Varianten"
  - "WiFiClient wifiClientHTTP in sensor_manager.cpp (nicht web_server.cpp) — dort am häufigsten genutzt, mqtt_handler greift ebenfalls darüber zu"
  - "sensor_manager.h inkludiert <DHT.h> und <WiFiClient.h> direkt für extern-Deklarationen der Hardware-Instanzen"
  - "mqtt_handler.cpp inkludiert sensor_manager.h für Zugriff auf dht und wifiClientHTTP (statt direkter extern-Deklarationen)"

patterns-established:
  - "Modul-Ownership: Hardware-Instanz definiert in .cpp, extern deklariert in .h"
  - "Debug-Import: #include debug.h statt extern void debug() überall"

requirements-completed: [ARCH-02, QUAL-03]

# Metrics
duration: 15min
completed: 2026-03-26
---

# Phase 08 Plan 02: debug-Modul + Hardware-Instanzen-Verschiebung Summary

**debug.h/.cpp als eigenstaendiges Logging-Modul extrahiert, alle 6 Hardware-Instanzen in ihre jeweiligen Module verschoben, extern-Duplikate eliminiert, moodlight.cpp auf 524 Zeilen reduziert**

## Performance

- **Duration:** 15 min
- **Started:** 2026-03-26T14:15:00Z
- **Completed:** 2026-03-26T14:30:00Z
- **Tasks:** 1
- **Files modified:** 15 (13 modifiziert, 2 neu erstellt)

## Accomplishments

- Neues debug.h/.cpp Modul mit beiden debug()-Überladungen und floatToString() — 86 Zeilen
- 6 Hardware-Instanzen aus moodlight.cpp in ihre Module verschoben: pixels (led_controller), dnsServer (wifi_manager), server (web_server), wifiClientHTTP + dht (sensor_manager), preferences (settings_manager)
- Alle extern void debug() / extern String floatToString() Deklarationen in 5 Modulen durch #include "debug.h" ersetzt
- moodlight.cpp von vorher ~616 Zeilen auf 524 Zeilen reduziert — enthält nur noch setup()/loop()-Orchestrierung

## Task Commits

Jeder Task wurde atomisch committed:

1. **Task 1: debug() extrahieren + Hardware-Globals verschieben** - `410a176` (feat)

**Plan metadata:** folgt mit diesem Commit

## Files Created/Modified

- `firmware/src/debug.h` - debug() und floatToString() Deklarationen
- `firmware/src/debug.cpp` - debug()-Implementierung mit Ringpuffer (86 Zeilen)
- `firmware/src/moodlight.cpp` - debug-Blöcke und Hardware-Instanzen entfernt, #include "debug.h" hinzugefügt
- `firmware/src/MoodlightUtils.h` - extern debug() durch #include "debug.h" ersetzt
- `firmware/src/led_controller.cpp` - extern debug() durch #include "debug.h", pixels hier definiert
- `firmware/src/led_controller.h` - extern Adafruit_NeoPixel pixels hinzugefügt
- `firmware/src/wifi_manager.cpp` - extern debug() durch #include "debug.h" + "web_server.h", dnsServer hier definiert
- `firmware/src/wifi_manager.h` - #include <DNSServer.h> + extern DNSServer dnsServer hinzugefügt
- `firmware/src/web_server.cpp` - extern debug()/floatToString() durch #include "debug.h", server(80) hier definiert
- `firmware/src/web_server.h` - extern WebServer server hinzugefügt
- `firmware/src/sensor_manager.cpp` - extern debug()/floatToString() durch #include "debug.h", dht + wifiClientHTTP hier definiert
- `firmware/src/sensor_manager.h` - #include <DHT.h> + <WiFiClient.h>, extern-Deklarationen für dht + wifiClientHTTP
- `firmware/src/mqtt_handler.cpp` - extern debug()/floatToString()/dht/wifiClientHTTP durch #include "debug.h" + "sensor_manager.h" ersetzt
- `firmware/src/settings_manager.cpp` - extern debug() durch #include "debug.h", preferences hier definiert
- `firmware/src/settings_manager.h` - #include <Preferences.h> + extern Preferences preferences hinzugefügt

## Decisions Made

- debug.cpp definiert `#define DEBUG_MODE` als erste Zeile vor den Implementierungen — stellt sicher, dass beide Überladungen korrekt kompiliert werden
- WiFiClient wifiClientHTTP in sensor_manager.cpp (nicht web_server.cpp) — sensor_manager ist der primäre Nutzer (HTTP-Calls für Sentiment/DHT), mqtt_handler greift ebenfalls darüber zu via sensor_manager.h
- mqtt_handler.cpp inkludiert jetzt sensor_manager.h für transitiven Zugriff auf dht und wifiClientHTTP — sauberere Abhängigkeit als direkte extern-Deklarationen

## Deviations from Plan

None — Plan exakt wie spezifiziert ausgeführt.

## Issues Encountered

None — Build war beim ersten Versuch grün.

## Next Phase Readiness

- debug-Modul ist stabile Basis für alle weiteren Module
- moodlight.cpp ist bereit für weitere Extraktion (Plan 03: Health-Check + Utility-Instanzen)
- Alle Hardware-Instanzen sind jetzt in ihren natürlichen Modulen — Plan 03 kann fileOps/watchdog/memMonitor/netDiag/sysHealth analog verschieben

---
*Phase: 08-konsolidierung-qualitaet*
*Completed: 2026-03-26*
