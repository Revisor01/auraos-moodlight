# Phase 6: Shared State Fundament - Context

**Gathered:** 2026-03-26
**Status:** Ready for planning
**Mode:** Auto-generated (infrastructure phase)

<domain>
## Phase Boundary

Alle globalen Variablen aus moodlight.cpp in ein zentrales AppState-Struct überführen. Danach kommunizieren Module über ein definiertes Interface statt über extern-Variablen.

**ARCH-01**: AppState-Struct oder globale Header-Datei `app_state.h` mit allen geteilten Variablen.

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
- **Ansatz**: `app_state.h` mit einem globalen `AppState` Struct. Kein Singleton-Pattern, kein Dependency Injection — einfacher globaler Zustand, passend für ESP32 embedded.
- **Scope**: Nur Variablen die von mehreren zukünftigen Modulen gebraucht werden. Rein lokale Variablen (z.B. static in einem Handler) bleiben wo sie sind.
- **Schritt-für-Schritt**: Erst Struct definieren, dann in moodlight.cpp die globalen Variablen durch AppState-Member ersetzen, dann prüfen dass es baut.
- **Kein Funktions-Refactoring in dieser Phase** — nur Variablen-Zentralisierung. Funktionen werden in Phase 7 extrahiert.

</decisions>

<code_context>
## Existing Code Insights

### Globale Variablen in moodlight.cpp (zu zentralisieren)
- WiFi: `wifiSSID`, `wifiPass`, `wifiConfigured`, `isInConfigMode`, `wifiReconnectActive`, `disconnectStartMs`
- LED: `ledPin`, `numLeds`, `lightOn`, `autoMode`, `currentLedIndex`, `manualColor`, `manualBrightness`, `statusLedMode`, `ledColors[]`, `ledMutex`, `ledBrightness`, `ledUpdatePending`, `ledClear`, `statusLedIndex`
- Sentiment: `sentimentScore`, `sentimentCategory`, `moodUpdateInterval`, `lastMoodUpdate`, `consecutiveFailures`
- MQTT: `mqttEnabled`, `mqttServer`, `mqttUser`, `mqttPass`, `mqttPort`
- DHT: `dhtEnabled`, `dhtPin`, `dhtInterval`, `currentTemp`, `currentHumidity`
- Settings: `apiUrl`, `customColors[5]`
- System: `rebootNeeded`, `rebootTime`, `lastSystemHealthCheckTime`

### Integration Points
- `firmware/src/moodlight.cpp` — alle globalen Variablen
- `firmware/src/config.h` — Konstanten (DEFAULT_*, MAX_LEDS etc.)
- `firmware/src/MoodlightUtils.h/.cpp` — Utility-Klassen (unverändert in dieser Phase)

</code_context>

<specifics>
## Specific Ideas

No specific requirements beyond ARCH-01.

</specifics>

<deferred>
## Deferred Ideas

None.

</deferred>
