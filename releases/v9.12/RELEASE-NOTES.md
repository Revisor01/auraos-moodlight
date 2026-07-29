# AuraOS v9.12 — Release Notes (2026-07-30)

Großes Stabilitäts- und Wartungsrelease. Zwei vollständige Code-Review-Runden über Firmware, Backend und Web-UI mit insgesamt über 60 umgesetzten Findings. Kern des Releases: die Ursache des seit Monaten beobachteten LED-Pulsierens und der Spontan-Reboots ist gefunden und behoben.

## Highlights

### 🔴 Root-Cause-Fix: LED-Pulsieren & Spontan-Reboots
`pixels = Adafruit_NeoPixel(...)` in `setup()` erzeugte einen Use-after-free: Die NeoPixel-Klasse hat keinen Copy-Assignment-Operator, der Destruktor des Temporary-Objekts gab den Pixel-Puffer frei und den RMT-Kanal zurück — das globale LED-Objekt arbeitete seither auf freigegebenem Speicher. Jede spätere Heap-Wiederverwendung konnte LED-Artefakte („rhythmisches Pulsieren") und Heap-Korruption mit Reboots auslösen. Jetzt wird das globale Objekt korrekt über `updateType()/updateLength()/setPin()/begin()` initialisiert.

### 🔴 MQTT sendete die Initial-States doppelt
Vom Nutzer gemeldet, im Serial-Log belegt: Bei jedem Boot wurden alle Zustände (Farbe, Sensoren, Heartbeat) zweimal an Home Assistant gesendet. Zwei Ursachen behoben: fehlendes `mqttWasConnected`-Flag nach dem Startup-Connect und `setRetain(true)` auf den Command-Entitäten (Broker spielte alte Befehle bei jedem Reconnect wieder ein).

### 🔴 MQTT-Passwort wurde beim Speichern zerstört
Wer MQTT-Einstellungen speicherte, ohne das Passwort neu einzutippen, überschrieb das echte Passwort mit der Maske `****`. `/savemqtt` behandelt die Maske jetzt als „unverändert" und rebootet nur noch bei echten Änderungen.

### 🔴 Backend: API-Ausfälle erzeugten falsche Daten
Ein Monat lang wurden fehlgeschlagene Anthropic-Aufrufe (Guthaben leer) als Sentiment 0.0 gespeichert — 1911 unbrauchbare Messungen verfälschten die Perzentil-Schwellen und damit die LED-Farbe. Fehler führen jetzt zum Überspringen des Zyklus statt zu Fake-Daten; der echte Fehlertext wird geloggt. Die Altdaten wurden bereinigt (Backup auf dem Server).

## Firmware (v9.11 → v9.12)

**Stabilität**
- NeoPixel-Use-after-free behoben (siehe oben)
- Nächtliche Reboot-Schleife behoben: kumulierte NVS-Uptime wurde als aktuelle Laufzeit interpretiert — ab 30 Tagen Gesamtlaufzeit empfahl jeder Boot einen Neustart
- Neutral-Fallback funktioniert jetzt wirklich (Erfolgs-Flags wurden in Fehlerpfaden fälschlich zurückgesetzt); nach 1 h ohne API-Update färbt die Lampe neutral
- HA-Kategorie-Sensor aktualisiert wieder (Vergleich lief gegen bereits überschriebenen Wert)
- OTA: Magic-Byte-Prüfung (0xE9) gegen versehentliches Flashen falscher Dateien, HTTP 500 bei Update-Fehlern, Abbruch-Handling (`UPLOAD_FILE_ABORTED`), UI-Upload meldet Fehler statt pauschal „Complete", Platzprüfung vor Upload, `/extract` wird nach Installation aufgeräumt
- Factory-Reset löscht jetzt auch `settings.json` (war vorher wirkungslos)
- Eingabevalidierung: `numLeds` (1–64), GPIO-Pins (Flash-Pins gesperrt), SSID/Passwort-Längen, Farbwerte (inkl. `#`-Präfix-Parsing), Intervalle vor Multiplikation geclampt

**Performance & Speicher**
- ~51 KB Flash durch Totcode-Entfernung gespart (CSVBuffer, TaskManager, verwaiste Endpoints u. v. m.)
- ~24 KB RAM frei durch JSON-Pool-Verkleinerung (16 KB → 4 KB pro Puffer)
- Blockaden in `loop()` reduziert: MQTT-Reconnect-Busy-Wait entfernt, stündlicher 4-s-WiFi-Scan entfernt, `initTime()`-Blocking entfernt, Webserver-Doppeldrosselung aufgehoben (Loop-Delay 20 → 10 ms)
- Cache-Header für CSS/JS/Favicon — Assets werden nicht mehr bei jedem Seitenwechsel neu vom ESP geladen
- Stündliche sysstat-Flash-Writes entfernt (wurden von nichts gelesen), MemoryMonitor-NVS-Writes gedrosselt
- Upload-Durchsatz: Datei-Handle bleibt über Chunks offen statt pro Chunk öffnen/schließen
- `WiFi.persistent(false)`: keine Flash-Writes der Credentials bei jedem Reconnect mehr

**Sonstiges**
- Config-Modus: AP-Status-LED (gelb blinkend) funktioniert jetzt sichtbar; AP-Timeout verlängert sich bei aktiver Nutzung des Setup-Portals
- Heartbeat meldet „DHT Sensor Probleme" nicht mehr bei bewusst deaktiviertem Sensor
- `/logs` liefert `text/plain; charset=utf-8`; Stored-XSS-Weg geschlossen

## Backend (sentiment-api)

- **DB-Zugriff grundsaniert:** Der Connection-Pool wird jetzt wirklich genutzt (vorher teilten sich alle Threads eine Connection mit gemeinsamer Transaktion); Rollback in allen Fehlerpfaden
- **Worker-Robustheit:** `reconfigure()` in den ersten 10 s tötete den Worker dauerhaft; Intervalländerungen lösten ungewollte Sofort-Analysen aus — beides behoben (Deadline-basierte Warte-Schleife). Lock verhindert parallele Analysen (doppelte API-Kosten)
- **Härtung (öffentliche API):** SECRET_KEY-Fail-fast statt bekanntem Fallback-Wert, Login-Rate-Limit (5 Versuche/60 s Sperre), `/api/news`-Kostenvektor gedeckelt (`headlines_per_source` max. 10), Open-Redirect im Login geschlossen, SSRF-Guard bei Feed-Validierung, `SameSite=Lax`
- **Performance:** Feeds werden parallel geholt (vorher sequenziell mit bis zu 15 s pro Feed), Redis-Cache (120 s) für History/Trend/Stats/Feed-Trends
- **Korrektheit:** Cache-Invalidierung löschte den falschen Key (Geräte sahen neue Werte bis zu 5 min verspätet); `import requests` fehlte (`/api/news` war komplett tot); Kategorie-Logik nur noch in Python (DB-Trigger per Migration 002 entfernt); `init.sql` läuft wieder auf frischer Datenbank; timezone-aware Timestamps; Perzentile erst ab 20 echten Messwerten (vorher 3); Feed-Status (`last_fetched_at`/`error_count`) wird jetzt gepflegt
- gunicorn-Healthcheck im Compose, `.dockerignore`, `requests` ≥ 2.32.4 (CVE-2024-47081)

## Web-UI

- „WLAN zurücksetzen"-Button funktioniert (Funktion existierte nicht)
- Statistik-Seite: „Gesamter Zeitraum" wird nicht mehr vom Auto-Refresh zerstört; Zeitraum bleibt beim Tab-Wechsel korrekt; History kommt jetzt direkt vom Backend (entlastet den ESP massiv, vorher OOM-Risiko beim 720-h-Proxy); volle Zeitraum-Darstellung per Dezimierung statt Abschneiden
- ~350 KB CDN-Ballast entfernt (moment.js + Chart.js-Adapter — durch native Date-Formatierung ersetzt); insgesamt ~28 KB toter JS-Code aus Runde 1 + weitere Reste aus Runde 2 entfernt
- Firmware-Flash-Abbruch wird nicht mehr als „Erfolg" gemeldet; UI-Update-Fehler stoppen den Firmware-Schritt
- XSS-Reste geschlossen (WLAN-SSIDs, Settings-Anzeige, Log); Log-Anzeige wieder mehrzeilig
- Status-Polling entschärft (In-Flight-Guard, 5 s statt 2 s, nur auf dem Dashboard; `/logs`-Polling nur wo ein Log existiert)
- Kein weißer Flash mehr beim Laden im Dark Mode; Schalter zeigen keinen falschen Zustand mehr vor dem ersten Status; diverse Detail-Fixes (Slider, Icons, Farben, Typografie, ARIA)

## Installation

**Backend:** deployt automatisch nach dem Push (GitHub Actions → GHCR → Portainer). Migration 002 ist bereits auf der Produktions-DB angewendet.

**Gerät** (`http://192.168.0.37/setup` → Tab „Update"):
1. UI-Datei hochladen: `releases/v9.12/UI-9.12-AuraOS.tgz`
2. Firmware hochladen: `releases/v9.12/Firmware-9.12-AuraOS.bin`
3. „Update starten"

## Bekannte offene Punkte (Follow-ups)

- WiFi-Reconnect-Logik bewusst unangetastet (Stabilitäts-Historie) — nicht-blockierender Umbau als eigenes, isoliert testbares Release
- CDN-Abhängigkeiten (Chart.js, Font Awesome) bestehen weiter — reines LAN ohne Internet zeigt keine Icons/Charts
- `/wifiscan` und `/testapi` blockieren bei bewusster Nutzung weiterhin kurz
