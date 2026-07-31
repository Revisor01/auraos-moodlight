---
phase: quick-260731-fdf
plan: 01
subsystem: firmware-web-ui
tags: [mood.html, design-system, chart.js, dashboard-alignment]
dependency-graph:
  requires: [firmware/data/css/style.css]
  provides: [mood.html-dashboard-aligned]
  affects: [firmware/data/mood.html, firmware/data/css/mood.css, firmware/data/js/mood.js]
tech-stack:
  added: []
  patterns:
    - "CSS-Variablen-Aliasing (Legacy-Namen -> style.css-Tokens) statt eigener Farbpalette"
    - "hexToRgba()-Helfer statt fest kodierter rgba()-Literale in Chart.js-Datasets"
    - "getComputedStyle()-Auslesen von CSS-Variablen fuer Chart.js-Achsenfarben (Dark-Mode)"
key-files:
  created: []
  modified:
    - firmware/data/mood.html
    - firmware/data/css/mood.css
    - firmware/data/js/mood.js
decisions:
  - "Gesamtverlauf (720h) ist jetzt Standard-Tab statt Letzte Woche (168h)"
  - "mood.css definiert keine eigene Farbpalette mehr, sondern aliast auf style.css-Tokens"
  - "Score-Schwellwerte in getColorBasedOnValue() an scoreClass() im Backend-Dashboard angeglichen"
metrics:
  duration: "~35min"
  completed: "2026-07-31"
---

# Phase quick-260731-fdf Plan 01: mood.html an Dashboard-Design-Sprache angleichen Summary

mood.html/mood.css/mood.js auf die vereinheitlichten style.css-Tokens (Violett #8A2BE2,
Inter/JetBrains Mono) umgestellt und Gesamtverlauf (720h) zur Standard-Ansicht gemacht.

## Was wurde gebaut

**Task 1 — Gesamtverlauf als Standard-Ansicht (720h):**
- `#all-tab-link` ist jetzt beim Laden `class="active"` / `aria-selected="true"`,
  `#week-tab-link` entsprechend deaktiviert; Panel `#all-tab` aktiv statt `#week-tab`.
- `currentHours`-Default in mood.js von 168 auf 720 gesetzt; `loadData(hours)`-Fallback
  ebenfalls auf 720 angepasst, damit Auto-Refresh (5 Min) denselben Zeitraum lädt.
- Zeitraum-Auswahl (Gesamter Zeitraum / Letzte Woche / Letzter Tag) bleibt vollständig
  funktionsfähig, Cache-Logik (`loadedHoursSet`) unverändert.

**Task 2 — mood.css an Dashboard-Design-Sprache angleichen:**
- `:root`-Block in mood.css setzt keine eigenen Farbwerte mehr, sondern aliast alle
  Legacy-Variablennamen (`--primary-color`, `--dark-color`, `--neutral-color`, etc.) auf
  die style.css-Tokens (`var(--primary)`, `var(--text)`, `var(--text-muted)`, ...).
- Der doppelte `.dark { … }`-Token-Block wurde komplett entfernt — Dark Mode läuft jetzt
  automatisch über die `.dark`-Overrides in style.css.
- Harte Randfarben `rgba(189, 195, 199, 0.2)` durch `var(--border)`, graue Flächen
  `rgba(189, 195, 199, 0.1)` durch `var(--bg)` ersetzt (Karten, Tabs, Filter, Tabellen,
  Progress-Bars — inkl. `.upload-progress`, die zusätzlich zu den im Plan explizit
  genannten Stellen dasselbe Muster aufwies und für die Verifikation ebenfalls
  umgestellt werden musste).
- Statistik-/Storage-Werte nutzen jetzt `var(--font-mono)` mit `tabular-nums`.
- `h2` von 1.5rem auf 1.1rem reduziert (Mobile-Overrides 1.3rem→1.05rem, 1.2rem→1rem).
- Stat-/Info-Icons auf `var(--primary)` / `var(--success)` / `var(--danger)` umgestellt.
- Perzentil-Abschnitt (Inline-`<style>` in mood.html) nutzt jetzt `--card-gradient`,
  `--card-shadow`, `--radius-lg`, `--font-mono`, `--warning` statt fest kodierter Werte;
  `.dark .fallback-hint`-Regel entfernt (durch rgba-Warnfarbe überflüssig geworden).
- Zeitraum-Badge `#range-badge` neben „Stimmungsverlauf" ergänzt (Klasse `.badge` aus
  style.css), aktualisiert sich in `setupTabs()` beim Tab-Wechsel via `textContent`.

**Task 3 — Chart-Farben auf Dashboard-Score-Palette umstellen:**
- Zentrale `SCORE_COLORS`-Konstante (5 Stufen) + `PRIMARY_COLOR` + `hexToRgba()`-Helfer
  am Kopf von mood.js ergänzt.
- `getColorBasedOnValue()` nutzt jetzt die 5 Dashboard-Score-Stufen mit identischen
  Schwellwerten wie `scoreClass()` im Backend-Dashboard (>= 0.30 sehr positiv, >= 0.10
  positiv, >= -0.20 neutral, >= -0.50 negativ, sonst sehr negativ).
- `getAllTimeData()`, `getDayData()`, `getTrendData()` (Hauptlinie) nutzen die
  Primary-Farbe #8A2BE2 statt Blau/Violett-Mix; Trendlinie auf Neutral-Blau (#3498db)
  statt Rot (Rot ist im Dashboard „sehr negativ" reserviert).
- `getDistributionData()` bildet die 7 Buckets auf die 5 Score-Stufen ab (äußere Buckets
  teilen sich die Endfarbe mit abgestufter Deckkraft 0.7/0.9).
- `createLegend()` zeigt 5 statt 6 Kategorien mit den neuen Schwellwerten.
- `commonLineChartOptions`/`commonBarChartOptions` zu Funktionen (`getCommonLineChartOptions()`,
  `getCommonBarChartOptions()`) umgebaut, die Achsen-Tick-/Gridfarben zur Laufzeit aus
  `getComputedStyle(document.body)` für `--text-muted`/`--border` lesen — Dark-Mode-Lesbarkeit
  für Achsenbeschriftungen und Gitterlinien. `getDistributionOptions()` analog erweitert.
  Alle sieben `createOrUpdateChart`-Aufrufe auf Funktionsaufruf statt Referenz umgestellt.

Geändert wurden ausschließlich Web-Dateien — kein Firmware-Build, kein Version-Bump.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `.upload-progress` nutzte dieselbe harte Graufarbe wie die im Plan
explizit genannten Stellen**
- **Found during:** Task 2
- **Issue:** `firmware/data/css/mood.css` Zeile 509 (`background-color: rgba(189, 195, 199, 0.2)`)
  war im Plan nicht explizit aufgeführt, folgt aber demselben Muster (harte Randfarbe/Fläche
  statt Design-Token) wie die genannten Stellen. Die Verifikations-Prüfung sucht global nach
  `rgba(189, 195, 199` und hätte sonst fehlgeschlagen.
- **Fix:** `var(--border)` als Hintergrund der Progress-Bar-Spur gesetzt (konsistent mit dem
  übrigen Umbau).
- **Files modified:** `firmware/data/css/mood.css`
- **Commit:** b3cdee8

Keine weiteren Abweichungen — die übrigen Tasks folgen dem Plan wie geschrieben.

## Bekannte Abweichung von der Erwartung im Verifikationstext (kein Blocker)

Der Plan-Verifikationstext nannte als Erwartung, dass die Summe der drei Dateien durch
Entfernen der CSS-Duplikate „eher kleiner" wird (Ausgangswert 71953 Bytes). Tatsächliches
Ergebnis nach allen drei Tasks:

```
23532 firmware/data/mood.html
16114 firmware/data/css/mood.css
34550 firmware/data/js/mood.js
74196 total
```

Das ist ein Zuwachs von ca. 2243 Bytes (+3,1%). Ursache: Task 3 fügt mehr Code hinzu
(zentrale Farbkonstante, `hexToRgba()`-Helfer, zwei neue Options-Funktionen mit
`getComputedStyle()`-Logik) als Task 2 durch das Entfernen des `.dark`-Duplikat-Blocks
und der doppelten `--card-gradient`/`--card-shadow`-Definitionen einspart. Das ist eine
harte Zahlenabweichung vom Verifikationstext, aber kein funktionaler oder LittleFS-Kapazitäts-
Blocker — 74196 Bytes bleiben für die `min_spiffs`-Partition unkritisch. Keine externen
Assets wurden hinzugefügt (`grep -c "https://cdnjs\|https://fonts"` weiterhin 2, unverändert).

## Threat Flags

Keine neuen Trust-Boundaries oder Netzwerk-/Auth-Pfade eingeführt. `renderHeadlines()`
bleibt textContent-basiert (nur `container.innerHTML = ''` als Clear-Aufruf, keine
Fremdtext-Injection-Fläche) — Mitigation T-fdf-01 aus dem Plan bestätigt unverändert.

## Offener Punkt: Task 4 (Human-Verify-Checkpoint)

Task 4 ist ein `checkpoint:human-verify`-Gate (Sicht-Check am Gerät) und wurde gemäß
Anweisung NICHT automatisch ausgeführt. Ausstehend:

1. UI-Deploy per OTA auf das Gerät (http://192.168.0.37) durch den Orchestrator/User.
2. Sicht-Check von: Standard-Ansicht (Gesamter Zeitraum aktiv), Kartenoptik (Perzentil-
   Abschnitt vs. übrige Karten), Chart-Farben (violett/blau/orange/rot), Tab-Wechsel +
   Badge-Update, Dark Mode in allen Abschnitten, Funktionsfähigkeit (Schlagzeilen,
   Zusammenfassung, Datenpunkte-Zähler), Mobile-Layout.
3. Details siehe `<how-to-verify>` in Task 4 des Plans
   (`.planning/quick/260731-fdf-mood-html-statistik-auf-dem-ger-t-ans-la/260731-fdf-PLAN.md`).

## Self-Check: PASSED

- FOUND: firmware/data/mood.html
- FOUND: firmware/data/css/mood.css
- FOUND: firmware/data/js/mood.js
- FOUND: commit 33ddde9 (Task 1)
- FOUND: commit b3cdee8 (Task 2)
- FOUND: commit a5d14c1 (Task 3)
