# Changelog

Alle nennenswerten Änderungen an diesem Projekt werden hier dokumentiert.

Das Format orientiert sich an [Keep a Changelog 1.1.0](https://keepachangelog.com/de/1.1.0/),
die Versionierung folgt [Semantic Versioning 2.0.0](https://semver.org/lang/de/).

> **Hinweis zur Nummerierung:** Die Git-Tags `v1.0`–`v9.0` (25.–27. März 2026) sind
> *Planungs-Meilensteine* aus dem GSD-Workflow und **keine** Firmware-Versionen.
> Die Firmware-Versionen (`MOODLIGHT_VERSION` in `firmware/src/config.h`) laufen in
> einem eigenen Nummernkreis `9.x`. Ab v9.12 sind die Tags `v9.12`, `v9.13`, `v9.14`
> Firmware-Releases — die Meilenstein-Tags werden nicht fortgeführt.

## [Unreleased]

### Hinzugefügt
- Debug-Dokumentation für zwei abgeschlossene Diagnosen: spontane ESP32-Neustarts
  (`isRestartRecommended()` prüft Fragmentierung ohne geladene Uptime aus NVS) und
  rhythmisches LED-Pulsieren (`isPulsing` wurde beim Sentiment-Abruf gesetzt)
- Codebase-Analyse-Dokumente unter `.planning/codebase/` (Architektur, Konventionen,
  Struktur, Stack, Integrationen, Testing, Concerns)
- CHANGELOG.md (dieses Dokument), rückwirkend aus der Git-History rekonstruiert

### Geändert
- Meilenstein-Audits nach `.planning/milestones/` verschoben
- GSD-Konfiguration: Worktrees deaktiviert (`use_worktrees: false`)
- README inhaltlich richtiggestellt: nannte OpenAI GPT-4o-mini statt Anthropic
  Claude Haiku, Version v9.0, Python 3.11, `OPENAI_API_KEY`, die entfernte
  Diagnose-Seite und HA-Entitäten, die es so nie gab. Ergänzt: tatsächlicher
  Deploy-Weg über GHCR und Portainer-Webhook, OTA-Installation über den Update-Tab
- Projektseite: die zwölf Screenshots zeigten die UI vor dem Redesign in v9.13 und
  bewarben die entfernte Diagnose-Seite. Ersetzt durch vier aktuelle Aufnahmen vom
  Gerät unter Firmware v9.14 (MAC-Adresse maskiert)

### Entfernt
- `releases/v9.2/AuraOS-9.2.tgz` und `releases/v9.12/RELEASE-NOTES.md` aus dem
  Tracking — `releases/` steht in der .gitignore, die Binaries hängen an den
  GitHub-Releases, die Release-Notes sind in diesen CHANGELOG übernommen

### Sicherheit
- GitHub Vulnerability-Alerts, Dependabot Security Updates und CodeQL Default Setup
  für das Repository aktiviert
- Backend-Abhängigkeiten aktualisiert: Flask 3.1.0 → 3.1.3 (fehlender `Vary: Cookie`-
  Header, Fallback-Key statt aktuellem Signing-Key) und anthropic 0.86.0 → 0.87.0
  (Race Condition in der Pfadvalidierung des Memory Tools, unsichere Standard-
  Dateirechte im lokalen Filesystem-Memory-Tool)
- SSRF-Guard bei der Feed-Validierung greift jetzt auch für Redirects: Der Check lief
  nur gegen die Start-URL, `allow_redirects=True` erlaubte einem zugelassenen Host per
  302 die Umleitung auf `127.0.0.1` oder `169.254.169.254`. Redirects werden nun manuell
  verfolgt und jeder Hop erneut geprüft (max. 5 Weiterleitungen)
- `set_setting()` loggte jeden Wert im Klartext — darunter `anthropic_api_key` und
  `admin_password_hash`. Sensible Keys werden jetzt maskiert (nur Länge im Log)
- Sechs Endpoints gaben `str(e)` an den Client zurück und konnten dabei Interna aus
  psycopg2-Fehlern preisgeben (Hostnamen, Ports, Tabellennamen). Sie antworten jetzt
  mit „Interner Serverfehler"; der volle Trace bleibt im Log
- Subresource Integrity für die CDN-Skripte in `docs/index.html` und
  `firmware/data/mood.html` — ein kompromittiertes CDN kann keinen fremden Code mehr
  ausliefern. Der ungenutzte `chartjs-adapter-moment` wurde entfernt

## [9.14] – 2026-07-31

### Geändert
- Sentiment-Poll richtet sich am Analyse-Takt des Servers aus: Die Firmware berechnet
  aus dem Analyse-Zeitstempel der API-Antwort die Restzeit bis zur nächsten Analyse
  und pollt zu diesem Zeitpunkt plus 90 s Puffer. Anzeige und LED-Farbe liegen damit
  maximal ~2 Minuten statt bis zu 30 Minuten hinter der aktuellen Analyse.
  Sicherheitsnetze: Mindestabstand 60 s, Obergrenze bleibt das konfigurierte Intervall,
  bei API-Ausfall oder fehlender NTP-Zeit greift das bisherige Verhalten.

### Behoben
- Speichernutzung im Info-Tab als breite Grid-Kachel mit sichtbarem Fortschrittsbalken
- mood.html an die Design-Sprache des Dashboards angeglichen (mood.css, Chart-Farben
  auf die Score-Palette umgestellt), Gesamtverlauf (720 h) als Standard-Ansicht
- Status-Kacheln reflow-frei — kein Layout-Springen mehr im 5-Sekunden-Refresh
- System-Log von der Startseite in den Info-Tab der Einstellungen verschoben

## [9.13] – 2026-07-30

### Hinzugefügt
- Home-Assistant-Sensor `sensor.moodlight_weltlage_perzentil` (0–100 %): zeigt, wo der
  aktuelle Sentiment-Score im 7-Tage-Fenster liegt. Wird bei jedem Sentiment-Update
  publiziert, auch im Fehlerfall (Entität fällt nicht auf `unavailable`).

### Geändert
- UI-Redesign von Startseite und Einstellungen im Look der Weltlage-Ansicht:
  Gradient-Karten, Pill-Tabs, Icon-Kreise, Stat-Kacheln
- Typografie auf Inter + JetBrains Mono umgestellt (nicht-blockierend via CDN,
  System-Fallback im Captive-Portal ohne Internet, 0 Byte Flash-Verbrauch)

### Behoben
- Versionsanzeige im setup.html-Header

## [9.12] – 2026-07-29

Großes Stabilitäts- und Wartungsrelease. Zwei vollständige Code-Review-Runden über
Firmware, Backend und Web-UI mit über 60 umgesetzten Findings. Kern: die Ursache des
seit Monaten beobachteten LED-Pulsierens und der Spontan-Reboots ist gefunden.

### Hinzugefügt
- Perzentil-Visualisierung auf dem Haupt-Dashboard und in mood.html mit Score-Erklärung
- Read-only-Endpoint für den Neustart-Zähler sowie Endpoint zum Zurücksetzen
- Dynamische LED-Farben in der Statistik-Ansicht
- Backend: Redis-Cache (120 s) für History, Trend, Stats und Feed-Trends;
  gunicorn-Healthcheck im Compose, `.dockerignore`

### Geändert
- Statistik-Ladevorgang optimiert, Perzentil-Sektion nach oben verschoben,
  Filter für veraltete Daten entfernt
- Separate Diagnose-Seite entfernt — Inhalte im Update-/Info-Tab von setup.html
- Backend: Feeds werden parallel statt sequenziell geholt (vorher bis zu 15 s pro Feed)
- Backend: Perzentile erst ab 20 echten Messwerten (vorher 3), Kategorie-Logik nur
  noch in Python (DB-Trigger per Migration 002 entfernt)
- Firmware: ~51 KB Flash durch Totcode-Entfernung gespart (CSVBuffer, TaskManager,
  verwaiste Endpoints), ~24 KB RAM durch JSON-Pool-Verkleinerung (16 KB → 4 KB)
- Firmware: Blockaden im `loop()` reduziert (MQTT-Busy-Wait, stündlicher WiFi-Scan,
  `initTime()`-Blocking entfernt), Cache-Header für CSS/JS/Favicon,
  `WiFi.persistent(false)` gegen Flash-Writes bei jedem Reconnect
- UI: ~350 KB CDN-Ballast entfernt (moment.js + Chart.js-Adapter durch native
  Date-Formatierung ersetzt), Status-Polling entschärft (In-Flight-Guard, 5 s statt 2 s)

### Behoben
- Firmware: NeoPixel-Use-after-free — `pixels = Adafruit_NeoPixel(...)` in `setup()`
  erzeugte über den fehlenden Copy-Assignment-Operator einen Zugriff auf freigegebenen
  Speicher; Ursache des rhythmischen LED-Pulsierens und der Heap-Korruption mit Reboots.
  Initialisierung jetzt über `updateType()/updateLength()/setPin()/begin()`
- Firmware: nächtliche Reboot-Schleife — kumulierte NVS-Uptime wurde als aktuelle
  Laufzeit interpretiert, ab 30 Tagen Gesamtlaufzeit empfahl jeder Boot einen Neustart
- Firmware: MQTT sendete alle Initial-States bei jedem Boot doppelt (fehlendes
  `mqttWasConnected`-Flag, `setRetain(true)` auf Command-Entitäten)
- Firmware: MQTT-Passwort wurde beim Speichern durch die Maske `****` überschrieben
- Firmware: Neutral-Fallback greift wirklich (Erfolgs-Flags wurden in Fehlerpfaden
  zurückgesetzt), HA-Kategorie-Sensor aktualisiert wieder
- Firmware: OTA-Härtung — Magic-Byte-Prüfung (0xE9), HTTP 500 bei Fehlern,
  Abbruch-Handling, Platzprüfung vor Upload; Factory-Reset löscht `settings.json`
- Firmware: Eingabevalidierung für `numLeds` (1–64), GPIO-Pins (Flash-Pins gesperrt),
  SSID/Passwort-Längen, Farbwerte, Intervalle
- Backend: API-Ausfälle erzeugten falsche Daten — fehlgeschlagene Anthropic-Aufrufe
  wurden einen Monat lang als Sentiment 0.0 gespeichert (1911 unbrauchbare Messungen,
  verfälschte Perzentil-Schwellen). Fehler überspringen jetzt den Zyklus; Altdaten bereinigt
- Backend: Connection-Pool wird wirklich genutzt (vorher teilten sich alle Threads eine
  Connection mit gemeinsamer Transaktion), Rollback in allen Fehlerpfaden
- Backend: Worker-Robustheit — `reconfigure()` in den ersten 10 s tötete den Worker
  dauerhaft, Intervalländerungen lösten Sofort-Analysen aus; Lock gegen Parallelanalysen
- Backend: Cache-Invalidierung löschte den falschen Key, fehlendes `import requests`
  (`/api/news` war tot), `init.sql` läuft wieder auf frischer DB, timezone-aware Timestamps
- UI: XSS-Reste (WLAN-SSIDs, Settings, Log), Log-Anzeige wieder mehrzeilig,
  History kommt direkt vom Backend (vorher OOM-Risiko beim 720-h-Proxy über den ESP),
  „WLAN zurücksetzen"-Button funktioniert, kein weißer Flash im Dark Mode
- Build: fehlende diagnostics.html aus der tar-Liste entfernt, Geräte-IP korrigiert

### Sicherheit
- Backend: `SECRET_KEY` schlägt beim Start fehl, wenn nicht gesetzt (kein bekannter
  Fallback-Wert mehr), Login-Rate-Limit (5 Versuche / 60 s Sperre)
- Backend: `/api/news`-Kostenvektor gedeckelt (`headlines_per_source` max. 10),
  Open-Redirect im Login geschlossen, SSRF-Guard bei der Feed-Validierung, `SameSite=Lax`
- Backend: `requests` ≥ 2.32.4 (CVE-2024-47081)
- Firmware: `/logs` liefert `text/plain; charset=utf-8` — Stored-XSS-Weg geschlossen

## [9.11] – 2026-03-27

### Hinzugefügt
- Backend-Endpoint `GET /api/moodlight/feeds/trends` und `get_feed_trends()` in der
  Database-Klasse — Sentiment-Trend pro RSS-Feed

### Behoben
- DHT22 nutzt den Pin aus den Einstellungen statt des hardcodierten Standardwerts
  (Pin 17 → 18)

## [9.10] – 2026-03-27

### Geändert
- UI-Redesign aller Seiten: Inline-Styles durch CSS-Klassen ersetzt
  (index.html, setup.html, mood.html, diagnostics.html), Score-Farbklassen statt
  inline berechneter Farben, Dashboard-Karten-Layout

## [9.9] – 2026-03-27

### Hinzugefügt
- Schlagzeilen-Transparenz: Sektion „Analysierte Schlagzeilen" in mood.html mit
  fetch()-Integration
- Vollständiges SPA-Dashboard im Backend mit drei Tabs

## [9.8] – 2026-03-26

### Behoben
- Doppelte Watchdog-Initialisierung entfernt, TWDT nutzt `reconfigure`

## [9.7] – 2026-03-26

### Behoben
- Crash auf bestimmten ESP32-Board-Revisionen durch Konflikt zwischen WiFi- und
  NeoPixel-Event-Group

## [9.6] – 2026-03-26

### Geändert
- Firmware-Modularisierung abgeschlossen: `moodlight.cpp` auf 197 Zeilen reduziert
  (reine Orchestrierung). Logik liegt in `mqtt_handler`, `web_server`, `wifi_manager`,
  `sensor_manager`, `settings_manager`, `led_controller`, `debug`
- Magic Numbers nach `config.h` zentralisiert
- Dead Code und Kommentarreste entfernt

## [9.5] – 2026-03-26

### Hinzugefügt
- One-Click-Update-Workflow im Web-Interface
- `ChunkStream`-Klasse und VERSION.txt im kombinierten TGZ
- Eigene Partitionstabelle

### Geändert
- `build-release.sh` für den Zwei-Datei-Workflow (Firmware .bin + UI .tgz) neu geschrieben

### Behoben
- Versionsanzeige: `/api/firmware-version` liefert JSON
- BSD-tar `--exclude`-Reihenfolge für macOS-Kompatibilität

## [9.2] – 2026-03-26

### Geändert
- `build-release.sh` als Builder für ein kombiniertes TGZ

### Behoben
- `JsonBufferPool.release()`-Fix und RAII-Guard gegen Speicherlecks

## [9.0] – 2026-03-25

### Geändert
- Backend-Integration: lokale Datenverwaltung auf dem ESP32 entfernt, das Gerät ist
  ein dünner Client, der den vorberechneten Score von `analyse.godsapp.de` pollt
- `headlinesPerSource` in der Firmware optional
- Komplette UI-Bereinigung, Import/Export-Funktion entfernt

### Behoben
- MQTT-Callback-Fix, API-URL-Typ

## [1.0] – 2025-07-31

### Hinzugefügt
- Erste Version von AuraOS: ESP32-Moodlight mit Nachrichtenanalyse, NeoPixel-Steuerung,
  DHT-Sensor, Web-Interface, Home-Assistant-Integration via MQTT, Captive Portal
- Backend mit RSS-Feed-Analyse, Statistik-Dashboard und Trend-Ansicht
- Projekt-Homepage mit Live-Statistiken

[Unreleased]: https://github.com/Revisor01/auraos-moodlight/compare/v9.14...HEAD
[9.14]: https://github.com/Revisor01/auraos-moodlight/compare/v9.13...v9.14
[9.13]: https://github.com/Revisor01/auraos-moodlight/compare/v9.12...v9.13
[9.12]: https://github.com/Revisor01/auraos-moodlight/compare/v9.11...v9.12
[9.11]: https://github.com/Revisor01/auraos-moodlight/compare/v9.10...v9.11
[9.10]: https://github.com/Revisor01/auraos-moodlight/compare/v9.9...v9.10
[9.9]: https://github.com/Revisor01/auraos-moodlight/compare/v9.8...v9.9
[9.8]: https://github.com/Revisor01/auraos-moodlight/compare/v9.7...v9.8
[9.7]: https://github.com/Revisor01/auraos-moodlight/compare/v9.6...v9.7
[9.6]: https://github.com/Revisor01/auraos-moodlight/compare/v9.5...v9.6
[9.5]: https://github.com/Revisor01/auraos-moodlight/compare/v9.2...v9.5
[9.2]: https://github.com/Revisor01/auraos-moodlight/compare/v9.0...v9.2
[9.0]: https://github.com/Revisor01/auraos-moodlight/compare/v1.0...v9.0
[1.0]: https://github.com/Revisor01/auraos-moodlight/releases/tag/v1.0
