# Phase 19: Einstellungs-Persistenz - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning
**Mode:** Infrastructure phase — discuss skipped

<domain>
## Phase Boundary

settings-Tabelle in PostgreSQL für alle Konfigurationsparameter. GET/PUT /api/moodlight/settings API. Beim Start aus DB lesen mit Env-Variable Fallback. Änderungen sofort wirksam ohne Container-Neustart (Worker-Intervall, API Key, Headlines-Anzahl).

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
Infrastruktur-Phase — alle Implementierungsentscheidungen nach eigenem Ermessen.

Key constraints:
- settings-Tabelle als Key-Value-Store (key VARCHAR, value TEXT, updated_at)
- Gespeicherte Keys: analysis_interval, headlines_per_source, anthropic_api_key, admin_password_hash
- Beim Start: DB zuerst, dann Env-Variablen als Fallback
- PUT /api/moodlight/settings muss @api_login_required haben
- GET /api/moodlight/settings: API Key maskiert zurückgeben (sk-ant-...****)
- Worker muss Intervall-Änderung zur Laufzeit übernehmen
- Admin-Passwort als Hash speichern (werkzeug generate_password_hash)

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/database.py — Database-Klasse
- sentiment-api/moodlight_extensions.py — @api_login_required Decorator
- sentiment-api/app.py — os.environ.get() Aufrufe für Config
- sentiment-api/background_worker.py — self.interval_seconds

### Integration Points
- database.py: Neue Methoden get_setting(), set_setting(), get_all_settings()
- moodlight_extensions.py: GET/PUT /api/moodlight/settings Endpoints
- app.py: Startup-Logik — DB-Settings laden statt nur Env-Variablen
- background_worker.py: Intervall dynamisch aus Settings lesen
- init.sql + Migration: settings-Tabelle

</code_context>

<specifics>
## Specific Ideas

Keine spezifischen Vorgaben.

</specifics>

<deferred>
## Deferred Ideas

Keine.

</deferred>
