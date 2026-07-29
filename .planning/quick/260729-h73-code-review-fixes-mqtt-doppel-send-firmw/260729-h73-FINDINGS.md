# Code-Review-Findings — Fix-Grundlage (2026-07-29)

Konsolidierte, verifizierte Findings aus einem Vier-Bereiche-Review (Firmware-Module, web_server.cpp, Python-Backend, Web-UI). Jedes Finding wurde am Code belegt. Diese Datei ist die verbindliche Arbeitsgrundlage für Plan und Ausführung.

## Explizite Nutzer-Vorgaben (VERBINDLICH)

- **NICHT fixen:** Klartext-Passwörter in `/data/settings.json` / Zugriffsschutz auf LittleFS-Pfade, Auth/CSRF-Härtung der Geräte-API, OTA-Auth-Token. Begründung Nutzer: Gerät nur im Heimnetz. (Ausnahme: der billige Magic-Byte-Check 0xE9 beim OTA gegen versehentliches Bricking ist erwünscht, kein Auth.)
- **Testen ist Pflicht:** Firmware muss bauen (`cd firmware && pio run`), Backend-Tests müssen laufen (`cd sentiment-api && python3 -m pytest tests/ -q`), JS-Dateien Syntax-Check (`node --check <datei>`).
- **Kein Deployment durch Claude** — der Nutzer deployt selbst (OTA + Portainer). Nur Code + Commits.
- **Umlaute** in allen Texten korrekt (öäüß, nie oe/ae/ue/ss).
- Antworten/Commits-Beschreibungen: Konventionen des Repos beachten (Commit-Stil siehe `git log`).

## Kontext: Live-Diagnose Backend (bereits erledigt, KEIN Fix nötig am Server)

Die API `analyse.godsapp.de` läuft. Der Anthropic-Call schlägt aber seit ≥30 Tagen mit HTTP 400 fehl: `"Your credit balance is too low to access the Anthropic API"`. Deshalb sind 1438 History-Einträge alle 0.0. Nutzer kauft Guthaben; der Code-Fix unten (B2) verhindert künftig, dass Fehler als 0.0 gespeichert werden.

---

## Task-Block A: Firmware (C++, `firmware/src/`)

### A1 — MQTT-Doppel-Send beim Start (Hauptbefund, vom Nutzer gemeldet)
- `mqtt_handler.cpp:503` `connectMQTTOnStartup()`: sendet nach erfolgreichem Connect `sendInitialStates()`, setzt aber nie `appState.mqttWasConnected = true`. Dadurch triggert `checkAndReconnectMQTT()` (Z. 477, `else if (!appState.mqttWasConnected)`) ~2 s später ein zweites `sendInitialStates()`. Beleg: serial-log.txt zeigt bei jedem Boot zweimal „Sende initiale Zustände" im Abstand ~1 s.
- **Fix:** In `connectMQTTOnStartup()` im Erfolgsfall `appState.mqttWasConnected = true;` setzen (direkt bei `sendInitialStates()`).
- Zusätzlich `setRetain(true)` entfernen bei `haLight` (Z. 291), `haMode` (Z. 298), `haUpdateInterval` (Z. 308), `haDhtInterval` (Z. 318). Begründung: `retain` im HA-Discovery-Schema lässt Home Assistant seine *Befehle* retained publizieren; bei jedem Reconnect spielt der Broker alte Befehle wieder ein → Callbacks feuern mit veralteten Werten, State wird erneut publiziert (zweite Quelle des Doppel-Sends), frische Einstellungen können überschrieben werden. `sendInitialStates()` versorgt HA nach jedem Reconnect korrekt.
- Kommentar „für nächsten Loop-Zyklus einplanen" (Z. 461, 480) anpassen — Z. 495 führt es in derselben Invocation aus.

### A2 — numLeds/Pins unvalidiert → Out-of-Bounds-Write
- `web_server.cpp:1504-1507`: `appState.numLeds = doc["numLeds"].as<int>()` ohne Prüfung. `ledColors` ist `uint32_t[MAX_LEDS]` (64), `updateLEDs()` schreibt bis `numLeds`, `statusLedIndex = numLeds - 1` (settings_manager.cpp:119/213). **Fix:** `constrain(val, 1, MAX_LEDS)`.
- `web_server.cpp:1495-1501`: `ledPin`/`dhtPin` unvalidiert. **Fix:** Bereich 0–39, GPIO 6–11 (Flash) ablehnen; bei ungültigem Wert Wert nicht übernehmen.

### A3 — Factory-Reset wirkungslos
- `web_server.cpp:1529-1559`: löscht nur Preferences. `loadSettings()` (settings_manager.cpp:179-186) lädt aber bevorzugt `/data/settings.json`. **Fix:** vor dem Reboot auch `LittleFS.remove("/data/settings.json")` und `.bak`-Datei entfernen.

### A4 — HA-Kategorie-Sensor friert nach erstem Update ein
- `sensor_manager.cpp`: `getSentiment()` setzt `appState.sentimentCategory` (Z. 321-324) VOR dem Aufruf von `handleSentiment()`; dort vergleicht Z. 89 `categoryText != appState.sentimentCategory` — immer false. **Fix:** alte Kategorie vor Überschreiben sichern und den Vergleich gegen den alten Wert führen (oder Zuweisung erst in `handleSentiment()`).

### A5 — Neutral-Fallback hebt sich selbst auf
- `sensor_manager.cpp:100-103`: `handleSentiment()` setzt `sentimentAPIAvailable = true`, `consecutiveSentimentFailures = 0`, `lastSuccessfulSentimentUpdate = millis()` — wird aber auch aus beiden FEHLER-Pfaden mit `handleSentiment(0.0)` aufgerufen (1h-Timeout Z. 260-266; 5-Fehler-Pfad Z. 378/384). **Fix:** dieses Erfolgs-Bookkeeping aus `handleSentiment()` entfernen; es steht bereits redundant im Success-Zweig von `getSentiment()` (Z. 355-360).

### A6 — Nächtliche Reboot-Schleife durch persistierte Uptime
- `MoodlightUtils.cpp:1035-1039`: `SystemHealthCheck::begin()` lädt `_uptimeHours` aus NVS (kumuliertes Maximum, nie zurückgesetzt). `isRestartRecommended()` (Z. 1179-1216) prüft `_uptimeHours > 720/48/24` → nach 30 Tagen kumulierter Laufzeit empfiehlt JEDER Boot einen Neustart; web_server.cpp:1975 plant dann jede Nacht einen Reboot. **Fix:** Laufzeit pro Boot aus `millis()/3600000UL` ableiten (Boot-Zeitpunkt merken); NVS-Wert nur als Statistik. Passt zur Debugging-Historie des Nutzers (Restart-Counter!).

### A7 — Buffer-/Parse-Fehler bei Farb-Handling
- `web_server.cpp:652, 1167`: `char hex[8]; sprintf(hex, "#%06X", ...)` — Werte > 0xFFFFFF überlaufen den Puffer. **Fix:** `snprintf(hex, sizeof(hex), "#%06X", wert & 0xFFFFFF)`. Auch Z. 1776/1799 auf snprintf umstellen.
- `web_server.cpp:1391, 1699`: `sscanf(hexColor.c_str(), "%x", &rgb)` — Rückgabewert ungeprüft, führendes `#` lässt Parse still scheitern (Farbe wird schwarz). **Fix:** führendes `#` überspringen, `sscanf`-Return == 1 prüfen (sonst 400), Ergebnis `& 0xFFFFFF` maskieren.

### A8 — dhtEnabled-Toggle ignoriert echte Booleans
- `web_server.cpp:1320`: `doc["dhtEnabled"].is<float>()` — JSON-`true` ist in ArduinoJson 7 kein float. **Fix:** `is<bool>() || is<int>() || is<float>()` behandeln (bool → direkt, Zahl → `!= 0`).

### A9 — OTA-Robustheit (kein Auth!)
- `web_server.cpp:1863-1869`: `/update` flasht jeden Body. **Fix (nur Robustheit):** ersten Chunk auf Magic Byte `0xE9` prüfen, sonst abbrechen mit Fehlermeldung; bei `Update.hasError()` HTTP 500 statt 200 (Z. 1830-1838).

### A10 — Kleinere Korrektheit
- `web_server.cpp:1312-1313, 1332`: Sekunden VOR der `*1000`-Multiplikation clampen (`constrain(sec, 10, 7200)`) gegen 32-Bit-Overflow.
- `web_server.cpp:903, 1003, 2034`: Division durch 0 bei `total == 0` absichern (wie Z. 719 es korrekt macht).
- `web_server.cpp:1562-1573`: `/logs` als `text/plain` statt `text/html` senden (Stored-XSS-Weg, Ein-Zeilen-Fix).
- `web_server.cpp:798-861 + 1905-1908`: `onNotFound` doppelt registriert — erster 64-Zeilen-Handler ist toter Code. Zu EINEM Handler zusammenführen (Verhalten des zweiten beibehalten; KEINE Pfad-Sperrliste einbauen, siehe Nutzer-Vorgabe).
- `sensor_manager.cpp:174-179`: unerreichbaren Code nach `return` entfernen.
- `sensor_manager.cpp:107-113`: tote Funktion `formatString` entfernen (+ Deklaration sensor_manager.h:36).
- `sensor_manager.cpp:401`: redundanten unbedingten `updateLEDs()`-Aufruf entfernen (Success-Pfad Z. 350-353 ruft bereits korrekt geguarded auf).
- `settings_manager.cpp:191-197`: Preferences-Fallback soll die allgemeinen Settings (`moodInterval`, `dhtInterval`, `autoMode`, `lightOn`, `manBright`, `manColor`) per `preferences.get*()` laden statt Defaults zu setzen (symmetrisch zu `saveSettings()` Z. 143-148).
- `settings_manager.cpp:137-138`: Rückgabewert von `saveSettingsToFile()` prüfen; bei Fehlschlag stale JSON löschen oder Fehler loggen (JSON hat Lade-Vorrang!).
- `MoodlightUtils.cpp:151-158`: `MemoryMonitor`-NVS-Write throttlen (max. alle 10 min ODER Delta > 1 KB).

### A11 — Toter Code (nur wenn grep Unbenutztheit bestätigt)
- `CSVBuffer`- und `TaskManager`-Klassen komplett aus MoodlightUtils.h/.cpp entfernen; ebenso `MoodlightUtils::getTimestamp()` (zieht `<sstream>`/`<iomanip>`), `safeDelay()`, `randomString()`, `SafeFileOps::moveFile()`/`removeDir()`. Vor JEDER Entfernung mit grep über `firmware/src/` bestätigen, dass nichts referenziert. Ziel: Flash-Ersparnis. Instanzen/Includes mit entfernen. Build muss danach sauber laufen.
- `wifi_manager.cpp:141-175`: `startAPMode()` entfernen falls unbenutzt (nur `startAPModeWithServer()` wird gerufen); Header anpassen.
- Tote v9.0-Endpoints `web_server.cpp:697-708, 1034-1044, 1052-1053` (`/api/stats/delete`, `/api/stats/reset`, `/api/repair/stats`) entfernen. VORSICHT: vorher grep in `firmware/data/` dass kein UI-Code sie ruft.

**Test A:** `cd firmware && pio run` — muss fehlerfrei bauen. Vorher/nachher Flash-Größe notieren.

---

## Task-Block B: Backend (Python, `sentiment-api/`)

### B1 — Fehlender Import
- `app.py:412/415`: `requests` wird benutzt, aber nie importiert → `/api/news` wirft immer NameError→500. **Fix:** `import requests` ergänzen.

### B2 — API-Fehler werden als Sentiment 0.0 gespeichert (Ursache der 30-Tage-Nullserie)
- `app.py:202-213`: bei `APIConnectionError`/`RateLimitError`/`APIStatusError` wird `[0.0] * n` zurückgegeben; Worker speichert das als echte Messung. **Fix:** Fehlerfall unterscheidbar machen (z. B. `None` zurückgeben oder Exception propagieren); `background_worker.py::_perform_update` überspringt dann den Zyklus OHNE DB-Write und loggt WARN. Beim `APIStatusError` auch den Response-BODY loggen (aktuell nur `<Response [400]>` — der eigentliche Fehlertext „credit balance too low" war unsichtbar).
- Teilweise ungeparste Scores (`app.py:197-198`): wenn `parsed_count != len(batch)` → Zyklus verwerfen/loggen statt fehlende still auf 0.0 zu lassen.

### B3 — Cache-Invalidierung löscht falschen Key
- `background_worker.py:223`: löscht `moodlight:current`, aktiver Key ist `moodlight:current:v2` (moodlight_extensions.py:39). **Fix:** Key-Konstante(n) in `shared_config.py` zentralisieren; `_perform_update` invalidiert beide bzw. den richtigen Key.

### B4 — DB-Connection-Handling (WICHTIGSTER Backend-Fix)
- `database.py:51/125`: EINE geteilte Connection für alle gunicorn-Threads + Worker; `get_cursor` nutzt immer `self.conn` → geteilte Transaktionen, Commits/Rollbacks treffen fremde Writes.
- Read-Methoden (z. B. Z. 256-263) haben KEIN `rollback()` im except → Connection hängt nach erstem Fehler dauerhaft in „current transaction is aborted"; außerdem bleiben SELECT-Transaktionen offen („idle in transaction").
- **Fix:** `get_cursor` als Context-Manager umbauen: pro Aufruf `conn = pool.getconn()`, `try: yield cursor; conn.commit()` / `except: conn.rollback(); raise` / `finally: pool.putconn(conn)`. Alle Methoden auf diesen Pfad bringen; `self.conn`-Nutzung entfernen. `_ensure_connection`-Logik entsprechend vereinfachen. Sorgfältig: alle Aufrufstellen in app.py/moodlight_extensions.py/background_worker.py prüfen.

### B5 — Worker-Robustheit
- `background_worker.py:155`: `time.sleep(interval)` → `threading.Event.wait(interval)`; `stop()` setzt das Event (Stop/Reconfigure greifen sofort).
- Lock zwischen `trigger()` (moodlight_extensions.py:383) und `_perform_update()`: `threading.Lock`, `trigger()` mit `acquire(blocking=False)` → bei laufender Analyse 409/Meldung statt Parallel-Lauf (doppelte API-Kosten).

### B6 — LLM-Call-Qualität
- `app.py:168-169`: `max_tokens = len(batch) * 15` zu knapp → Truncation → stiller Neutral-Bias. **Fix:** `max(1024, len(batch) * 25)`. `temperature=1.0` → `0` (deterministisches Scoring).

### B7 — Deployment-Hygiene (nur Repo-Dateien, kein Server-Eingriff)
- `docker-compose.yaml`: Healthcheck für `news-analyzer` (`curl -f http://localhost:6237/api/health`), `depends_on` mit `condition: service_healthy` für postgres/redis; Bind-Mount `./:/app` entfernen (überschattet das deployte GHCR-Image).
- `.dockerignore` anlegen: `.env`, `__pycache__/`, `tests/`, `*.sh`.
- `Dockerfile:3`: `build-essential` entfernen (alle Deps sind Binary-Wheels).

### B8 — Kleinere Fixes
- `moodlight_extensions.py:192`: `limit` deckeln (`min(limit, 50000)`).
- Naive Timestamps: `datetime.utcnow()`/`datetime.now()` in moodlight_extensions.py:200-221 und database.py:283 → `datetime.now(timezone.utc)`.
- `get_database()` (database.py:889-892): `_db` erst NACH erfolgreichem `connect()` zuweisen.

**Test B:** `cd sentiment-api && python3 -m pytest tests/ -q` — alle Tests grün. Zusätzlich sinnvoll: kleiner Test für B2 (Fehler → kein Save). Syntax-Check: `python3 -m py_compile app.py database.py background_worker.py moodlight_extensions.py shared_config.py`.

---

## Task-Block C: Web-UI (`firmware/data/`) + Doku

### C1 — Funktionsloser Button
- `setup.html:89`: `onclick="resetWiFi()"` — Funktion existiert nirgends. **Fix:** in setup.js implementieren: `confirm()` + POST auf `/resetwifi`, Erfolg/Fehler anzeigen.

### C2 — Auto-Refresh zerstört „Gesamter Zeitraum"
- `mood.js:23/63`: `setInterval(loadData, 300000)` ruft ohne Argument (168 h) und überschreibt die 720-h-Ansicht. **Fix:** `currentHours`-Variable pflegen, Intervall nutzt sie.

### C3 — Status-Polling
- `script.js:354`: `setInterval(refreshStatus, 2000)` ohne In-Flight-Guard; mood.html lädt script.js mit und pollt mit. **Fix:** In-Flight-Flag, Intervall auf 5000 ms, Polling nur starten wenn Dashboard-Elemente existieren (z. B. `document.getElementById('leds')`-Guard o. ä. — an tatsächlicher DOM-Struktur orientieren).

### C4 — Fehlerbehandlung Steuerung
- `script.js:316-336`: `toggleLight()`, `toggleMode()`, `setColor()`, `setBrightness()` ohne `.catch()`/Statusprüfung. **Fix:** `.catch()` + danach `refreshStatus()` zum Zustand-Rollback.

### C5 — Upload-Fehler als Erfolg gemeldet
- `setup.html:368-376` (`doUpload`): error/timeout beim `/update` wird pauschal als Erfolg gewertet. **Fix:** Abbruch nur als Erfolg werten, wenn `xhr.upload` 100 % erreicht hatte; sonst Fehlermeldung.

### C6 — HTML-Injection
- `mood.html:373-374` (`renderHeadlines`): `h.headline`/`feed` unescaped via innerHTML. **Fix:** `textContent` oder Escape-Helfer.

### C7 — Dark-Mode-Selektor
- `mood.html:38`: `[data-theme="dark"] .fallback-hint` wirkungslos — Theme wird als Klasse `.dark` auf body gesetzt. **Fix:** Selektor `.dark .fallback-hint`.

### C8 — Toter Code (~28 KB)
- `setup.js:940-1286` auskommentierte Blöcke (12,7 KB) löschen; tote Upload-Handler `setup.js:46-223` + Duplikat im DOMContentLoaded-Block (237-320) löschen (Formulare existieren nicht mehr; Update-Tab nutzt `startFullUpdate()`).
- `mood.js`: doppelt definierte `closeDataModal`/`toggleModalFilter`/`renderDataTable` (828-875 vs. 936-988) — je eine Definition behalten bzw. ganz löschen wenn von keiner Seite gerufen; tote Stubs (`showDataTable`, `deleteDataPoint`, `resetAllData`, Archiv-Stubs 1019-1042, `formatFileSize`) und toten Timer `setInterval(loadStorageInfo, 300000)` (Z. 20) entfernen.
- `setup.js:443-454, 532-546`: `loadUiVersion()`/`loadFirmwareVersion()` schreiben in nicht existierende Elemente → entfernen (inkl. Aufrufstellen).
- Vor JEDER Löschung grep über alle HTML/JS-Dateien, dass die Funktion nirgends referenziert wird.

### C9 — Kleinigkeiten
- `index.html:10`, `setup.html:11`, `mood.html:14`: `/favicon.png` → `/favicon.ico` (404 pro Seitenaufruf).
- `mood.html:255-265`: hartkodierte `LED_COLORS` → Farben von `/api/settings/colors` laden (Fallback auf bisherige Werte behalten), damit Custom-Farben konsistent zum Dashboard angezeigt werden. Die hartkodierte Backend-URL nur dann anfassen, wenn ein Geräte-Proxy-Endpoint bereits existiert — sonst belassen.
- `web_server.cpp`: Route `/diagnostics` (Z. 846) auf nicht mehr existierende Datei + Backup/Restore-Referenzen auf diagnostics.html (Z. 524/539) entfernen (gehört technisch zu Task A, beim UI-Kontext erwähnt).

### C10 — CLAUDE.md aktualisieren (nicht löschen — enthält aktive GSD-Konfiguration)
Veraltete Aussagen korrigieren:
- Firmware ist modular (`moodlight.cpp` nur Orchestrierung; Module: `mqtt_handler`, `web_server`, `wifi_manager`, `sensor_manager`, `settings_manager`, `led_controller`, `app_state.h`, `debug`, `MoodlightUtils`), Quellpfad `firmware/src/`, Version 9.11.
- Backend nutzt **Anthropic Claude Haiku** (`claude-haiku-4-5-20251001`), NICHT OpenAI; läuft unter **gunicorn** (Dockerfile), nicht `python app.py`.
- Deployment: GitHub Actions → GHCR-Image → Portainer (Container `moodlight-analyzer`); das Verzeichnis `/opt/auraos-moodlight/sentiment-api/` existiert auf dem Server NICHT mehr — SSH-Pull-Workflow-Beschreibung ersetzen.
- Env-Var heißt `ANTHROPIC_API_KEY` (nicht `OPENAI_API_KEY`).
- `diagnostics.html` existiert nicht mehr; OTA läuft über Update-Tab in setup.html.
- Die Abschnitte „GSD Workflow Enforcement", „Developer Profile" u. ä. unverändert lassen.

**Test C:** `node --check firmware/data/js/setup.js firmware/data/js/mood.js firmware/data/js/script.js` (einzeln); grep-Nachweis, dass keine gelöschte Funktion mehr referenziert wird; Firmware-Build (Task A) deckt web_server.cpp-Änderungen ab.

---

## Ausdrücklich NICHT in diesem Durchgang (bewusst)

- Zugriffsschutz/Auth/CSRF (Nutzer-Vorgabe „Heimnetz").
- Löschen der 1438 Null-Zeilen in der Produktions-DB (destruktiv — nur nach expliziter Freigabe; heilt sich über das 7-Tage-Fenster selbst).
- `/testapi`-SSRF, CDN-Abhängigkeiten, Chart.js-Adapter-Entfernung, DB-Trigger-vs-Python-Kategorien-Dedup, `INTERVAL '%s hours'`-Umbau — als Follow-ups notieren.
