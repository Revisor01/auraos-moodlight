# Phase 8: Konsolidierung & Qualität - Context

**Gathered:** 2026-03-26
**Status:** Ready for planning
**Mode:** Auto-generated (infrastructure phase)

<domain>
## Phase Boundary

moodlight.cpp auf max 200 Zeilen reduzieren (aktuell 677), Dead Code entfernen, Magic Numbers durch Konstanten ersetzen, Duplikate konsolidieren.

1. **ARCH-02**: moodlight.cpp nur noch setup(), loop(), Modul-Init — max 200 Zeilen
2. **ARCH-03**: pio run fehlerfrei, keine zirkulären Dependencies
3. **QUAL-01**: Toten Code entfernen
4. **QUAL-02**: Magic Numbers → Konstanten in config.h
5. **QUAL-03**: Duplikate konsolidieren

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
- **moodlight.cpp auf 200 Zeilen**: Die restlichen ~477 Zeilen sind vermutlich debug(), Konstanten-Definitionen, und verbleibende Globals die noch nicht in Module gehören. Diese in bestehende Module verschieben oder ein neues `system_utils.h` erstellen.
- **Magic Numbers**: In moodlight.cpp und allen neuen Modulen durchsuchen, in config.h zentralisieren
- **Dead Code**: Beim Aufteilen gefundene unreachable Pfade, auskommentierte Blöcke, unused Funktionen entfernen
- **Duplikate**: Gleiche Patterns in mehreren Modulen (z.B. JSON-Serialisierung, debug-Aufrufe) konsolidieren

</decisions>

<code_context>
## Current State

- moodlight.cpp: 677 Zeilen (Ziel: <200)
- 6 Module extrahiert: settings_manager, wifi_manager, led_controller, sensor_manager, mqtt_handler, web_server
- app_state.h: AppState-Struct mit ~60 Membern
- pio run baut sauber (81.9% Flash, 28.3% RAM)

</code_context>

<specifics>
## Specific Ideas

None.

</specifics>

<deferred>
## Deferred Ideas

None.

</deferred>
