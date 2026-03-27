# Phase 12: Headline-Persistenz - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning
**Mode:** Infrastructure phase — discuss skipped

<domain>
## Phase Boundary

Das Backend speichert bei jeder Sentiment-Analyse die einzelnen Headlines mit ihren Einzel-Scores dauerhaft in der Datenbank. Aktuell wird nur der Durchschnitts-Score gespeichert, die einzelnen Headlines gehen verloren.

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
All implementation choices are at Claude's discretion — pure infrastructure phase.

Key constraints:
- headlines-Tabelle braucht Fremdschlüssel zu feeds und sentiment_updates
- Bestehende Analyse-Funktion analyze_headlines_openai_batch() liefert bereits Einzel-Scores
- Die Scores müssen nach der Analyse in die DB geschrieben werden (background_worker.py)
- Performance: Bulk-INSERT für ~100 Headlines pro Analyse-Zyklus
- Bestehende Funktionalität darf nicht brechen

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/database.py — Database-Klasse mit ThreadedConnectionPool
- sentiment-api/init.sql — Bestehende Schema-Definitionen (sentiment_scores, feeds)
- sentiment-api/background_worker.py — _perform_update() und _fetch_headlines()
- sentiment-api/app.py — analyze_headlines_openai_batch() gibt Dict mit Einzel-Scores zurück

### Established Patterns
- DB-Schema in init.sql + Migrations-SQL für Produktionssystem
- Database-Klasse mit execute(), fetchall(), RealDictCursor
- Background Worker speichert via save_sentiment() in sentiment_scores

### Integration Points
- background_worker.py: Nach analyze_headlines_openai_batch() → Headlines + Scores in DB schreiben
- database.py: Neue Methode save_headlines() für Bulk-INSERT
- init.sql: headlines-Tabelle mit FK zu feeds(id) und sentiment_scores(id)

</code_context>

<specifics>
## Specific Ideas

Keine spezifischen Vorgaben — Standard-DB-Persistierung.

</specifics>

<deferred>
## Deferred Ideas

Keine — Infrastruktur-Phase.

</deferred>
