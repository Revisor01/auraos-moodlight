# Phase 23: Seiten-Redesign - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Alle 4 ESP32 HTML-Seiten (index.html, setup.html, mood.html, diagnostics.html) an das Dashboard-Design angleichen. Karten-Layout, Typografie, Farbkodierung konsistent mit dem neuen style.css aus Phase 22. Keine Funktionsänderungen — nur visuelles Redesign.

</domain>

<decisions>
## Implementation Decisions

### Design-Prinzipien
- Karten-Layout (border-radius 8px, Schatten, kompaktes Padding) wie Dashboard
- Score-Farbkodierung in 5 Stufen wie im Dashboard
- Navigation: Header mit Links zwischen den Seiten bleibt
- Responsive: Mobile-First, wie bisher
- Dark/Light Mode über .dark Klasse (funktioniert bereits via Phase 22 CSS)

### Keine Funktionsänderungen
- JavaScript bleibt unverändert (außer CSS-Klassen-Referenzen falls nötig)
- API-Calls bleiben identisch
- Alle Einstellungen, Controls, Uploads bleiben funktional

### Claude's Discretion
- Genaues HTML-Markup für die Karten
- Ob inline-Styles entfernt und durch CSS-Klassen ersetzt werden
- Layout-Details

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- firmware/data/css/style.css — Neues CSS aus Phase 22 (1035 Zeilen)
- firmware/data/index.html (102 Zeilen) — Hauptseite
- firmware/data/setup.html (491 Zeilen) — Einstellungen
- firmware/data/mood.html (252 Zeilen) — Stimmung/Headlines
- firmware/data/diagnostics.html (373 Zeilen) — Diagnose/Update
- sentiment-api/templates/dashboard.html — Design-Referenz

### Integration Points
- Alle 4 Dateien nutzen /css/style.css
- JavaScript in den Dateien referenziert CSS-Klassen

</code_context>

<specifics>
## Specific Ideas

Keine spezifischen Vorgaben — Dashboard als visuelle Referenz.

</specifics>

<deferred>
## Deferred Ideas

Keine.

</deferred>
