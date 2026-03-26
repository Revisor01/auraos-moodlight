# Phase 11: Feed-Management Web-Interface - Context

**Gathered:** 2026-03-26
**Status:** Ready for planning

<domain>
## Phase Boundary

Browser-basierte Feed-Verwaltung: Seite zeigt alle Feeds als Tabelle, User kann Feeds hinzufügen (mit Fehleranzeige bei ungültiger URL) und entfernen (ohne Seiten-Reload). Pro Feed sind Name, URL, letzter Fetch-Zeitpunkt und Fehler-Count sichtbar.

</domain>

<decisions>
## Implementation Decisions

### UI-Platzierung
- Feed-Management als eigenständige HTML-Seite, serviert vom Flask-Backend (NICHT vom ESP32)
- Grund: Feeds sind eine Backend-Concern, die setup.html auf dem ESP32 hat keinen Zugriff auf das Backend
- URL: `/feeds` oder `/admin/feeds` auf analyse.godsapp.de
- Alternativ: Neue Sektion auf der bestehenden GitHub Page (docs/index.html) — aber Backend-served ist einfacher für CRUD

### Design-Stil
- Schlichtes Design passend zum bestehenden Backend-Look
- Tabelle mit Feed-Daten, Formular zum Hinzufügen, Löschen-Button pro Zeile
- JavaScript fetch() gegen die /api/moodlight/feeds Endpoints (Phase 10)
- Kein Framework — Vanilla HTML/CSS/JS wie die bestehenden Seiten

### Claude's Discretion
- Genaues Layout und Styling
- Validierungs-UX Details (inline Fehler, Loading-States)
- Ob als separate HTML-Datei oder als Jinja2-Template

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/moodlight_extensions.py — GET/POST/DELETE /api/moodlight/feeds Endpoints (Phase 10)
- docs/index.html — Bestehende GitHub Page mit Chart.js, dark/light Mode, responsive Design
- firmware/data/ — ESP32 Web-Interface Dateien (setup.html, mood.html etc.) — NICHT für diese Phase relevant

### Established Patterns
- Backend hat aktuell keine eigene HTML-Seite — nur JSON-APIs
- GitHub Page nutzt Vanilla JS + fetch() + CSS Grid
- ESP32 Web-UI nutzt Vanilla JS + fetch() + responsive CSS

### Integration Points
- Flask app.py: Neue Route für die Feed-Management-Seite
- /api/moodlight/feeds: CRUD-Endpoints aus Phase 10
- Optional: Link von GitHub Page zur Feed-Verwaltung

</code_context>

<specifics>
## Specific Ideas

- Feed-Status (letzter Fetch-Zeitpunkt, Fehler-Count) pro Feed sichtbar
- Inline-Fehleranzeige beim Hinzufügen (URL nicht erreichbar, Duplikat)
- Löschen ohne Seiten-Reload (AJAX)

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>
