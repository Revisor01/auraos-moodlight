# Phase 7: Modul-Extraktion - Context

**Gathered:** 2026-03-26
**Status:** Ready for planning
**Mode:** Auto-generated (infrastructure phase — pure refactoring)

<domain>
## Phase Boundary

6 Module aus moodlight.cpp extrahieren. Nach jeder Extraktion muss `pio run` grün bleiben.

1. **MOD-05**: Settings-Manager (loadSettings, saveSettings, JSON-Serialisierung) — ZUERST, da andere Module Settings-Funktionen aufrufen
2. **MOD-01**: WiFi-Manager (AP-Mode, Captive Portal, Station-Connect, Reconnect)
3. **MOD-02**: LED-Controller (NeoPixel, updateLEDs, processLEDUpdates, Status-LED, Pulse)
4. **MOD-06**: Sensor & Sentiment (DHT-Read, API-Fetch, mapSentimentToLED)
5. **MOD-04**: MQTT/HA-Handler (setupHA, MQTT-Connect, ArduinoHA-Entities, Heartbeat)
6. **MOD-03**: Web-Server (Route-Registration, API-Handler, File-Handler, Upload-Handler)

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
- **Reihenfolge**: Settings zuerst (wird von allen gebraucht), dann WiFi (standalone), dann LED (standalone), dann Sensor (braucht LED für mapSentimentToLED), dann MQTT (braucht HA-Library), dann WebServer (braucht alles andere)
- **Pattern**: Jedes Modul bekommt eine `init_xxx(AppState& state)` Funktion und ggf. `loop_xxx(AppState& state)` oder `handle_xxx(AppState& state)` Funktionen
- **AppState by Reference**: Module bekommen `AppState&` statt globale Variable direkt — ermöglicht späteren Wechsel
- **#include-Strategie**: Jedes Modul inkludiert `app_state.h` + seine eigenen Dependencies. Kein Modul inkludiert ein anderes Modul.
- **Library-Instanzen**: `pixels`, `dht`, `server`, `mqtt` etc. bleiben als globale Instanzen in moodlight.cpp und werden per Parameter an Module übergeben ODER als extern in den Modul-Headern deklariert
- **Warnung aus Phase 6**: Bei replace_all IMMER `grep '"appState.'` prüfen — JSON-Keys und Preferences-Keys dürfen nicht ersetzt werden

</decisions>

<code_context>
## Existing Code Insights

### Aktuelle Struktur (nach Phase 6)
- `firmware/src/app_state.h` — AppState-Struct mit ~60 Membern
- `firmware/src/moodlight.cpp` — ~4500 Zeilen, alle Logik, 585 appState-Referenzen
- `firmware/src/MoodlightUtils.h/.cpp` — Utility-Klassen (bleiben unverändert)
- `firmware/src/config.h` — Konstanten

### Funktionsgruppen in moodlight.cpp (grobe Orientierung)
- Zeilen ~100-400: LED-Funktionen (updateLEDs, processLEDUpdates, setStatusLED)
- Zeilen ~400-700: Settings (saveSettingsToFile, loadSettings, loadSettingsFromJSON)
- Zeilen ~700-900: WiFi-Funktionen (startAPMode, reconnect)
- Zeilen ~900-1300: Web-API-Handler (/api/status, /api/settings, etc.)
- Zeilen ~1300-1600: Upload-Handler (UI-Upload, Firmware-Upload)
- Zeilen ~1600-2000: Sentiment-Fetch, mapSentimentToLED
- Zeilen ~2000-2500: Route-Registration, setupWebServer
- Zeilen ~2500-3200: MQTT/HA-Setup, Entity-Definitionen
- Zeilen ~3200-3800: setup() Funktion
- Zeilen ~3800-4500: loop() Funktion

</code_context>

<specifics>
## Specific Ideas

No specific requirements beyond MOD-01 through MOD-06.

</specifics>

<deferred>
## Deferred Ideas

None.

</deferred>
