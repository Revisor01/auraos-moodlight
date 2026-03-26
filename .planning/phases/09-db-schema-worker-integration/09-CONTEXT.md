# Phase 9: DB-Schema & Worker-Integration - Context

**Gathered:** 2026-03-26
**Status:** Ready for planning
**Mode:** Infrastructure phase — discuss skipped

<domain>
## Phase Boundary

Feed-Liste ist in PostgreSQL persistiert und der Background Worker liest Feeds aus der DB statt aus hardcodierten Python-Listen. Focus.de Feed wird aus der Default-Liste entfernt.

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
All implementation choices are at Claude's discretion — pure infrastructure phase. Use ROADMAP phase goal, success criteria, and codebase conventions to guide decisions.

Key constraints:
- Existing init.sql pattern für DB-Schema nutzen
- Background Worker hat bereits DB-Zugriff via Database-Klasse
- shared_config.py existiert als aktuelle Zwischenlösung für Feed-Listen
- Feed-Liste ist in app.py und background_worker.py dupliziert

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/database.py — Database-Klasse mit ThreadedConnectionPool, Singleton via get_database()
- sentiment-api/init.sql — Bestehende Schema-Definitionen (sentiment_scores, device_requests, views, triggers)
- sentiment-api/shared_config.py — Aktuelle Feed-Liste als Python-Dict (Zwischenlösung aus v1.0)

### Established Patterns
- DB-Schema in init.sql, wird beim Container-Start via docker-compose volumes gemountet
- Database-Klasse hat execute(), fetchone(), fetchall() Methoden
- Background Worker nutzt Database-Instanz via get_database()

### Integration Points
- background_worker.py Zeile ~137: Hardcoded RSS_FEEDS Liste → muss aus DB lesen
- app.py Zeile ~55: Hardcoded RSS_FEEDS Liste → muss aus DB lesen
- shared_config.py: Wird von beiden importiert → kann nach Migration entfernt werden

</code_context>

<specifics>
## Specific Ideas

No specific requirements — infrastructure phase. Refer to ROADMAP phase description and success criteria.

</specifics>

<deferred>
## Deferred Ideas

None — infrastructure phase.

</deferred>
