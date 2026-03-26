# Phase 1: Firmware-Stabilität - Context

**Gathered:** 2026-03-25
**Status:** Ready for planning
**Mode:** Auto-generated (infrastructure phase — all fixes technically specified in research)

<domain>
## Phase Boundary

Der ESP32 läuft dauerhaft ohne Watchdog-Resets, Heap-Leaks oder unerklärliches LED-Blinken. Sechs konkrete Fixes an der bestehenden Firmware:

1. **LED-01**: Buffer-Overflow Fix — `ledColors[MAX_LEDS]` statt `ledColors[DEFAULT_NUM_LEDS]`, Validation bei `numLeds`
2. **LED-02**: LED-Timing Fix — `processLEDUpdates()` Bedingung fixen, kein `pixels.show()` während WiFi/MQTT-Reconnect
3. **LED-03**: Status-LED Debounce — Blink erst nach 30s Disconnect, nicht sofort
4. **MEM-01**: JSON Buffer Pool RAII — Guard-Klasse, die im Destruktor `release()` aufruft
5. **MEM-02**: Health-Check Konsolidierung — zwei Timer (1h + 5min) zu einem zusammenführen
6. **SEC-01**: Credentials masken — Passwörter in API-Responses durch `"****"` ersetzen

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
All implementation choices are at Claude's discretion — pure infrastructure phase with technically specified fixes from research. Key constraints from research:

- **MAX_LEDS**: Statisches Array mit Konstante (z.B. 30), KEIN dynamisches Heap-Alloc
- **RAII Guard**: Destruktor-basiert, `wasPooled` Flag oder equivalent um Heap-Fallback korrekt freizugeben
- **Health-Check**: 5min Intervall beibehalten, Counter-basierte Eskalation. Vor Merge: `sysHealth.isRestartRecommended()` Kriterien in MoodlightUtils prüfen
- **LED Timing**: Reconnect-State tracken, `processLEDUpdates()` Bedingung vereinfachen
- **Debounce**: `disconnectStartMs` Variable, Status-LED erst setzen wenn `millis() - disconnectStartMs > 30000`

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ledMutex` (FreeRTOS Semaphore) — bereits vorhanden für thread-safe LED-Updates
- `MoodlightUtils.h/.cpp` — SystemHealthCheck Klasse bereits vorhanden
- `JsonBufferPool` Klasse (Zeilen 320-372) — wird um RAII-Guard erweitert
- `processLEDUpdates()` (Zeile 560) — zentrale LED-Update-Funktion
- `setStatusLED()` / `updateStatusLED()` (Zeilen 470-529) — Status-LED Logik

### Established Patterns
- FreeRTOS Mutex mit `xSemaphoreTake`/`xSemaphoreGive` (10ms Timeout)
- `constrain()` für Werte-Validierung
- Preferences-API für persistente Flags
- `debug()` Funktion für Logging

### Integration Points
- `config.h` — neue `MAX_LEDS` Konstante
- `moodlight.cpp` Zeile 148 — `ledColors` Array-Definition
- `moodlight.cpp` Zeile 592 — `processLEDUpdates()` WiFi-Bedingung
- `moodlight.cpp` Zeilen 4364-4499 — beide Health-Check Blöcke
- Web-API Handlers: `/api/export/settings`, `/api/settings/mqtt`

</code_context>

<specifics>
## Specific Ideas

No specific requirements — infrastructure phase. Refer to ROADMAP phase description, success criteria, and .planning/research/ documents for implementation details.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>
