# Phase 20: Manueller Analyse-Trigger - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

POST /api/moodlight/analyze/trigger startet sofortige Analyse. Dashboard zeigt Lade-Indikator während Analyse läuft. Nach Abschluss aktualisiert sich Dashboard automatisch.

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
Alle Implementierungsentscheidungen nach eigenem Ermessen.

Key constraints:
- Endpoint POST /api/moodlight/analyze/trigger braucht @api_login_required
- Kann _perform_update() aus dem Worker wiederverwenden
- Synchroner Request (wartet auf Ergebnis) — Analyse dauert ~10-30 Sekunden
- Dashboard: Button im Übersichts-Tab, wird disabled während Analyse läuft
- Nach Abschluss: loadOverview() erneut aufrufen

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/background_worker.py — _perform_update() enthält die komplette Analyse-Logik
- sentiment-api/moodlight_extensions.py — @api_login_required, bestehende Endpoints
- sentiment-api/templates/dashboard.html — Übersichts-Tab

### Integration Points
- moodlight_extensions.py: Neuer POST /api/moodlight/analyze/trigger Endpoint
- background_worker.py: _perform_update() oder separate trigger-Methode
- dashboard.html: Button + Spinner + Auto-Refresh nach Abschluss

</code_context>

<specifics>
## Specific Ideas

Keine spezifischen Vorgaben.

</specifics>

<deferred>
## Deferred Ideas

Keine.

</deferred>
