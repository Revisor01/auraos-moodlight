# Phase 25: Visualisierung & GitHub Page - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Dashboard und GitHub Page zeigen Feed-Ranking: positivster bis negativster Feed, farbkodierte Score-Balken, Zeitfenster-Umschalter (7/30 Tage). Nutzt GET /api/moodlight/feeds/trends aus Phase 24.

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
Alle Designentscheidungen nach eigenem Ermessen.

Key constraints:
- Dashboard: Neuer Bereich im Übersichts-Tab oder eigener Tab
- Farbkodierte Balken: rot (negativ) bis grün (positiv)
- Zeitfenster-Umschalter: Buttons "7 Tage" / "30 Tage"
- GitHub Page: Neue Sektion mit gleichen Daten
- Endpoint: GET /api/moodlight/feeds/trends?days=7|30

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/templates/dashboard.html — Dashboard mit 4 Tabs
- docs/index.html — GitHub Page
- GET /api/moodlight/feeds/trends — Phase 24 Endpoint

### Integration Points
- dashboard.html: Neue Feed-Trend-Sektion
- docs/index.html: Neue Feed-Vergleichs-Sektion

</code_context>

<specifics>
## Specific Ideas

Keine spezifischen Vorgaben.

</specifics>

<deferred>
## Deferred Ideas

Keine.

</deferred>
