---
phase: quick-260730-r7v
plan: 01
subsystem: ui
tags: [css-custom-properties, google-fonts, esp32-web-ui, littlefs]

requires:
  - phase: quick-260327-fnj
    provides: mood.html-Redesign (Referenz-Look für Karten, Kacheln, Pill-Tabs)
provides:
  - "index.html (Steuerung/Status/Logs) und setup.html (Settings) im mood.html-Look"
  - "Design-Token-Fundament in style.css (--font-sans, --font-mono, --card-gradient, --card-shadow, --radius-lg)"
  - "Bugfix: #version-Header auf setup.html zeigt echte Version statt dauerhaft „Lädt...“"
affects: [ui, firmware-data]

tech-stack:
  added: ["Google Fonts CDN (Inter, JetBrains Mono) via nicht-blockierendem media=print onload-Pattern"]
  patterns:
    - "Karten mit --card-gradient/--card-shadow(-hover) + Hover-Lift (translateY) statt Flächenfarbe"
    - ".stat-tiles/.stat-tile-Grid für Kachel-Statuswerte statt .stat-Zeilen"
    - ".card-icon: runder farbig hinterlegter Icon-Kreis vor Karten-/Tab-Überschriften"
    - ".nav-tabs als Pill-Container (gefüllte aktive Pille) statt Unterstrich-Tabs"

key-files:
  created: []
  modified:
    - firmware/data/css/style.css
    - firmware/data/js/setup.js
    - firmware/data/index.html
    - firmware/data/setup.html

key-decisions:
  - "Font-Einbindung exakt wie in Constraints festgelegt: Inter + JetBrains Mono über Google-Fonts-CDN, media=print/onload nicht-blockierend, System-Font-Fallback zuerst im --font-sans/--font-mono-Stack für Captive-Portal-Tauglichkeit"
  - "Perzentil-Sektion und #weltlage-card in index.html bewusst nicht umgebaut (war bereits im mood.html-Stil) — nur inline style=\"padding:28px;\" entfernt"
  - "--transition-speed-Legacy-Alias von var(--transition) auf 0.3s umgestellt (mood.html-Wert), da beide Namen nicht parallel als Alias auf unterschiedliche Werte zeigen können"

patterns-established:
  - "Neue generische Hilfsklassen (.card-icon, .stat-tiles/.stat-tile, .control-group) sind seiten-/kartenunabhängig wiederverwendbar für künftige UI-Erweiterungen"

requirements-completed: [UI-01, UI-02, UI-03, BUG-01]

duration: 25min
completed: 2026-07-30
---

# Quick Task 260730-r7v: UI-Redesign index.html/setup.html Summary

**index.html und setup.html im mood.html-Kartenlook (Verlauf, Hover-Lift, Icon-Kreise, Kachel-Stats, Pill-Tabs) mit Inter/JetBrains Mono via Google Fonts, plus Bugfix für die dauerhaft „Lädt...“ anzeigende Versionsnummer im setup.html-Header.**

## Performance

- **Duration:** 25 min
- **Started:** 2026-07-30T17:24:00Z
- **Completed:** 2026-07-30T17:49:52Z
- **Tasks:** 2 von 3 (Task 3 ist Human-Verify-Checkpoint, siehe unten)
- **Files modified:** 4

## Accomplishments
- Bugfix BUG-01: `#version` im setup.html-Header wird jetzt beim Seitenaufruf befüllt (unabhängig vom aktiven Tab), mit Null-Check und neutralem Fehler-Fallback statt dauerhaft „Lädt...“
- Design-Token-Fundament in `style.css` im mood.html-Look übernommen: `--font-sans`/`--font-mono`, `--card-gradient`, `--card-shadow`/`-hover`, `--radius-lg`, Pill-Tabs, neue Hilfsklassen `.card-icon`/`.stat-tiles`/`.stat-tile`/`.control-group`
- `index.html` (Steuerung, Status, Logs) und `setup.html` (alle acht Tabs) vollständig auf den neuen Kartenlook umgestellt, inklusive nicht-blockierender Google-Fonts-Einbindung mit System-Font-Fallback
- Regressions-Guard verifiziert: alle 94 IDs und 26 Inline-Handler aus dem Original-Stand (`c7bf13c`) sind unverändert erhalten

## Task Commits

Each task was committed atomically:

1. **Task 1: Bugfix „Lädt…“ + Design-Tokens und Kern-Stile in style.css** - `567bcd1` (fix)
2. **Task 2: Markup-Redesign index.html + setup.html inkl. Font-Einbindung** - `dc81d1a` (feat)

**Plan metadata:** wird vom Orchestrator committed (SUMMARY.md, STATE.md)

## Files Created/Modified
- `firmware/data/js/setup.js` - `loadSystemInfo()` befüllt `#version` im Header (Null-Check + Fehler-Fallback), `pageInit()` ruft `loadSystemInfo()` unabhängig vom aktiven Tab auf
- `firmware/data/css/style.css` - Design-Tokens (Font-Stacks, Karten-Gradient/Schatten, Radius), `.card`/`.card:hover`, Pill-`.nav-tabs`, `.stat-label`-Typografie, neue Hilfsklassen `.card-icon`/`.stat-tiles`/`.stat-tile`/`.control-group`/`.card-header`, responsive Anpassungen (Karten-Padding 20px/16px, `.stat-tiles` 2-/1-spaltig)
- `firmware/data/index.html` - Font-Einbindung im `<head>`, Steuerung-Karte mit Icon-Kreis + `.control-group`, System-Status als `.stat-tiles`-Kacheln, System-Log mit Icon-Kreis, inline-Padding an `#weltlage-card` entfernt
- `firmware/data/setup.html` - Font-Einbindung im `<head>`, alle acht `card-header`-Überschriften mit thematischen Icon-Kreisen (WLAN, MQTT, API, DHT, Farben, Update, Hardware, Info), Info-Tab-Versionsfelder als `.stat-tiles`-Kacheln umgebaut

## Decisions Made
- Font-Stack-Reihenfolge und Einbindungsmuster exakt nach Plan-Constraint 4 umgesetzt (keine eigene Neubewertung nötig)
- `--transition-speed` als Legacy-Alias auf den mood.html-Wert `0.3s` umgestellt statt eine zweite parallele Deklaration anzulegen (Plan-Vorgabe, verhindert doppelte Custom-Property)
- Perzentil-Sektion/`#weltlage-card` unangetastet gelassen — war bereits im Zielstil, nur das inline `padding:28px;` entfernt, damit das neue `.card`-Padding (24px) greift

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None. Eine organisatorische Anmerkung: Das PLAN.md für diese Quick-Task existierte im Haupt-Checkout, aber nicht im für diese Ausführung zugewiesenen Git-Worktree (Worktree wurde offenbar vor dem Anlegen des Plans erstellt). Der Plan wurde vor Ausführungsbeginn 1:1 in den Worktree kopiert (`.planning/quick/260730-r7v-.../260730-r7v-PLAN.md`), Inhalt unverändert. Kein Deviation im Sinne der Ausführungsregeln, da keine Code-Entscheidung betroffen war.

## User Setup Required

None - keine externe Service-Konfiguration nötig. Die neue Google-Fonts-CDN-Einbindung erfordert keine Zugangsdaten.

## Next Phase Readiness

**Task 3 (Human-Verify-Checkpoint) ist NICHT ausgeführt und steht noch offen.** Gemäß Auftrag wird sie hier nur dokumentiert, nicht selbst durchgeführt:

### Offener Checkpoint: Human-Verify

**Was gebaut wurde:** Redesign von `index.html` und `setup.html` im mood.html-Stil (Karten mit Verlauf und Hover-Lift, Icon-Kreise, Kachel-Statuswerte, Pill-Tabs, Inter + JetBrains Mono via Google-Fonts-CDN mit System-Font-Fallback) sowie Bugfix des dauerhaften „Lädt…“ im setup.html-Header.

**Wie zu verifizieren:** Die Dateien liegen nur lokal (Worktree) — zum Prüfen entweder die UI per OTA-Update-Tab auf das Gerät (http://192.168.0.37) hochladen oder `firmware/data/` lokal mit einem statischen Webserver öffnen (dann zeigen die Geräte-API-Aufrufe erwartungsgemäß Fehler, das Layout ist aber beurteilbar).

Bitte prüfen:
1. **setup.html Header:** Steht dort jetzt eine Versionsnummer (z. B. „v9.12“) statt „Lädt...“ — direkt beim Öffnen, ohne vorher auf den Info-Tab zu klicken?
2. **index.html Steuerung:** Licht-Toggle, Auto-Modus-Toggle, Helligkeitsregler (Wert-Anzeige aktualisiert sich beim Ziehen) und Farbauswahl funktionieren wie vorher?
3. **index.html Status:** Werden WLAN, MQTT, Uptime, WiFi-Signal, Temp/Luftfeuchte und Speicher als Kacheln korrekt gefüllt? Blendet sich die DHT-Kachel bei deaktiviertem Sensor weiterhin aus?
4. **index.html Logs:** Log wird geladen, ist mehrzeilig lesbar, „Log aktualisieren“ funktioniert?
5. **setup.html Tabs:** Alle acht Tabs (WLAN, MQTT, Einstellungen, Farben, Update, Hardware, Info) schalten um und laden ihre Daten; Speichern-Buttons reagieren?
6. **Optik:** Wirken index.html und setup.html jetzt stilistisch wie mood.html? Gefällt die Font?
7. **Dark Mode:** Umschalten auf beiden Seiten testen — alles gut lesbar?

**Resume-Signal:** Mit „approved“ bestätigen oder konkrete Abweichungen beschreiben.

### Automatisierte Verifikation (bereits durchgeführt, alle bestanden)
- Task-1-Check: `#version`-Befüllung, `pageInit()`-Aufruf, Design-Tokens vorhanden — OK
- Encoding-Check (keine kaputten Umlaute U+FFFD) — OK
- Task-2-Check: keine verlorenen IDs/Handler (gegen Baseline `c7bf13c`: 94 IDs / 26 Handler unverändert) — OK
- Font-Einbindung in beiden Dateien vorhanden — OK
- Flash-Budget: 89.424 B von 104.000 B (14.576 B Reserve) — OK
- div-Balance in beiden Dateien — OK
- `git diff --stat` zeigt ausschließlich die vier geplanten Dateien, keine Änderung an `mood.html`/`mood.css`/`mood.js`/`script.js` — OK

**Blocker:** Kein Firmware-Build und kein Deployment in diesem Task (laut Constraint 2) — das OTA-Update auf das Gerät und die visuelle Verifikation müssen vom User separat durchgeführt werden.

---
*Phase: quick-260730-r7v*
*Completed: 2026-07-30*

## Self-Check: PASSED

- FOUND: firmware/data/css/style.css
- FOUND: firmware/data/js/setup.js
- FOUND: firmware/data/index.html
- FOUND: firmware/data/setup.html
- FOUND: .planning/quick/260730-r7v-ui-redesign-index-html-steuerung-status-/260730-r7v-SUMMARY.md
- FOUND: commit 567bcd1 (Task 1)
- FOUND: commit dc81d1a (Task 2)
