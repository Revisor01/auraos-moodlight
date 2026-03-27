# Phase 14: Backend-Dashboard - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Volles Backend-Dashboard: Übersichtsseite mit aktuellem Score und System-Status, Headlines-Ansicht mit Einzel-Scores und Feed-Zuordnung, Score-Aggregations-Visualisierung, Feed-Verwaltung über Navigation erreichbar. Alles hinter dem Login-Schutz aus Phase 13.

</domain>

<decisions>
## Implementation Decisions

### Dashboard-Architektur
- Single-Page-Ansatz mit mehreren Tabs/Bereichen: Übersicht, Headlines, Feeds
- Navigation zwischen Bereichen ohne Seiten-Reload (JavaScript Tab-Switching)
- Bestehende feeds.html Funktionalität wird ins Dashboard integriert
- Basis-Template dashboard.html mit Tab-Navigation

### API-Endpoints
- GET /api/moodlight/headlines — liefert letzte Headlines mit Einzel-Scores, Feed-Name, Zeitstempel
- Bestehende Endpoints nutzen: /api/moodlight/current, /api/moodlight/feeds

### Score-Visualisierung
- Zeigen wie der Gesamt-Score aus Einzelwerten berechnet wird
- Farb-kodierte Score-Anzeige (rot bis violett, wie LED-Farben)
- Durchschnittsberechnung sichtbar machen

### Design
- Konsistent mit bestehendem feeds.html Design (CSS-Variablen)
- Deutsch-sprachiges Interface
- Responsive für Desktop-Nutzung
- Abmelden-Link in der Navigation

### Claude's Discretion
- Genaues Layout und CSS
- Chart/Grafik-Bibliothek für Score-Visualisierung (oder reines CSS)
- Tab-Switching Implementierung

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/templates/feeds.html — Feed-Verwaltung (wird ins Dashboard integriert)
- sentiment-api/templates/login.html — Login-Seite (Design-Referenz)
- sentiment-api/moodlight_extensions.py — Bestehende API-Endpoints
- sentiment-api/database.py — get_all_feeds(), get_active_feeds(), save_headlines()
- docs/index.html — GitHub Page mit Chart.js (Design-Referenz für Visualisierung)

### Established Patterns
- Flask render_template mit Jinja2
- Vanilla JS + fetch() für API-Calls
- CSS-Variablen für konsistentes Theming
- @login_required Decorator aus Phase 13

### Integration Points
- app.py: Neue Route /dashboard (geschützt mit @login_required)
- moodlight_extensions.py: Neuer GET /api/moodlight/headlines Endpoint
- database.py: Neue Methode get_recent_headlines()
- /feeds Route kann auf /dashboard#feeds redirecten

</code_context>

<specifics>
## Specific Ideas

- Dashboard als Hauptseite nach Login (/ redirected zu /dashboard)
- Headlines mit Farbkodierung nach Score (-1.0 rot bis +1.0 grün)
- Feed-Name als Badge/Tag bei jeder Headline
- Zeitstempel als relative Angabe ("vor 2 Stunden")

</specifics>

<deferred>
## Deferred Ideas

Keine — Phase bleibt im Scope.

</deferred>
