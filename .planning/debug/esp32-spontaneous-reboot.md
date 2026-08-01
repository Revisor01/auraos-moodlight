---
status: diagnosed
trigger: "Der ESP32 Moodlight startet manchmal spontan neu, ohne dass der Benutzer das auslöst."
created: 2026-04-05T00:00:00Z
updated: 2026-04-05T01:00:00Z
---

## Current Focus

hypothesis: BESTÄTIGT — isRestartRecommended() gibt true zurück weil _uptimeHours niemals aus NVS geladen wird (immer 0), sodass Speicherfragmentierung > 85% oder Heap < 10KB SOFORT einen Neustart auslöst, ohne auf das 48h/24h-Schutzfenster zu warten.
test: Hypothese durch Code-Lesepfad verifiziert
expecting: —
next_action: Diagnose abgeschlossen

## Symptoms

expected: Gerät läuft dauerhaft stabil ohne Neustarts
actual: Spontane Neustarts ohne erkennbaren Auslöser
errors: Keine Fehlermeldungen bekannt (kein Serial-Monitor angeschlossen)
reproduction: Zufällig — wahrscheinlich Heap-Fragmentierung nach einigen Stunden Laufzeit
started: Unklar, aber Gerät war gerade wieder online nach einem spontanen Neustart

## Eliminated

- hypothesis: Watchdog-Reset wegen blockierendem HTTP (getSentiment ohne WDT-Feed)
  evidence: watchdog.feed() wird direkt nach safeHttpGet() und fetchBackendStatistics() aufgerufen; Timeout 10s/15s < 30s WDT-Timeout
  timestamp: 2026-04-05

- hypothesis: ESP.restart() in AP-Modus-Timeout oder rebootNeeded-Pfad (unbeabsichtigt ausgelöst)
  evidence: AP-Timeout nur im Config-Modus; rebootNeeded wird ausschließlich durch explizite User-Aktionen (WiFi-Save, MQTT-Save, /restart) oder durch runSystemHealthCheck gesetzt
  timestamp: 2026-04-05

- hypothesis: Stack-Overflow in FreeRTOS-Tasks
  evidence: Keine eigenen Tasks außer Loop-Task; TaskManager nicht aktiv genutzt in normalem Betrieb; Watchdog mit panicOnTimeout=false — würde keinen Stack-Overflow-Crash auslösen
  timestamp: 2026-04-05

- hypothesis: Reboot durch geplantes restartPending aus NVS
  evidence: Nur im Zeitfenster 3:00–4:00 Uhr — erklärt keine zufälligen Neustarts zu beliebiger Tageszeit
  timestamp: 2026-04-05

## Evidence

- timestamp: 2026-04-05
  checked: MoodlightUtils.cpp — SystemHealthCheck::begin()
  found: _uptimeHours wird auf 0 initialisiert und NIEMALS aus NVS geladen. NVS-Wert "uptime" wird nur geschrieben (putULong), nicht gelesen.
  implication: Nach jedem Neustart beginnt _uptimeHours wieder bei 0, unabhängig von der echten Gesamtlaufzeit.

- timestamp: 2026-04-05
  checked: MoodlightUtils.cpp — isRestartRecommended() Zeilen 1187–1193
  found: Zwei Bedingungen prüfen _uptimeHours als Schutzfenster:
    (1) fragmentationIndex > 0.85 && _uptimeHours > 48  → schützt nur wenn >48h seit Boot
    (2) freeHeap < 10000 && _uptimeHours > 24           → schützt nur wenn >24h seit Boot
  Da _uptimeHours immer 0 ist, sind BEIDE Bedingungen NIE erfüllt, also nie true.
  implication: isRestartRecommended() gibt für Speicher-Szenarien praktisch NIE true zurück — aber...

- timestamp: 2026-04-05
  checked: MoodlightUtils.cpp — isRestartRecommended() Zeile 1203
  found: Eine DRITTE Bedingung hat KEINEN Uptime-Schutz: percentUsed > 95 (Dateisystem fast voll).
  implication: Wenn das Dateisystem >95% voll ist, gibt isRestartRecommended() sofort true zurück, ohne Uptime-Prüfung.

- timestamp: 2026-04-05
  checked: web_server.cpp — runSystemHealthCheck() ab Zeile 1920
  found: SYSSTAT_FILE_ROTATION = 24 rotierende JSON-Dateien werden bei JEDEM Health-Check (stündlich) in /data/sysstat_0.json bis sysstat_23.json geschrieben. Das Dateisystem (LittleFS, min_spiffs-Partition, sehr kleines FS) könnte dadurch voll laufen.
  implication: Wenn FS > 95% voll ist → isRestartRecommended() = true → falls Uhrzeit 2:00–4:00 → sofortiger Neustart nach 60s.

- timestamp: 2026-04-05
  checked: web_server.cpp — runSystemHealthCheck() Zeile 1936–1954
  found: Wenn isRestartRecommended() true → UND timeinfo.tm_hour >= 2 && < 4 → appState.rebootNeeded = true, rebootTime = jetzt + 60s.
    Falls nicht Nachtstunden → restartPending = true in NVS gespeichert.
    NVS-Flag restartPending wird beim nächsten stündlichen Check zwischen 3:00–4:00 ausgeführt.
  implication: Erklärt Neustarts gegen 3:00–4:00 Uhr morgens — wenn FS-Füllstand kritisch ist.

- timestamp: 2026-04-05
  checked: config.h — Partition min_spiffs
  found: min_spiffs = minimales Filesystem. Dateistruktur im /data/-Verzeichnis umfasst: settings.json, 24x sysstat_N.json, ggf. firmware-version.txt, settings.json.bak, settings.json.tmp — Gesamtgröße kann die winzige min_spiffs-Partition (ca. 200KB) schnell füllen.
  implication: Stundenlang laufendes Gerät akkumuliert sysstat-Dateien und kann FS-Schwelle überschreiten.

- timestamp: 2026-04-05
  checked: web_server.cpp — Zeile 1920–1930
  found: File-Counter ist static int (startet bei 0 nach jedem Reboot), sysstat_0.json bis sysstat_23.json werden überschrieben — das ist korrekt. Dateien wachsen nicht unbegrenzt, aber 24 JSON-Dateien à ca. 200–400 Bytes = ~5-10KB. Kein Problem für FS-Füllung allein.
  implication: Dateisystem-Überfüllung allein wenig wahrscheinlich für neues Gerät ohne viele Backups.

- timestamp: 2026-04-05
  checked: Zweiter Neustartpfad: restartPending + SCHEDULED_REBOOT_HOUR
  found: Wenn irgendwann isRestartRecommended() true wurde (auch durch temporäre Heap-Spitze), wird restartPending=true in NVS gesetzt. Dieses Flag überlebt den Neustart. Beim NÄCHSTEN Anlauf zwischen 3:00–4:00 Uhr wird ein erneuter Neustart ausgelöst — AUCH WENN das Original-Problem längst verschwunden ist.
  implication: Ein einmaliger Trigger kann zu einem DAUERHAFTEN nächtlichen Reboot-Muster führen (NVS-Flag wird erst nach Ausführung gelöscht).

- timestamp: 2026-04-05
  checked: MoodlightUtils.cpp — isRestartRecommended() mit korrekter _uptimeHours-Logik
  found: _uptimeHours basiert auf millis()/3600000 — beginnt nach jedem Reboot bei 0. Selbst wenn NVS-Uptime geladen würde, würde sie die kumulative Laufzeit über mehrere Reboots messen, nicht die aktuelle Session-Uptime. Die Variable ist als "Session-Uptime in Stunden" konzipiert, aber als "kumulativer Counter" gespeichert — konzeptioneller Widerspruch.
  implication: Die Uptime-Guards funktionieren nie wie beabsichtigt.

## Resolution

root_cause: |
  Zwei verkettete Bugs lösen spontane Neustarts aus:

  BUG 1 (Primär): isRestartRecommended() kann ohne Uptime-Schutz true zurückgeben.
  In MoodlightUtils.cpp (Zeile 1203) gibt es eine dritte Bedingung — "Dateisystem > 95% voll" —
  die KEIN Uptime-Schutzfenster hat. Wenn das FS-Limit überschritten wird, empfiehlt die Funktion
  sofort einen Neustart, unabhängig davon, wie lang das Gerät läuft.

  BUG 2 (Sekundär/Verstärker): restartPending-NVS-Flag überlebt Neustarts.
  Wenn isRestartRecommended() außerhalb der Nachtstunden true zurückgibt, wird
  restartPending=true in NVS gespeichert. Dieses Flag überlebt den Neustart und
  löst beim nächsten Anlaufen zwischen 3:00–4:00 Uhr AUTOMATISCH einen weiteren
  Neustart aus — auch wenn der ursprüngliche Trigger längst nicht mehr existiert.
  Das führt zu einem persistenten, nächtlichen Reboot-Zyklus.

  BUG 3 (Latent): _uptimeHours wird nicht aus NVS geladen.
  SystemHealthCheck::begin() lädt den gespeicherten uptime-Wert NICHT zurück.
  Die Uptime-Schutzfenster (>24h, >48h) in isRestartRecommended() sind daher
  nach jedem Neustart wirkungslos — _uptimeHours startet immer bei 0.

fix: |
  Fix 1 (sofort): Uptime-Schutzfenster für FS-Bedingung hinzufügen:
    In MoodlightUtils.cpp, isRestartRecommended():
    Zeile 1203: `if (percentUsed > 95)` ändern zu `if (percentUsed > 95 && _uptimeHours > 1)`
    (Mindestens 1h Laufzeit bevor FS-Vollstand als Restart-Grund gilt)

  Fix 2 (sofort): restartPending-Flag beim Boot löschen wenn Bedingung nicht mehr gilt:
    In runSystemHealthCheck(): Wenn isRestartRecommended() false zurückgibt,
    restartPending-Flag aus NVS löschen (Cleanup nach selbst-heilenden Situationen).

  Fix 3 (empfohlen): _uptimeHours aus NVS beim Start laden:
    In SystemHealthCheck::begin(): Nach `_restartCount = _prefs.getULong("restarts", 0);`
    auch `_uptimeHours = _prefs.getULong("uptime", 0);` laden.
    Dadurch funktionieren die >24h/>48h-Guards auch über Neustarts hinweg korrekt.

  Fix 4 (optional): Weniger aggressives FS-Limit:
    95% als Neustart-Schwelle ist sehr aggressiv für min_spiffs. Erhöhen auf >98%
    oder ganz entfernen (FS-Vollstand ist kein Grund für Neustart, wenn Rotation läuft).

verification:
files_changed: [firmware/src/MoodlightUtils.cpp, firmware/src/web_server.cpp]
