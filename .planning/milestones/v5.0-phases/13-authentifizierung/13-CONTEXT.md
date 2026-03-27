# Phase 13: Authentifizierung - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Einfacher Passwort-Schutz für das Backend-Interface. Nicht eingeloggte Benutzer werden auf eine Login-Seite weitergeleitet. POST/DELETE-Endpoints geben 401 zurück ohne gültigen Session-Cookie. Lese-Endpoints (GET /api/moodlight/current, /history) bleiben öffentlich für ESP32-Zugriff.

</domain>

<decisions>
## Implementation Decisions

### Authentifizierungs-Ansatz
- Einfacher Passwort-Login — kein OAuth, kein Authentik, keine Benutzer-Verwaltung
- Ein Passwort für alle, konfigurierbar über Umgebungsvariable
- Flask-Session mit Secret Key für Cookie-basierte Sitzungen
- Passwort als Hash gespeichert (werkzeug.security)

### Geschützte vs. öffentliche Routen
- Geschützt: /feeds, /dashboard, POST/DELETE /api/moodlight/feeds, alle neuen Dashboard-Routen
- Öffentlich: GET /api/moodlight/current, GET /api/moodlight/history, GET /api/moodlight/feeds (Lesezugriff)
- Login-Seite: GET /login, POST /login

### Claude's Discretion
- Session-Timeout (z.B. 24h)
- Login-Seite Design
- Ob Flask-Login oder eigener Decorator

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/app.py — Flask App mit bestehenden Routen
- sentiment-api/moodlight_extensions.py — /api/moodlight/* Endpoints
- sentiment-api/templates/feeds.html — Bestehende HTML-Seite (braucht Auth-Integration)

### Established Patterns
- Flask mit render_template, jsonify
- .env Datei für Konfiguration (OPENAI_API_KEY, POSTGRES_PASSWORD etc.)
- docker-compose.yaml für Umgebungsvariablen

### Integration Points
- app.py: Login-Route + Session-Management
- moodlight_extensions.py: Auth-Decorator für POST/DELETE Endpoints
- templates/feeds.html: Redirect wenn nicht eingeloggt
- docker-compose.yaml: ADMIN_PASSWORD Umgebungsvariable

</code_context>

<specifics>
## Specific Ideas

- Passwort über ADMIN_PASSWORD Umgebungsvariable in docker-compose.yaml
- @login_required Decorator für geschützte Routen
- Einfache Login-Seite mit deutschem UI-Text

</specifics>

<deferred>
## Deferred Ideas

Keine — Phase bleibt im Scope.

</deferred>
