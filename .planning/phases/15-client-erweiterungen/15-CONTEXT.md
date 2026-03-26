# Phase 15: Client-Erweiterungen - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

ESP32 mood.html und GitHub Page (docs/index.html) zeigen Headlines mit Einzel-Scores an. Beide nutzen den öffentlichen GET /api/moodlight/headlines Endpoint aus Phase 14.

</domain>

<decisions>
## Implementation Decisions

### ESP32 mood.html
- Neuer Bereich/Sektion in mood.html für Headlines
- fetch() gegen analyse.godsapp.de/api/moodlight/headlines
- Farbkodierte Einzel-Scores wie im Dashboard
- Fallback bei nicht erreichbarem Backend
- Kein Login nötig — Endpoint ist öffentlich

### GitHub Page (docs/index.html)
- Neue Sektion in der bestehenden Single-Page-App
- Headline-Liste mit Einzel-Scores und Feed-Zuordnung
- Konsistent mit bestehendem Chart.js Design
- Fallback bei nicht erreichbarem Backend

### Claude's Discretion
- Layout und Positionierung der Headlines-Sektion
- Wie viele Headlines angezeigt werden (10-20 reicht)
- Styling-Details

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- firmware/data/mood.html — Bestehende Stimmungsseite auf ESP32
- docs/index.html — GitHub Page mit Chart.js, Live-Score, Verlaufs-Charts
- GET /api/moodlight/headlines — Öffentlicher Endpoint (Phase 14)

### Established Patterns
- mood.html nutzt fetch() gegen apiUrl (konfigurierbar im ESP32)
- docs/index.html nutzt fetch() gegen analyse.godsapp.de
- Beide nutzen Vanilla JS, kein Framework

### Integration Points
- mood.html: Neue Sektion nach dem Score-Display
- docs/index.html: Neue Sektion nach den Charts

</code_context>

<specifics>
## Specific Ideas

Keine spezifischen Vorgaben.

</specifics>

<deferred>
## Deferred Ideas

Keine.

</deferred>
