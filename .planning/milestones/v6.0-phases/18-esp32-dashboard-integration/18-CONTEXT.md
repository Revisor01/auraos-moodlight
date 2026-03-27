# Phase 18: ESP32 + Dashboard Integration - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

ESP32 bezieht led_index und Schwellwerte dynamisch aus der erweiterten /api/moodlight/current Response (Phase 17). Dashboard zeigt Skalierungs-Transparenz: Wo steht der aktuelle Score im historischen Vergleich (Perzentil-Position, Bereich).

</domain>

<decisions>
## Implementation Decisions

### ESP32 Integration
- sensor_manager.cpp: mapSentimentToLED() durch led_index aus API-Response ersetzen
- Fallback: Wenn API kein led_index liefert (altes Backend), hardcoded Schwellwerte als Fallback
- Keine Änderung an der LED-Farb-Zuordnung selbst (customColors[0-4] bleibt)

### Dashboard Integration
- Übersichts-Tab im Dashboard erweitern: Perzentil-Position, historischer Bereich
- Farbverlauf-Balken zeigt Position des aktuellen Scores im historischen Min/Max-Bereich
- Schwellwerte-Anzeige: P20/P40/P60/P80 Grenzen sichtbar

### Claude's Discretion
- Genaues Layout der Skalierungs-Anzeige im Dashboard
- Wie der Perzentil-Rang visualisiert wird
- ESP32 Parsing der neuen JSON-Felder

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- firmware/src/sensor_manager.cpp — mapSentimentToLED(), handleSentiment()
- sentiment-api/templates/dashboard.html — Übersichts-Tab mit Score-Karten
- /api/moodlight/current — liefert jetzt led_index, percentile, thresholds, historical (Phase 17)

### Integration Points
- sensor_manager.cpp: handleSentiment() → led_index aus JSON-Response lesen statt mapSentimentToLED()
- moodlight.cpp oder sensor_manager.cpp: JSON-Parsing der erweiterten Response
- dashboard.html: Übersichts-Tab erweitern mit Skalierungs-Daten

</code_context>

<specifics>
## Specific Ideas

Keine spezifischen Vorgaben.

</specifics>

<deferred>
## Deferred Ideas

Keine.

</deferred>
