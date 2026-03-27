# Phase 21: Dashboard Einstellungs-UI - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Neuer "Einstellungen"-Tab im Dashboard. Zeigt alle konfigurierbaren Parameter: Analyse-Frequenz, Headlines pro Quelle, Anthropic API Key (maskiert), Admin-Passwort. Nutzt GET/PUT /api/moodlight/settings aus Phase 19.

</domain>

<decisions>
## Implementation Decisions

### Design
- Vierter Tab "Einstellungen" nach Übersicht/Schlagzeilen/Feeds
- Konsistentes Design mit bestehendem Dashboard (CSS-Variablen, Karten-Layout)
- Formular-Gruppen: Analyse, Authentifizierung
- Speichern-Button pro Sektion oder ein globaler Speichern-Button
- Erfolgs-/Fehlermeldung nach Speichern

### API Key Handling
- Maskiert anzeigen: sk-ant-...****
- "Bearbeiten"-Button zeigt Klartext-Eingabefeld
- Leeres Feld = Key nicht ändern

### Passwort-Änderung
- Altes Passwort zur Bestätigung
- Neues Passwort + Bestätigung
- Fehlermeldung bei falschem alten Passwort

### Claude's Discretion
- Genaues Layout
- Validierungsmeldungen
- UX-Details

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/templates/dashboard.html — 3 Tabs (Übersicht, Schlagzeilen, Feeds)
- GET /api/moodlight/settings — liefert aktuelle Werte
- PUT /api/moodlight/settings — aktualisiert Werte

### Integration Points
- dashboard.html: Neuer Tab-Button + Tab-Content
- showTab() JS-Funktion erweitern

</code_context>

<specifics>
## Specific Ideas

Deutsche UI-Texte mit korrekten Umlauten.

</specifics>

<deferred>
## Deferred Ideas

Keine.

</deferred>
