# Phase 24: Backend-Trendberechnung - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning
**Mode:** Infrastructure phase — discuss skipped

<domain>
## Phase Boundary

Durchschnitts-Score pro Feed berechnen (7-Tage/30-Tage) und als sortiertes Ranking per API liefern. Nutzt bestehende headlines-Tabelle mit feed_id FK.

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
Infrastruktur-Phase — alle Implementierungsentscheidungen nach eigenem Ermessen.

Key constraints:
- headlines-Tabelle hat feed_id FK zu feeds-Tabelle
- SQL: GROUP BY feed_id, AVG(sentiment_score), COUNT(*), mit WHERE analyzed_at Zeitfilter
- Endpoint: GET /api/moodlight/feeds/trends?days=7 (default 7, auch 30)
- Response: sortiertes Array mit feed_name, avg_score, headline_count, min/max Score
- Öffentlicher Endpoint (kein Auth) — GitHub Page braucht ihn

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/database.py — Database-Klasse, headlines-Tabelle
- sentiment-api/moodlight_extensions.py — Bestehende /api/moodlight/* Endpoints
- sentiment-api/init.sql — headlines-Tabelle mit feed_id FK

### Integration Points
- database.py: Neue Methode get_feed_trends(days=7)
- moodlight_extensions.py: GET /api/moodlight/feeds/trends Endpoint

</code_context>

<specifics>
## Specific Ideas

Keine spezifischen Vorgaben.

</specifics>

<deferred>
## Deferred Ideas

Keine.

</deferred>
