# Phase 22: CSS-Fundament - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

style.css komplett überarbeiten: CSS-Variablen vom Backend-Dashboard übernehmen (Farben, Abstände, border-radius, Schriften). Dark/Light Mode beibehalten. Alle 4 ESP32-Seiten müssen mit dem neuen CSS ohne Fehler laden.

</domain>

<decisions>
## Implementation Decisions

### CSS-Variablen vom Dashboard übernehmen
- Farben: --bg, --card-bg, --text, --text-secondary, --border, --accent
- Abstände: --padding, --gap, --radius (8px)
- Schriften: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif
- Dark/Light Toggle: :root für Light, [data-theme="dark"] für Dark

### Bestehende Klassen beibehalten
- .card, .btn, .grid, .header — Klassen-Namen bleiben, Styling ändert sich
- .gauge, .led-row, .slider — Spezifische ESP32 UI-Elemente bleiben
- .mood-* Klassen — Farbkodierung für Sentiment-Stufen

### Claude's Discretion
- Genaue Farbwerte und Abstände
- Transition-Effekte
- Ob bestehende Klassen umbenannt oder nur umgestyled werden

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- firmware/data/css/style.css — Aktuelles ESP32 CSS (1119 Zeilen)
- sentiment-api/templates/dashboard.html — Dashboard CSS inline (Referenz für Design-Sprache)
- sentiment-api/templates/login.html — Login CSS inline (Referenz)

### Integration Points
- firmware/data/css/style.css: Komplett überschreiben
- Alle 4 HTML-Dateien nutzen style.css über <link rel="stylesheet" href="/css/style.css">
- JavaScript toggleDarkMode() muss weiter funktionieren

</code_context>

<specifics>
## Specific Ideas

Keine spezifischen Vorgaben — Dashboard-Design als Referenz.

</specifics>

<deferred>
## Deferred Ideas

Keine.

</deferred>
