# Phase 5: Diagnostics-UI - Context

**Gathered:** 2026-03-26
**Status:** Ready for planning
**Mode:** Auto-generated (UI changes well-specified in requirements)

<domain>
## Phase Boundary

Die Update-Sektion in diagnostics.html bekommt einen einzigen Datei-Picker und "Full Update"-Button. Drei Requirements:

1. **UI-01**: Ein Datei-Picker + "Full Update"-Button — führt sequentiell erst POST /update/combined-ui, dann POST /update/combined-firmware aus
2. **UI-02**: Fortschrittsanzeige zeigt welcher Schritt läuft (1/2 UI-Installation, 2/2 Firmware-Flash)
3. **UI-03**: Eine einzige Versionsanzeige (nicht getrennt Firmware/UI) — liest von /api/firmware-version oder /api/ui-version

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
All implementation choices at Claude's discretion. Key constraints:

- **Bestehende Update-Sektionen beibehalten** als Fallback (Legacy UI-Upload + Legacy Firmware-Upload), aber visuell als "Advanced" oder eingeklappt
- **Sequentieller AJAX-Upload**: Erst XHR/fetch an `/update/combined-ui`, nach Erfolg an `/update/combined-firmware`
- **Fortschrittsanzeige**: Einfache Text/Status-Updates (Step 1/2, Step 2/2), kein komplexes Progress-Bar-Widget nötig
- **Versionsanzeige**: Eine Zeile "Aktuelle Version: X.X - AuraOS" statt getrennte Firmware/UI-Versionen
- **Stil**: Bestehende CSS-Klassen und Design-Sprache der diagnostics.html beibehalten (Dark-Mode-Kompatibel)
- **Gerät startet nach Firmware-Flash automatisch neu** — UI muss darauf hinweisen

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `firmware/data/diagnostics.html` — bestehende Update-Sektion wird erweitert
- `firmware/data/css/style.css` — bestehende Styles
- `/api/firmware-version` und `/api/ui-version` — bestehende Version-Endpoints
- Bestehende AJAX-Patterns in diagnostics.html (fetch/XHR für Status-Refresh etc.)

### Integration Points
- `firmware/data/diagnostics.html` — Update-Sektion
- Neue Endpoints: `/update/combined-ui` (POST), `/update/combined-firmware` (POST)
- Version-Endpoints: `/api/firmware-version` (GET), `/api/ui-version` (GET)

</code_context>

<specifics>
## Specific Ideas

No specific requirements beyond what's in the ROADMAP success criteria.

</specifics>

<deferred>
## Deferred Ideas

None.

</deferred>
