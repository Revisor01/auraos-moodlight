---
task: 260730-s1m
title: index.html Systemstatus-Bereich beruhigen + Log-Umzug
subsystem: firmware/data (Web-UI)
tags: [ui, css-layout, reflow-fix, log-polling]
requirements: [QT-S1M-01, QT-S1M-02]
key-files:
  modified:
    - firmware/data/index.html
    - firmware/data/setup.html
    - firmware/data/css/style.css
    - firmware/data/js/script.js
    - firmware/data/js/setup.js
metrics:
  tasks_completed: 2
  tasks_total: 3
  files_modified: 5
  completed_date: 2026-07-30
---

# Quick Task 260730-s1m: index.html Systemstatus-Bereich beruhigen Summary

Feste 3-Spalten-Kachelraster + `tabular-nums`/`visibility`-Fixes beheben das Layout-Springen der System-Status-Karte beim 5-Sekunden-Refresh; die System-Log-Karte ist von der Startseite in den Info-Tab der Einstellungen umgezogen mit tab-gebundenem Polling-Lifecycle.

## Was wurde gebaut

**Task 1 — Status-Kacheln reflow-frei (Commit `a67c367`)**

- `firmware/data/css/style.css`:
  - `.stat-tiles`: `grid-template-columns` von `repeat(auto-fit, minmax(140px, 1fr))` auf `repeat(3, 1fr)` geändert — feste Spaltenzahl unabhängig davon, wie viele Kacheln gerade sichtbar sind. Bestehende Media-Queries (`repeat(2, 1fr)` ≤ 768px, `1fr` ≤ 479px) unverändert.
  - `.stat-tile`: `min-height: 76px` sowie `display: flex; flex-direction: column; justify-content: center;` ergänzt — verhindert Höhensprung zwischen ein- und zweizeiligen Werten (z. B. Platzhalter `--` vs. `21.4°C / 48.0%`).
  - `.stat-tile .stat-value`: `font-variant-numeric: tabular-nums`, `font-feature-settings: "tnum"` und `white-space: nowrap` ergänzt, Schriftgröße von `1.3rem` auf `1.15rem` reduziert (verhindert Überlauf bei `nowrap` in schmalen Spalten).
  - Neue Regel `.stat-tile .stat-value .badge`: `min-width: 4.5em; display: inline-block; text-align: center;` — verhindert Sprung der WLAN/MQTT-Kachel beim Wechsel von `--` auf `Online`/`Offline`.
- `firmware/data/js/script.js`: `updateStats()` — DHT-Zeile (`#dht-row`) schaltet jetzt `visibility: visible/hidden` statt `display: flex/none`. Die Kachel behält ihren Platz im Grid, kein Reflow mehr beim Ein-/Ausblenden.

**Task 2 — Log-Karte in Info-Tab verschoben (Commit `51d3469`)**

- `firmware/data/index.html`: Die komplette „System Log"-Karte (`<div class="card">` mit `#logContent` und Refresh-Button) entfernt. Dashboard besteht jetzt aus drei Karten: Weltlage, Steuerung, System Status.
- `firmware/data/setup.html`: Dieselbe Karte wörtlich unverändert (inkl. `id="logContent"`, `aria-live="polite"`, `onclick="refreshLog()"`, ARIA-Attribute) als zweite `.card` in `#about-tab` eingefügt, direkt nach der „Über Moodlight"-Karte.
- `firmware/data/js/script.js`: `hasLogContent`-Guard und automatischer Poll-Start in `window.onload` entfernt. `refreshLog()` (Funktion) und `let refreshLogInterval;` bleiben unverändert bestehen — werden jetzt extern von setup.js gesteuert. Kommentar aktualisiert, um den neuen tab-gebundenen Lifecycle zu erklären.
- `firmware/data/js/setup.js`:
  - Neue Funktionen `startLogPolling()` (prüft Existenz von `#logContent`, ruft `refreshLog()` sofort auf, startet 5s-Intervall) und `stopLogPolling()` (stoppt Intervall, setzt `refreshLogInterval = null`).
  - Im Tab-Klick-Handler von `pageInit()`: `stopLogPolling()` wird vor der if/else-Kette bei jedem Tab-Wechsel aufgerufen; im `about`-Zweig zusätzlich `startLogPolling()`.
  - Im Block für den initial aktiven Tab: `startLogPolling()` im `about`-Zweig ergänzt (falls die Seite direkt mit aktivem Info-Tab lädt).
  - Neuer `visibilitychange`-Listener: stoppt Polling wenn der Browser-Tab in den Hintergrund geht, setzt es beim Zurückkehren nur fort, wenn der Info-Tab weiterhin aktiv ist.

## Regressions-Guard (IDs/Handler-Wanderung)

- `#logContent` (Element) — von `index.html` nach `setup.html` (`#about-tab`) gewandert, unverändert vorhanden.
- Inline-Handler `onclick="refreshLog()"` — mitgewandert, unverändert.
- `refreshLog()` (Funktion) — bleibt in `script.js`, wird nicht dupliziert, nur der Aufrufer (script.js → setup.js) hat sich geändert.
- Alle neun Status-Element-IDs (`wifi-status`, `wifi-ind`, `mqtt-status`, `mqtt-ind`, `uptime`, `rssi`, `dht-row`, `dht`, `heap`) sind unverändert in `index.html` vorhanden — keine ID ist entfallen.

## Gewählte CSS-Werte

| Wert | Gewählt | Begründung |
|------|---------|------------|
| `.stat-tile min-height` | `76px` | Richtwert aus dem Plan übernommen, deckt zweizeilige Werte wie DHT ab |
| `.stat-tile .stat-value font-size` | `1.15rem` (vorher `1.3rem`) | Reduziert wegen `white-space: nowrap`, damit lange Werte (`21.4°C / 48.0%`) nicht in schmalen Spalten überlaufen |
| `.badge min-width` | `4.5em` | Richtwert aus dem Plan übernommen, deckt `Offline` (längstes Wort) ab |
| `.stat-tiles grid-template-columns` | `repeat(3, 1fr)` | Für 6 Kacheln (Dashboard) ergibt das 3×2, für 4 Kacheln (Info-Tab) 3+1 — laut Plan akzeptabel |

Keine der optionalen Nachjustierungen aus dem Plan (z. B. `grid-column: span 3` für die letzte Info-Tab-Kachel) war nötig — der Plan verlangte explizit, diese NICHT vorzunehmen, da Sprungfreiheit wichtiger als Symmetrie ist.

## Offene Punkte

**Task 3 (Human-Verify-Checkpoint) — NICHT ausgeführt, offen:**

Der Plan enthält einen `checkpoint:human-verify`-Task mit neun manuellen Prüfschritten (Sicht-Check der Status-Karte über 30s, Badge-Sprungfreiheit, DHT-Toggle, Responsive-Breakpoints, Abwesenheit der Log-Karte auf der Startseite, Vorhandensein + Funktion im Info-Tab, kein Hintergrund-Polling bei anderem Tab, Wiederaufnahme des Pollings beim Zurückwechseln, Dark-Mode-Gegenprobe). Dieser Checkpoint erfordert einen OTA-Upload der UI auf das Gerät (192.168.0.37) und eine visuelle Prüfung durch den Nutzer — beides kann dieser Executor nicht durchführen. Siehe `260730-s1m-PLAN.md`, Task 3, für die vollständige Prüfliste.

Da kein Firmware-/Version-Bump und kein UI-Deployment Teil dieses Tasks war (laut Plan-Vorgabe „Das Deployment übernimmt der Orchestrator nach der Freigabe"), ist dieser Schritt bewusst nicht Teil dieser Ausführung.

## Verifikation

Alle automatisierten Checks aus Task 1 und Task 2 wurden einzeln (nicht als verkettete Shell-Befehle, da der Worktree-Sandbox-Guard komplexe Befehlsketten blockiert) ausgeführt und bestanden:

- `auto-fit` kommt in `style.css` nicht mehr als CSS-Wert vor (Kommentartext wurde entsprechend umformuliert)
- `tabular-nums` vorhanden
- `visibility` in `script.js` vorhanden, `dhtRow.style.display` entfernt
- Alle neun Status-Element-IDs unverändert in `index.html`
- `logContent`/`refreshLog` nicht mehr in `index.html`
- `id="logContent"` und `onclick="refreshLog()"` in `setup.html` vorhanden
- `startLogPolling`/`stopLogPolling` in `setup.js` vorhanden, `tabId === 'about'`-Verdrahtung und `visibilitychange`-Listener vorhanden
- `refreshLog`-Funktion weiterhin in `script.js`, `hasLogContent` entfernt
- `node --check` auf `script.js` und `setup.js` erfolgreich (syntaktisch valide)

## Deviations from Plan

None — Plan wurde 1:1 umgesetzt. Einzige Abweichung ist rein kosmetisch: der ursprünglich geschriebene Kommentar in `style.css` enthielt noch das Wort „auto-fit" im Fließtext, was den automatisierten `grep -c 'auto-fit' | grep -qx '0'`-Check fehlschlagen ließ. Der Kommentar wurde umformuliert (kein CSS-Wert betroffen), Ursache: [Rule 1 - Bug] Verify-Gate-Konflikt durch eigenen Kommentartext, sofort behoben, kein separater Commit nötig (im selben Task-1-Commit `a67c367` enthalten).

## Self-Check: PASSED

- FOUND: firmware/data/css/style.css (Task 1 Änderungen enthalten)
- FOUND: firmware/data/js/script.js (Task 1 + Task 2 Änderungen enthalten)
- FOUND: firmware/data/index.html (Log-Karte entfernt)
- FOUND: firmware/data/setup.html (Log-Karte im Info-Tab)
- FOUND: firmware/data/js/setup.js (Polling-Lifecycle)
- FOUND: Commit a67c367 (git log --oneline)
- FOUND: Commit 51d3469 (git log --oneline)
