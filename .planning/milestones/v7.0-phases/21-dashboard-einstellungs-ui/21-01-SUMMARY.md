---
phase: 21-dashboard-einstellungs-ui
plan: 01
subsystem: ui
tags: [dashboard, html, javascript, settings, forms]

# Dependency graph
requires:
  - phase: 20-manueller-analyse-trigger
    provides: dashboard.html mit Tab-Navigation und showTab()-Mechanik
  - phase: 19-einstellungs-persistenz
    provides: PUT/GET /api/moodlight/settings API-Endpunkte
provides:
  - Vierter Tab "Einstellungen" in dashboard.html mit Tab-Button und Tab-Pane
  - CSS-Klassen fuer Einstellungs-Formulare (.settings-section, .form-group, .settings-input, .btn-save, .settings-msg, .api-key-row, .btn-edit-key)
  - loadSettings() laedt GET /api/moodlight/settings lazy beim ersten Tab-Oeffnen
  - saveAnalysis() schickt PUT mit analysis_interval_minutes und headlines_per_source
  - toggleApiKeyEdit() / saveAuth() fuer maskierten API Key mit Klartext-Editierung
  - savePassword() mit clientseitiger Validierung und 403-Fehlerbehandlung
  - showSettingsMsg() Hilfsfunktion fuer einheitliche Feedback-Meldungen
affects: [22-dashboard-erweiterungen, sentiment-api-deployment]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Lazy-Load-Flag pro Tab (settingsLoaded) konsistent mit overviewLoaded/headlinesLoaded/feedsLoaded
    - onclick-Attribut fuer Settings-Buttons (konsistent mit triggerAnalysis-Pattern)
    - API Key maskiert in readonly password-Feld, Editier-Modus per toggleApiKeyEdit()

key-files:
  created: []
  modified:
    - sentiment-api/templates/dashboard.html

key-decisions:
  - "Einstellungs-Tab folgt dem bestehenden Lazy-Load-Muster mit settingsLoaded-Flag — kein separates loadSettings() beim Seitenstart"
  - "API Key im readonly password-Feld angezeigt (maskiert), nur beim Klick auf Bearbeiten als Klartext editierbar — konsistent mit v7.0-Entscheidung"
  - "saveAuth() und saveAnalysis() als separate Funktionen pro Sektion — kein kombiniertes Speichern"

patterns-established:
  - "Settings-Formular-Pattern: .settings-section > .form-group > label + input.settings-input + span.hint + .btn-save + .settings-msg"
  - "showSettingsMsg(el, type, text) als zentraler Helfer fuer alle Einstellungs-Meldungen"

requirements-completed: [UI-01, UI-02, UI-03, UI-04, UI-05]

# Metrics
duration: 12min
completed: 2026-03-27
---

# Phase 21 Plan 01: Dashboard Einstellungs-Tab Summary

**Vierter Dashboard-Tab mit drei Formularsektionen (Analyse, API Key, Passwort) direkt verdrahtet auf PUT /api/moodlight/settings**

## Performance

- **Duration:** 12 min
- **Started:** 2026-03-27T11:30:00Z
- **Completed:** 2026-03-27T11:42:00Z
- **Tasks:** 2
- **Files modified:** 1

## Accomplishments
- Tab-Button "Einstellungen" als vierter Tab in der bestehenden nav.tabs-Leiste
- Tab-Pane #tab-settings mit drei .settings-section-Bloecken (Analyse, Authentifizierung, Passwort)
- CSS-Klassen vollstaendig definiert ohne Kollision mit bestehenden Regeln
- lazy loadSettings() beim ersten Tab-Oeffnen (settingsLoaded-Flag-Pattern)
- saveAnalysis() mit clientseitiger Validierung und direktem PUT-Request
- API Key maskiert/editierbar via toggleApiKeyEdit() + saveAuth()
- Passwort-Aenderung mit Uebereinstimmungspruefung, Mindestlaenge und 403-Behandlung

## Task Commits

Beide Tasks wurden in einem Commit zusammengefasst (gleiche Datei, atomare Aenderung):

1. **Task 1: CSS-Klassen** + **Task 2: HTML/JS** - `d074d61` (feat)

**Plan metadata:** (folgt)

## Files Created/Modified
- `sentiment-api/templates/dashboard.html` - Einstellungs-Tab mit CSS, HTML und JavaScript hinzugefuegt (404 neue Zeilen)

## Decisions Made
- Beide Tasks in einem Commit zusammengefasst, da sie dieselbe Datei betreffen und inhaltlich untrennbar sind
- CSS-Block zwischen @keyframes spin und @media-Query eingefuegt — kein Konflikt mit bestehenden Regeln

## Deviations from Plan

None - Plan exakt wie beschrieben ausgefuehrt. Alle CSS-Klassen, HTML-Strukturen und JavaScript-Funktionen entsprechen den Planvorgaben.

## Issues Encountered

None.

## User Setup Required

None - keine externen Dienste konfiguriert. Der Tab nutzt ausschliesslich die bereits in Phase 19 implementierte /api/moodlight/settings API.

## Next Phase Readiness
- Einstellungs-Tab vollstaendig in dashboard.html integriert
- Alle fuenf Requirements (UI-01 bis UI-05) abgedeckt
- Bereit fuer Deployment auf server.godsapp.de

---
*Phase: 21-dashboard-einstellungs-ui*
*Completed: 2026-03-27*
