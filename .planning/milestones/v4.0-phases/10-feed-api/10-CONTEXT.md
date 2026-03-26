# Phase 10: Feed-API - Context

**Gathered:** 2026-03-26
**Status:** Ready for planning
**Mode:** Infrastructure phase — discuss skipped

<domain>
## Phase Boundary

CRUD-Endpoints für Feed-Verwaltung: GET /api/moodlight/feeds liefert Feed-Liste mit Status, POST fügt Feed mit URL-Validierung hinzu, DELETE entfernt Feed. Alle Endpoints arbeiten gegen die in Phase 9 erstellte feeds-Tabelle.

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
All implementation choices are at Claude's discretion — API infrastructure phase. Use ROADMAP phase goal, success criteria, and codebase conventions to guide decisions.

Key constraints:
- Endpoints gehören in moodlight_extensions.py (dort sind alle /api/moodlight/* Endpoints registriert)
- Database-Klasse hat bereits get_active_feeds() aus Phase 9
- Feed-Validierung: HTTP HEAD/GET Request auf die URL, Timeout ~5s
- Bestehende Patterns: Flask jsonify(), return mit Status-Code
- Der entfernte /api/feedconfig Endpoint war ein In-Memory-Stub — die neuen Endpoints persistieren in DB

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/database.py — Database-Klasse mit get_active_feeds(), execute(), fetchall()
- sentiment-api/moodlight_extensions.py — Alle /api/moodlight/* Endpoints, registriert via register_moodlight_endpoints(app)
- sentiment-api/init.sql — feeds-Tabelle mit id, name, url, active, last_fetched_at, error_count, created_at, updated_at

### Established Patterns
- Endpoints in moodlight_extensions.py folgen: try/except, logging, jsonify response
- Database operations via get_database() Singleton
- Redis caching für GET-Endpoints (5 min TTL)

### Integration Points
- moodlight_extensions.py: register_moodlight_endpoints(app) — neue Routen hier registrieren
- database.py: Neue Methoden für add_feed() und delete_feed()

</code_context>

<specifics>
## Specific Ideas

No specific requirements — API infrastructure phase.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>
