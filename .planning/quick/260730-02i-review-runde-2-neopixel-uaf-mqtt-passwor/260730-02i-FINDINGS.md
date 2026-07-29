# Review-Runde 2 — Fix-Grundlage (2026-07-30)

Konsolidierte Findings aus fünf Reviews des aktuellen Stands (nach den Runde-1-Commits 2674179/b8bfff4/25c9856). Alle Findings am Code belegt. Verbindliche Arbeitsgrundlage.

## Nutzer-Vorgaben (VERBINDLICH, wie Runde 1)

- KEINE Auth/CSRF/Zugriffsschutz-Fixes am GERÄT (Heimnetz). Das Backend ist aber ÖFFENTLICH im Internet — dort sind die gelisteten Härtungen ausdrücklich gewollt.
- Tests Pflicht: `cd firmware && pio run` | `cd sentiment-api && python3 -m py_compile *.py && python3 -m pytest tests/ -q` | `node --check` je geänderter JS-Datei.
- Kein Deployment, kein git push (macht der Orchestrator danach).
- Umlaute korrekt (öäüß) — auch in Commit-Messages!
- Ein atomarer Commit pro Task-Block. Nur selbst geänderte Dateien stagen (explizite Pfade).
- SECRET_KEY ist auf dem Produktionsserver gesetzt (verifiziert) — Fail-fast ist deploybar.

## BEWUSST AUSGESCHLOSSEN (nicht anfassen!)

- WiFi-Reconnect-Umbau (disconnect/busy-wait in wifi_manager.cpp:258-274): Historie mit Instabilität — NICHT umbauen. Nur `WiFi.persistent(true)` → `false` (Z. 152) ist erlaubt.
- gzip-Assets, CDN-Ersatz für Chart.js/Font Awesome: Follow-up.
- `/api/system/metrics|diagnose` entfernen: bleiben als Debug-APIs (nur cleanup-Iterator-Bug fixen).
- DB-Trigger `set_sentiment_category` droppen: NUR in init.sql entfernen + Migrationsdatei `sentiment-api/migrations/` anlegen (Anwendung auf Prod macht der Orchestrator per SSH). Keine Code-Änderung, die den Trigger voraussetzt.

---

## Task-Block A: Firmware (`firmware/src/`)

### A-KRITISCH-1 — NeoPixel Use-after-free (Root Cause für Pulsieren/Reboots!)
`moodlight.cpp:90`: `pixels = Adafruit_NeoPixel(appState.numLeds, appState.ledPin, NEO_GRB + NEO_KHZ800);` — Klasse hat KEINEN Copy-Assignment-Operator; das Temporary wird shallow kopiert und sein Destruktor gibt danach `pixels`-Puffer frei (`free(pixels)`), released den RMT-Kanal und setzt den Pin auf INPUT (verifiziert in Adafruit_NeoPixel.cpp:119-137). Das globale Objekt arbeitet seither auf freigegebenem Heap → Heap-Korruption, LED-Artefakte, Spontan-Reboots.
**Fix:**
```cpp
pixels.updateType(NEO_GRB + NEO_KHZ800);
pixels.updateLength(appState.numLeds);  // allokiert Puffer im globalen Objekt (free(NULL) ist safe)
pixels.setPin(appState.ledPin);
pixels.begin();
```

### A-HOCH-2 — MQTT-Passwort-Roundtrip zerstört Passwort
`web_server.cpp:1175` (`/savemqtt`): übernimmt `pass` blind; `/api/settings/mqtt` (Z. 1052) liefert `"****"`, `setup.js:651-653` füllt das Feld, Save schickt es zurück → echtes Passwort wird durch `****` ersetzt + Reboot. **Fix:** `if (pass != "****" && pass.length() > 0)` erst dann übernehmen; leer/Maske = unverändert. Zusätzlich `/savemqtt`: nur rebooten wenn sich wirklich etwas geändert hat (changed-Tracking wie `/saveapi`); fehlende Keys nicht als leer übernehmen (nur vorhandene Keys anwenden). UI (setup.js): Passwortfeld leer lassen mit `placeholder="(unverändert)"` statt `****` einzutragen.

### A-HOCH-3 — /api/stats: Clamp + UI-Direktzugriff
`web_server.cpp:712-729`: kein hours-Clamp, ESP puffert Backend-JSON doppelt (~150 KB bei 720h) → OOM-Risiko. **Fix Firmware:** hours `constrain(1, 720)`; `fetchBackendStatistics` (sensor_manager.cpp): Timeout 15s → 8s; Stats-URL aus `appState.apiUrl` ableiten (Suffix `/current` → `/history`), Fallback DEFAULT_STATS_API_URL. **Fix UI (Block C):** mood.js lädt die History DIREKT vom Backend (`https://analyse.godsapp.de/api/moodlight/history?hours=N` — CORS live verifiziert, mood.html macht das für current/headlines bereits). `/api/stats` bleibt als geclampter Fallback bestehen; verwaistes `/api/backend/stats` (Z. 930) entfernen.

### A-HOCH-4 — /ui-upload meldet immer Erfolg
`web_server.cpp:957-961`: Completion-Handler sendet bedingungslos 200. **Fix:** statisches Erfolgs-/Fehlerflag in `handleUiUpload` pflegen (Extraktion, Kopieren, Platz), Completion sendet bei Fehler 500 mit Klartext; vor Upload-Start Platzprüfung (freie Bytes < 3× Content-Length → ablehnen). setup.html `startFullUpdate()` wertet den Status aus (Block C).

### A-MITTEL — Upload-Robustheit
- Datei-Handle pro Chunk: `static File` bei UPLOAD_FILE_START öffnen, bei WRITE nur schreiben, bei END/ABORTED schließen (web_server.cpp:459-473).
- `UPLOAD_FILE_ABORTED`-Handling: Firmware-Update → `Update.abort()` + setStatusLED(0); UI-Upload → Teil-TGZ löschen, `isTgzFile=false`, `uploadPath=""`.
- `extractedVersion = ""` bei UPLOAD_FILE_START beider Handler (Z. 375, 1793); UI-Versions-Parse nur bei `startsWith("UI-")` (Z. 390-397).
- `/extract` nach erfolgreicher Installation rekursiv leeren (vorhandenes `deleteDir` nutzen — NICHT löschen, wird jetzt gebraucht).
- `uploadPath = ""` am Anfang von UPLOAD_FILE_START (Stale-Path).

### A-MITTEL — Blockaden in loop()
- MQTT-Reconnect-Busy-Wait entfernen (mqtt_handler.cpp:440-453): `mqtt.begin(...)` aufrufen, Erfolg beim nächsten Check auswerten (HAMqtt::loop() verbindet selbst nach, verifiziert). Startup-Wait in connectMQTTOnStartup() darf bleiben.
- Config-Modus (moodlight.cpp:106-123): `processLEDUpdates(); updateStatusLED();` und `delay(LOOP_DELAY_MS)` ergänzen (AP-Status-LED sichtbar machen, Busy-Loop beenden); `apModeStartTime` bei jedem behandelten Request im Config-Modus auffrischen (AP-Timeout nicht während aktiver Konfiguration).
- Stündlichen WiFi-Scan aus `NetworkDiagnostics::fullAnalysis()` entfernen (MoodlightUtils.cpp:577) — nur RSSI/Kanal loggen; Scan bleibt on-demand via `/wifiscan`. Im `/wifiscan`-Handler: `delay(10)` pro Netz entfernen, nach Serialisierung `WiFi.scanDelete()`.
- `initTime()` (wifi_manager.cpp:79): `delay(1000)`+Einmal-Check ersetzen durch nicht-blockierende Prüfung `time(&now) > 1600000000` beim nächsten Loop-Durchlauf (SNTP synct asynchron).
- `WiFi.persistent(true)` → `persistent(false)` (wifi_manager.cpp:152). SONST NICHTS am Reconnect ändern!

### A-MITTEL — Flash/RAM
- sysstat-Schreibblock in `runSystemHealthCheck()` entfernen (web_server.cpp:1896-1933) — niemand liest die Dateien.
- `JSON_BUFFER_SIZE` 16384 → 4096 (web_server.cpp:57-61); vorher prüfen, dass keine Pool-Antwort größer ist (Status ~2 KB).
- MemoryMonitor bleibt wie in Runde 1.
- LittleFS-Iterator-Bug in `/api/system/cleanup` (web_server.cpp:878-908): advance-then-delete-Muster wie Z. 429-434; `/data/settings.json.bak` NICHT löschen (Recovery-Backup).

### A-NIEDRIG/PERF
- `LOOP_SERVER_HANDLE_MS`-Gate in loop() streichen (delay drosselt bereits), `LOOP_DELAY_MS` 20 → 10 (config.h + moodlight.cpp:129-133).
- Cache-Header: für `/css/*`, `/js/*`, favicon-204 `Cache-Control: public, max-age=86400`; HTML `no-cache` (web_server.cpp handleStaticFile + Asset-Routen Z. 951-955 + favicon Z. 940-948).
- Redundanten `updateLEDs()`-Aufruf am Ende von `getSentiment()` entfernen (sensor_manager.cpp:381, falls noch vorhanden).
- 1h-Fallback: `currentLedIndex = 2; updateLEDs();` setzen, damit „Neutral-Modus" auch die LEDs neutral färbt (sensor_manager.cpp:241-248).
- Heartbeat: DHT-Status nur bei `appState.dhtEnabled` prüfen (mqtt_handler.cpp:219-222).
- `initDHT()`: bei `dhtEnabled=false` weder pinMode noch dht.begin (sensor_manager.cpp:25-34).
- Status-LED: MQTT-Erfolg setzt `statusLedMode = 0` nur wenn vorher 4 (mqtt_handler.cpp:466/487) — API-Fehler-Anzeige nicht löschen.
- `/savewifi`: SSID 1-32 Zeichen sonst 400, Passwort ≤ 63 (web_server.cpp:1125-1127). `/saveapi`: apiUrl nicht leer + startsWith("http") sonst 400. `/savecolors`: in temporäres Array parsen, nur komplett übernehmen (Z. 1292-1311). `/savehardware`: bei abgelehnten Pins Fehler in Response melden statt still ignorieren.
- Rest-`sprintf` → `snprintf` (web_server.cpp:603, 1097, 1533).
- `text/plain` → `text/plain; charset=utf-8` bei /logs und Fehlertexten mit Umlauten (Z. 1517, 1310, 1321).
- `/status`-Duplikat entfernen (Z. 1521-1583, kein Aufrufer), `/api/backend/stats` entfernen (Clamp wandert in /api/stats), `/api/export/settings` entfernen (~40 Z., verwaist), `/api/ui-version` entfernen (UI nutzt /api/system/info). `/api/restart-counter` + `/api/reset-restart-counter` BEHALTEN (Debug-APIs des Nutzers).
- `initWatchdog()` (Z. 1880) toter Code entfernen; `JsonBufferGuard` entfernen (ungenutzt); doppeltes lokales `copyFile` in web_server.cpp durch `SafeFileOps::copyFile` ersetzen ODER lokal behalten aber `char[64]`-Puffer entfernen (direkt `source.c_str()`).
- `logBuffer[20][...]` → `logBuffer[LOG_BUFFER_SIZE][...]` (app_state.h:128).
- `millis() > rebootTime` → `(long)(millis() - appState.rebootTime) >= 0` (moodlight.cpp:118/136).
- Unbenutzte NTP-Konstanten (config.h:72-74) entfernen ODER in initTime verwenden — eine Quelle.
- `getStorageInfo`: `LittleFS.begin()`-Aufrufe entfernen (Z. 332, 1755), FS ist gemountet. `handleStaticFile`: `exists()`+`open()` → direkt öffnen und Handle prüfen.
- Captive-Portal `canHandle`: `override`-Keyword ergänzen; stale Whitelist korrigieren oder auf Kommentar reduzieren (wifi_manager.cpp:39-51).
- try/catch um HTTP/DHT/MQTT (sensor_manager.cpp:132/187, mqtt_handler.cpp:516, web_server.cpp:497) entfernen — Scheinsicherheit, Arduino wirft dort keine Exceptions. Die MQTT-Startup-try/catch darf bleiben, wenn Entfernen riskant erscheint.

**Test A:** `cd firmware && pio run` fehlerfrei; Flash-Größe notieren.

---

## Task-Block B: Backend (`sentiment-api/`)

### B-KRITISCH-1 — SECRET_KEY fail-fast
`app.py:21`: Fallback `'dev-secret-schluessel-aendern'` entfernen. `SECRET_KEY` aus Env lesen; wenn leer/fehlend → `raise RuntimeError("SECRET_KEY muss gesetzt sein")`. (Prod hat den Key gesetzt, verifiziert.) Zusätzlich `SESSION_COOKIE_SAMESITE='Lax'` setzen. KEIN `SESSION_COOKIE_SECURE` (Seite läuft über http — würde Login brechen).

### B-KRITISCH-2 — init.sql bricht auf frischer DB
`init.sql:127-131` nutzt `update_updated_at_column()` bevor sie definiert ist (Z. 192-198). **Fix:** Helper-Funktionen-Sektion VOR die settings-Tabelle verschieben. Auf frischer DB testen ist nicht möglich — mindestens per Lese-Reihenfolge verifizieren.

### B-HOCH-3 — /api/news-Kostenvektor deckeln
`app.py:366-371`: `headlines_per_source` aus URL ohne Limit. **Fix:** `min(headlines_param, 10)`. (Kein Auth-Zwang, nur Cap.)

### B-HOCH-4 — Login-Rate-Limit
`app.py:581-597`: unbegrenzt brute-forcebar. **Fix ohne neue Dependency:** In-Memory-Dict `{ip: (fail_count, locked_until)}`; nach 5 Fehlversuchen 60 s Sperre (429), Reset bei Erfolg. Thread-safe (Lock), Speicher begrenzen (z. B. max 1000 IPs, älteste raus).

### B-MITTEL — Worker-Lebensdauer (3 bestätigte Bugs)
`background_worker.py:173-191`:
1. `if self._wake_event.wait(timeout=10): return` — `reconfigure()` in den ersten 10 s beendet den Thread dauerhaft. Fix: nach Wait `if not self.running: return`, sonst `clear()` und weiter.
2. Wake während `_perform_update()` wird vom folgenden `clear()` verschluckt → neues Intervall greift erst nach altem Ablauf. 
3. Wake durch `reconfigure()` startet sofort eine Analyse (ungewollter API-Call).
**Fix (deckt 1-3 ab):** Warte-Schleife mit Deadline: `deadline = now + interval`; innere Schleife `wait(min(rest, 5s))`; bei Wake: wenn `not running` → return; sonst `clear()` und Deadline gegen das (ggf. neue) `interval_seconds` NEU berechnen — ohne sofortige Analyse. Analyse nur bei Deadline-Ablauf. `trigger()` bleibt der Weg für Sofort-Analysen.

### B-MITTEL — Weitere
- Open Redirect: `next_url` nur akzeptieren wenn `startswith('/')` und nicht `startswith('//')` (app.py:590).
- SSRF-Guard Feed-Validierung (moodlight_extensions.py:461): nur http/https, Hostname auflösen und private/Loopback/Link-local-IPs ablehnen (ipaddress-Modul), `timeout=5` behalten.
- `INTERVAL '%s hours'` → `make_interval(hours => %s)` bzw. `%s * INTERVAL '1 day'` (database.py:385, 610, 662).
- feeds-Status schreiben: in `_fetch_headlines()` pro Feed bei Erfolg `UPDATE feeds SET last_fetched_at=NOW(), error_count=0`, bei Fehler `error_count=error_count+1` (neue DB-Methode, background_worker.py:310-354).
- `next_update_minutes` dynamisch aus `worker.interval_seconds // 60` (moodlight_extensions.py:142).
- `get_cursor()`: `conn.cursor(...)` INS try ziehen, damit putconn auch bei Cursor-Fehler läuft (database.py).
- requests auf `>=2.32.4` heben (requirements.txt) — CVE-2024-47081.
- Kategorie-Duplikat: Trigger `set_sentiment_category` + Funktion `get_sentiment_category` aus init.sql entfernen; Migrationsdatei `sentiment-api/migrations/002_drop_category_trigger.sql` (o. ä. fortlaufende Nummer) mit `DROP TRIGGER IF EXISTS ...; DROP FUNCTION IF EXISTS ...;` anlegen. Python (shared_config) ist die einzige Wahrheit. NICHTS deployen.
- Percentile-Mindestanzahl: `count < 3` → `count < 20` für echte Perzentile (database.py:582-637, get_score_percentiles); darunter Fallback-Schwellen (fallback-Flag transportiert das bereits). Tests anpassen falls betroffen.

### B-PERF
- Redis-Cache mit 120 s TTL für `/api/moodlight/history`, `/trend`, `/stats`, `/feeds/trends` (Key inkl. Parameter, z. B. `moodlight:history:{hours}:{limit}`); Invalidierung: bestehende Worker-Invalidierung um Pattern-Delete `moodlight:history:*` etc. erweitern (redis scan_iter + delete, kein KEYS).
- Feed-Fetching parallelisieren: `concurrent.futures.ThreadPoolExecutor(max_workers=6)` in `_fetch_headlines()` und dem app.py-Pendant; Timeout pro Feed 10 s; Reihenfolge der Feeds im Ergebnis stabil halten (Ergebnisse nach Feed-Reihenfolge zusammensetzen).

**Test B:** py_compile + pytest grün; neue/angepasste Tests für: Worker-Reconfigure-Verhalten (stirbt nicht, keine Sofort-Analyse), Login-Rate-Limit, headlines_per_source-Cap. init.sql-Reihenfolge per Skript prüfen (Funktion vor erster Verwendung definiert).

---

## Task-Block C: Web-UI (`firmware/data/`)

### C-HOCH-1 — XSS/innerHTML-Reste + Log-Anzeige-Regression
- `scanWifi()` (setup.js:592-598): SSIDs per DOM-API/`textContent` rendern.
- `showAllSettings()` (setup.js:201-209): key/value escapen bzw. textContent.
- `refreshLog()` (script.js:19-27): `log.textContent = data;` + in style.css `.logs { white-space: pre-wrap; }` — behebt zugleich die Regression, dass das Log seit dem text/plain-Umbau einzeilig erscheint.

### C-MITTEL
- `data-hours` auf ALLEN Zeitraum-Tabs (mood.html:66-68: 24/168/720) und `currentHours` bei jedem Tabwechsel setzen + bei Änderung neu laden (mood.js:45-55).
- `allDataLoaded = true` erst im Erfolgs-`.then()` (mood.js:50-54).
- `loadStorageInfo2()`-Aufruf vom Hardware- zum Info-Tab (`about`) verschieben (setup.js:33-35).
- Schalter (index.html:101/108): ohne `checked` starten + `disabled`, nach erstem `updateSwitches()` enablen.
- `#mood-class` aus der Backend-Kategorie (`data.category` von /api/status falls vorhanden, sonst aus ledIndex) ableiten statt eigener JS-Schwellen (script.js:205-217).
- UI-Upload: `startFullUpdate()`/`doUpload()` in setup.html werten HTTP-Status + Response-Text des /ui-upload-Abschlusses aus (mit Firmware-Fix A-HOCH-4); Fehler → Abbruch VOR dem Firmware-Flash.
- History direkt vom Backend: mood.js `loadData()` ruft `https://analyse.godsapp.de/api/moodlight/history?hours=${currentHours}` (Response-Format: `{data: [{timestamp, sentiment_score, category}], count}` — mit moodlight_extensions.py abgleichen!); Fallback auf `/api/stats` des Geräts bei Fetch-Fehler behalten.

### C-PERF
- `/logs`-Polling nur wenn `#logContent` existiert (script.js:376/391) — mood/setup pollen sonst ins Leere.
- moment.js + chartjs-adapter-moment aus mood.html entfernen (Z. 11-12); die vier moment-Nutzungen (mood.js:233, 498, 522, 547) durch native `Date`/`Intl.DateTimeFormat('de-DE', ...)`-Helfer ersetzen. Alle Charts nutzen category-Achsen — kein Adapter nötig.
- „Gesamter Zeitraum": statt `slice(-500)` Schrittfilter-Dezimierung wie im Day-Chart (mood.js:496 vs. 552-555).
- Dark-Mode-Flash: Mini-Inline-Script direkt nach `<body>`-Open in allen drei HTML-Seiten: `if(localStorage.getItem('darkMode')==='true')document.body.classList.add('dark');`.
- Tote `updateAllCharts`/`updateChart`/`update*Chart`-Funktionen entfernen (mood.js:398-484, nur mit `reprocess=false` erreichbar — per grep verifizieren).

### C-NIEDRIG
- Refresh-Button: Icon nicht per textContent zerstören (innerHTML mit Icon wiederherstellen oder span fürs Label, script.js:61-77); nach `/refresh` 3× im 3-s-Abstand nachpollen statt einmal 2 s.
- `getMoodColor`: „Sehr positiv" eigene Farbe (z. B. #12b886 vs. #20c997) (script.js:198-203).
- Slider: `oninput` fürs Label; während des Ziehens (`pointerdown`/`pointerup`-Flag) den Wert nicht vom Poll überschreiben lassen (index.html:114 + script.js).
- `xhr.timeout` 120000 → 300000 (setup.html:383).
- Anführungszeichen vereinheitlichen: `mood.html:99` und `:370` auf „…" (deutsche Typografie).
- `#wifi-status-info` (setup.html:63): mit aktueller SSID aus /api/settings/all befüllen (kleines Feature) ODER Element entfernen.
- aria-labelledby der Tabpanels korrigieren (mood.html:74-86): Tabs bekommen IDs, Panels referenzieren sie.

**Test C:** `node --check` je JS-Datei; grep: keine Referenz auf entfernte Funktionen; keine moment-Referenz mehr in mood.js/mood.html.

---

## Task-Block D: Build-Script (`build-release.sh`)

- Z. 91: `diagnostics.html` aus der tar-Dateiliste entfernen (Datei existiert nicht mehr — tar bricht sonst ab, set -e!).
- Z. 126: IP `192.168.0.140` → `192.168.0.37`.
- KEINEN Release bauen — das macht der Orchestrator danach.

**Test D:** `bash -n build-release.sh`.
