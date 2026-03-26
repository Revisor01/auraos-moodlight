# Pitfalls Research

**Domain:** ESP32 IoT Firmware Stabilization + Flask Backend Hardening + Combined OTA Update
**Researched:** 2026-03-25 (Phase 1/2), 2026-03-26 (Combined OTA Milestone)
**Confidence:** HIGH (pitfalls sind direkt aus Code-Analyse und verifizierten Community-Quellen abgeleitet)

---

## Combined OTA Update Pitfalls (Aktueller Milestone)

Diese Sektion behandelt Pitfalls spezifisch fuer den Combined Update Milestone: eine einzige TGZ-Datei die UI-Dateien + firmware.bin enthaelt, vom ESP32 sequentiell verarbeitet wird.

---

### Pitfall OTA-1: LittleFS 128 KB Limit macht Combined-TGZ-Upload physisch unmoeoglich (KRITISCH)

**What goes wrong:**
Die `min_spiffs.csv`-Partition hat nur 0x20000 = **128 KB** fuer das gesamte LittleFS. Der aktuelle Upload-Workflow schreibt das TGZ nach `/temp/`, entpackt nach `/extract/`, kopiert nach `/`. Gleichzeitig muessen die alten HTML/CSS/JS-Dateien noch vorhanden sein. Die UI-TGZ `UI-9.0-AuraOS.tgz` ist bereits ca. 15-25 KB (HTML/CSS/JS komprimiert), die firmware.bin ist ~900-1000 KB. Eine Combined-TGZ mit firmware.bin drin waere also ~1 MB+ — weit jenseits der 128 KB LittleFS-Kapazitaet. Selbst ohne firmware.bin: `/temp/TGZ` + entpackte Dateien in `/extract/` + bestehende Dateien in `/` ueberschreiten leicht das 128 KB Limit.

**Why it happens:**
`min_spiffs.csv` wurde gewaehlt um maximalen Flash fuer den App-Code zu bekommen (1920 KB OTA-Partition). Das war richtig fuer Firmware-Groesse, aber die Konsequenz fuer LittleFS bei Upload-Operationen wurde nie analysiert. Das LittleFS wird gleichzeitig als Upload-Staging, Extraktions-Ziel und Live-Webserver genutzt.

**Consequences:**
- Combined-TGZ-Upload schlaegt ab ca. 50-60 KB nutzlast mit "Fehler beim Schreiben" fehl (LittleFS voll)
- firmware.bin (900 KB+) kann definitiv nicht nach LittleFS geschrieben werden — auch nicht temporaer
- `tarGzExpanderNoTempFile()` (aktuell genutzt) ist NUR als Workaround fuer genau dieses Problem gedacht — funktioniert bei reinen UI-Updates, aber nicht wenn die firmware.bin im Archiv steckt und ins OTA-Partition-Flash geschrieben werden muss

**Prevention:**
Zwei valide Architektur-Entscheidungen:

Option A (empfohlen): Combined-TGZ enthaelt NUR UI-Dateien. Firmware.bin bleibt als separate Upload-Datei. Der "Combined" ist ein Konzept-Umbruch: Ein einziger Browser-Request postet beides sequentiell, aber der ESP32-Handler empfaengt und verarbeitet sie als two-step-Stream. ODER: Build-Script erzeugt eine `.zip`-aehliche Datei mit Manifest, ESP32 erwartet Firmware als letztes (nach UI), streamt es direkt in `Update.write()` ohne Filesystem-Staging.

Option B: Partition-Schema auf `default.csv` oder `default_8MB.csv` aendern wenn 8 MB Flash-Chip vorhanden. Standard ESP32-DevKit hat 4 MB — `default.csv` gibt LittleFS 1.5 MB, aber App-Partition schrumpft auf 1.2 MB (potentiell zu klein fuer aktuelle Firmware ~900 KB + Wachstumsraum).

**Wichtigste Architektur-Konsequenz:**
firmware.bin darf niemals in LittleFS gelandet werden. Der Combined-Handler muss firmware.bin-Inhalte direkt aus dem TGZ-Stream an `Update.write()` pipen, ohne Zwischenspeicher auf LittleFS. ESP32-targz unterstuetzt Stream-Callbacks fuer genau diesen Fall (`setTarStatusProgressCallback`, `tarGzStreamExpander`).

**Warning signs:**
- `ls -lh releases/v*/UI-*.tgz` zeigt Dateien > 60 KB
- Heap-Log zeigt "Fehler beim Schreiben: Nur X von Y Bytes" kurz nach Upload-Start
- `LittleFS.totalBytes()` minus `LittleFS.usedBytes()` < 40 KB vor Upload-Start

**Phase to address:**
Phase 1 des Combined-OTA-Milestones — muss die Architektur-Entscheidung (Stream-Ansatz vs. Partition-Wechsel) vor jeder Implementierung treffen

---

### Pitfall OTA-2: tarGzExpanderNoTempFile() erfordert ausreichend freien Heap (MIN ~60 KB)

**What goes wrong:**
`tarGzExpanderNoTempFile()` dekomprimiert die gesamte GZ-Schicht im RAM bevor es die TAR-Extraktion startet. Bei <60 KB freiem Heap (typisch nach WiFi+MQTT+WebServer+ArduinoHA-Initialisierung) schlaegt `malloc()` innerhalb der Bibliothek still fehl — `tarGzGetError()` gibt -2 zurueck ("Nicht genug Speicher"). Der aktuelle Code loggt den Heap vor Extraktion, aber bricht den Upload nicht ab, wenn der Heap zu niedrig ist.

**Why it happens:**
Die targz-Bibliothek arbeitet mit internen Puffern (Dictionary fuer zlib, TAR-Block-Buffer). Bei 4 MB ESP32 mit Standard-PSRAM-losen Board bleibt nach WiFi-Stack (~60 KB), ArduinoHA, JSON Buffer Pool (~48 KB), WebServer wenig uebrig.

**Consequences:**
- Upload scheinbar erfolgreich (HTTP 200), aber keine Dateien werden extrahiert
- Kein Rollback — `/backup/` hat alte Versionen, aber automatisches Rollback ist nicht implementiert
- Nach Fehler bleibt `/temp/TGZ` auf LittleFS liegen (128 KB Partition dabei weiter belegt)

**Prevention:**
- Heap-Check VOR dem Upload-Accept: `if (ESP.getFreeHeap() < 65536) { server.send(507, "text/plain", "Nicht genug Speicher"); return; }`
- `/temp`-Verzeichnis nach JEDEM Upload (Erfolg oder Fehler) bereinigen — aktueller Code bereinigt nur bei Erfolg
- Alternative: `tarGzStreamExpander` nutzen, der dateiweise streamt statt alles im RAM zu halten — niedrigerer Peak-Heap

**Warning signs:**
- Serial-Log: "Verfuegbarer Heap vor Extraktion: XXXX" mit Wert < 60000
- `tarGzGetError()` gibt -2 zurueck
- Upload-Seite zeigt "Erfolg" aber UI-Dateien sind unveraendert (keine Version-Bump-Bestaetigung)

**Phase to address:**
Phase 1 des Combined-OTA-Milestones — Heap-Check-Guard vor Upload-Handling

---

### Pitfall OTA-3: Version-Extraktion aus Dateiname ist fragil und bricht beim Combined-Format

**What goes wrong:**
Aktuell extrahiert der Handler die Version aus dem Dateinamen: `UI-9.1-AuraOS.tgz` → Version `9.1`. Der neue Handler fuer Combined-TGZ (`Combined-10.0-AuraOS.tgz` oder aehnlich) muss einen anderen Praefix erkennen. Wenn der Dateiname-Parse-Code nicht angepasst wird, extrahiert er eine falsche oder leere Version, was `extractedVersion.length() == 0` triggert und kein `ui-version.txt` geschrieben wird.

**Zusaetzliches Problem:** Das Build-Script extrahiert die Version mit `grep '^#define MOODLIGHT_VERSION' firmware/src/config.h | cut -d'"' -f2`. Wenn das Format in `config.h` sich aendert (z.B. `#define MOODLIGHT_VERSION "10.0"` → `#define MOODLIGHT_VERSION "v10.0"`) bricht der grep-cut still — das Build-Script laeuft durch, aber `VERSION` ist leer, `RELEASE_DIR` wird `releases/v/` statt `releases/v10.0/`.

**Why it happens:**
Stringparsing statt eines maschinenlesbaren Manifests. Die Version steht dreimal: in `config.h`, im Dateinamen, und in `ui-version.txt` — kein Single Source of Truth.

**Consequences:**
- Version wird nicht in `ui-version.txt` geschrieben → Diagnostics zeigt nach Update alte Version
- `RELEASE_DIR` leer → Build-Script erstellt Checksums fuer alle Release-Unterordner statt nur den aktuellen
- Bei Combined-Format: Firmware-Version und UI-Version waren getrennt, muessen jetzt unified sein — jede Logik die noch beide separat behandelt, zeigt falsche Versionsinformationen

**Prevention:**
- Eine `VERSION.txt` im Archiv-Root der Combined-TGZ als maschinenlesbares Manifest: der Handler liest diese Datei zuerst, extrahiert Version daraus — kein Dateiname-Parsing noetig
- Build-Script: Strenge Validierung nach dem `grep`: `[ -z "$VERSION" ] && echo "ERROR: VERSION leer" && exit 1`
- Unified Version von Anfang an: `config.h` ist Single Source of Truth, Build-Script liest sie einmal und schreibt die gleiche Version in den Dateinamen UND ins Archiv-Manifest

**Warning signs:**
- `BUILD_DIR` ist leer oder unerwartet (pruefen mit `echo "VERSION=${VERSION}"` am Anfang des Build-Scripts)
- Diagnostics-Seite zeigt nach Update noch die alte Version
- `ui-version.txt` hat nach Update den falschen Inhalt (Dateiinhalt per curl `/api/firmware-version` pruefen)

**Phase to address:**
Phase 1 des Combined-OTA-Milestones — zuerst Build-Script-Validation, dann Handler-Manifest-Lesen

---

### Pitfall OTA-4: Kein atomares Update — UI-Dateien aktualisiert, Firmware-Flash schlaegt fehl

**What goes wrong:**
Im Sequentiell-Ansatz (erst UI extrahieren, dann Firmware flashen) kann folgendes passieren: UI-Update erfolgreich, dann `Update.begin()` schlaegt fehl (nicht genug Partition-Platz, beschaedigte App-Partition, falscher Pruefsummenwert). Das Geraet started danach mit neuer UI aber alter Firmware — die UI kann API-Endpunkte erwarten die in der alten Firmware nicht existieren.

**Why it happens:**
ESP32-OTA-Flash und LittleFS-Schreiboperationen sind vollstaendig unabhaengige Operationen ohne gemeinsame Transaktion. `Update.begin()`/`Update.end()` ist atomar (der ESP32 wechselt OTA-Slot erst wenn `Update.end(true)` aufgerufen wird), aber die LittleFS-Schreiboperationen vorher sind es nicht.

**Consequences:**
- Web-UI zeigt neue Version, Firmware hat alte Version — verwirrend in Diagnostics
- Wenn neue UI mit geaenderten API-Endpunkten arbeitet, brechen Requests zur alten Firmware
- Kein automatisches Rollback ohne physischen Zugang

**Prevention:**
Reihenfolge invertieren: **Erst Firmware flashen, dann UI extrahieren**. Begruendung: `Update.end(true)` ist de-facto atomar — wenn er fehlschlaegt, bleibt die alte Firmware und die alte UI ist noch intakt. Wenn er erfolgreich ist, folgt unmittelbar die UI-Extraktion, und danach Neustart. Bei UI-Fehler nach erfolgreichem Firmware-Flash: zumindest Firmware ist aktuell, Rollback der UI aus `/backup/` moeglich.

Alternativ: Rollback-Logik implementieren — wenn UI-Extraktion nach erfolgreichem Firmware-Flash fehlschlaegt, alle Backup-Dateien zurueckkopieren.

**Warning signs:**
- `Update.hasError()` ist true nach Combined-Upload
- Serial-Log zeigt "Update Begin fehlgeschlagen" nachdem UI bereits geschrieben wurde
- Version-Mismatch zwischen `/api/firmware-version` und `/api/status`

**Phase to address:**
Phase 1 des Combined-OTA-Milestones — Reihenfolge (Firmware zuerst) in Handler-Architektur festlegen

---

### Pitfall OTA-5: esp_task_wdt_init API-Inkompatibilitaet blockiert Build

**What goes wrong:**
Der aktuelle Code ruft `esp_task_wdt_init(30, false)` auf. In Arduino ESP32 Core 3.x (espressif32 >= 6.x in PlatformIO) hat sich die API geaendert: `esp_task_wdt_init()` akzeptiert jetzt eine `esp_task_wdt_config_t`-Struktur statt separate Parameter. Das ist ein bekannter Breaking Change in esp-idf v5.x. Der Fehler verhindert `pio run` komplett — ohne diesen Fix kann das Projekt nicht gebaut werden.

**Why it happens:**
`platformio.ini` pinnt keine `platform`-Version. PlatformIO aktualisiert `espressif32` beim naechsten `pio run` automatisch, was den Breaking Change einfuehrt. In `platformio.ini` fehlt eine explizite Platform-Versionsangabe.

**Consequences:**
- `pio run` schlaegt mit Compile-Error fehl, kein Build moeglich
- Combined-OTA-Milestone kann nicht beginnen bis dieser Fix deployed ist

**Prevention:**
Zwei Optionen:
1. Kurzfristig: Platform-Version in `platformio.ini` pinnen: `platform = espressif32@6.3.2` (letzte Version mit esp-idf 4.x-kompatiblem API-Set). ODER: Watchdog-Call auf die neue API umschreiben.
2. Langfristig: Korrekter Fix auf neue API:
```cpp
esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 30000,
    .idle_core_mask = 0,
    .trigger_panic = false
};
esp_task_wdt_reconfigure(&wdt_config);
```
Diese API ist in `esp_task_wdt.h` ab esp-idf 5.0.

**Warning signs:**
- `pio run` Fehler: `error: too many arguments to function 'esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t*)'`
- `pio run` nach Update von PlatformIO-Packages funktioniert ploetzlich nicht mehr
- `platformio.ini` hat `platform = espressif32` ohne Versionsangabe

**Phase to address:**
Sofort — erster Schritt vor allem anderen im Milestone, da der Build sonst nicht moeglich ist

---

### Pitfall OTA-6: Static-State-Bug im Upload-Handler bei Combined-Upload-Abbruch

**What goes wrong:**
Der aktuelle `handleUiUpload()`-Handler nutzt `static`-Variablen (`static String uploadPath`, `static bool isTgzFile`, `static String extractedVersion`). Diese static Variablen bleiben ueber HTTP-Requests hinweg erhalten. Wenn ein Upload abbricht (Client-Verbindung getrennt, Browser-Tab geschlossen), bleibt `isTgzFile = true` und `uploadPath` auf den alten Pfad gesetzt. Der naechste Upload-Versuch behandelt einen anderen Request-Kontext als haette er den letzten fortgesetzt.

**Why it happens:**
`static`-Variablen in Handler-Funktionen sind ein Anti-Pattern in wiederverwendbaren HTTP-Handlern. Sie wurden wahrscheinlich genutzt um State zwischen `UPLOAD_FILE_START`, `UPLOAD_FILE_WRITE` und `UPLOAD_FILE_END`-Callbacks zu teilen — ein notwendiges Design mit dem ESP32-WebServer-Framework. Das Problem entsteht wenn der `UPLOAD_FILE_END`-Callback nie aufgerufen wird.

**Consequences:**
- Zweiter Upload-Versuch nach Abbruch verhaelt sich unklar (falsche Pfade, ggf. Schreiben in alten Upload-File)
- Bei Combined-Handler: wenn der Handler nach UI-Extraktion auf Firmware wartet aber der Firmware-Teil nie kommt, bleibt State erhalten der den naechsten regulaeren UI-Upload korrumpiert

**Prevention:**
- State explizit im `UPLOAD_FILE_START`-Zweig zuruecksetzen (bereits fuer `extractedVersion` getan, aber `isTgzFile` und `uploadPath` muessen bei jedem neuen `UPLOAD_FILE_START` explizit initialisiert werden)
- Timeout-Logik: Wenn `UPLOAD_FILE_END` nach X Sekunden seit `UPLOAD_FILE_START` nicht aufgerufen wurde, State in `loop()` zuruecksetzen
- Fuer Combined-Handler: State-Machine mit `enum UploadState { IDLE, RECEIVING_TGZ, EXTRACTING_UI, FLASHING_FIRMWARE }` statt loose static Variablen

**Warning signs:**
- Nach Abbruch eines Uploads funktioniert der naechste Upload seltsam (falsche Dateien werden ueberschrieben)
- Serial-Log zeigt "Upload gestartet" fuer Datei B, aber danach Aktionen bezogen auf Datei A
- `uploadPath` enthaelt nach Neustart noch alten Pfad wenn `/temp/`-Verzeichnis noch existiert

**Phase to address:**
Phase 1 des Combined-OTA-Milestones — State-Machine-Design am Anfang, nicht als Nachbesserung

---

### Pitfall OTA-7: Build-Script schlaegt still bei pio-Fehler und deployed defekte Firmware

**What goes wrong:**
Das Build-Script hat `set -e` gesetzt, aber der `pio run`-Aufruf ist so implementiert:
```bash
pio run -s 2>&1 | grep -E "(SUCCESS|error|Error)" || true
```
Das `|| true` verhindert, dass `set -e` bei einem Compile-Fehler abbricht. Der `grep`-Filter filtert den Exit-Code weg. Das Skript laeuft dann zu `cp firmware/.pio/build/esp32dev/firmware.bin ...` weiter — aber wenn der Build fehlschlaegt, gibt es keine neue `firmware.bin`, es wird die alte kopiert (sofern sie noch vorhanden ist). Das Release-Verzeichnis enthaelt dann eine Firmware die nicht dem aktuellen Code entspricht.

**Gleichzeitig:** Der Version-Bump (Active Requirement) soll die Version in `config.h` erhoehen und committen. Wenn das Build-Script committet ohne den Build-Erfolg zu verifizieren, landet ein Commit mit erhoehter Versionsnummer aber ohne valide Firmware.

**Why it happens:**
`|| true` wurde wahrscheinlich eingefuegt weil `grep` Exit-Code 1 zurueckgibt wenn keine Matches gefunden werden. Der korrekte Fix waere PIPESTATUS-Pruefung, nicht das Ausschalten des Exit-Codes.

**Consequences:**
- Release-Verzeichnis enthaelt alte `firmware.bin` mit neuer Versionsnummer — Versionsmismatch
- Wenn Version-Bump + Commit im gleichen Script: Git-History hat eine Version ohne funktionierende Firmware
- OTA-Update deployt unbemerkterweise eine alte Firmware

**Prevention:**
```bash
# Korrekt:
if ! pio run; then
    echo "ERROR: Firmware-Build fehlgeschlagen"
    exit 1
fi
```
Oder mit PIPESTATUS:
```bash
pio run -s; BUILD_STATUS=${PIPESTATUS[0]}
[ $BUILD_STATUS -ne 0 ] && echo "Build fehlgeschlagen" && exit 1
```
Zusaetzlich: Commit-Schritt darf erst nach erfolgreich validierter Checksums-Pruefung ausgefuehrt werden.

**Warning signs:**
- `ls -lt firmware/.pio/build/esp32dev/firmware.bin` zeigt altes Dateidatum nach Build-Script-Run
- Build-Script-Output endet nicht mit "SUCCESS" oder Dateigroesse bleibt unveraendert
- Version in `config.h` wurde gebumpt aber Firmware-Binary hat alte Groesse

**Phase to address:**
Phase 1 des Combined-OTA-Milestones — Build-Script-Robustheit als erstes, vor dem Version-Bump-Feature

---

### Pitfall OTA-8: Cleanup von /extract-Verzeichnis loescht keine Unterverzeichnisse

**What goes wrong:**
Der aktuelle Code bereinigt `/extract` mit einer flachen Schleife:
```cpp
File extractFile = extractRoot.openNextFile();
while (extractFile) {
    String path = String(extractFile.path());
    extractFile = extractRoot.openNextFile();
    LittleFS.remove(path);
}
```
`LittleFS.remove()` loescht keine Verzeichnisse — nur Dateien. Das TGZ entpackt nach `/extract/css/` und `/extract/js/`. Beim zweiten Upload-Versuch existieren diese Unterverzeichnisse noch, aber die Dateien darin wurden nicht geloescht (weil die Schleife sie als Verzeichnis ueberspringt, nicht als Datei). Die extrahierten Dateien koennen dann auf verschmutzte Verzeichnisse treffen.

**Why it happens:**
LittleFS-API-Unterschied: `remove()` fuer Dateien, `rmdir()` fuer Verzeichnisse (nur wenn leer). Rekursives Loeschen muss manuell implementiert werden. Die vorhandene `removeDirRecursive()`-Funktion existiert im Code aber wird fuer `/extract` nicht genutzt.

**Consequences:**
- Zweiter Upload entpackt in `/extract/css/style.css` das existiert bereits, `copyFile()` ueberschreibt es (ok)
- ABER: Verzeichnis-Handle von `extractRoot` ist noch offen waehrend `LittleFS.remove()` aufgerufen wird — moegliche Korrumpierung auf einigen LittleFS-Versionen
- Bei neuer TGZ-Struktur mit zusaetzlichen Unterverzeichnissen (z.B. `/extract/fonts/`) bleiben alte Inhalte erhalten

**Prevention:**
Den vorhandenen `removeDirRecursive()`-Call nutzen:
```cpp
removeDirRecursive("/extract");
LittleFS.mkdir("/extract");
```
Statt der manuellen Schleife. Dasselbe fuer `/temp/`-Bereinigung.

**Warning signs:**
- Zweiter Upload nach Fehler hinterlaesst Artefakte aus erstem Upload in der UI
- `LittleFS.usedBytes()` bleibt nach Cleanup gleich obwohl `/extract/` leer sein sollte

**Phase to address:**
Phase 1 des Combined-OTA-Milestones — Cleanup-Robustheit in Handler-Implementierung

---

## Critical Pitfalls (Phase 1/2 — weiterhin relevant)

### Pitfall 1: NeoPixel show() crasht unter WiFi-Last durch Interrupt-Timing-Konflikte

**What goes wrong:**
Die NeoPixel-Bibliothek (Adafruit oder FastLED) sendet Bits mit extrem engem Timing (150 ns Toleranz). WiFi-Interrupts und MQTT-Callbacks storten diese Zeitfenster, was zu Pixel-Datenverfaelschung oder einem kompletten CPU-Exception-Crash fuehrt. Das Symptom ist das bekannte "unerklaerliche Blinken" im laufenden Betrieb.

**Why it happens:**
ESP32 Arduino Core-Versionen >= 3.0.x haben bekannte Regressions mit NeoPixel-Bibliotheken (insbesondere >75 LEDs crashen reproduzierbar). Aber auch bei 12 LEDs: WiFi-Interrupt-Handler laufen auf demselben Core und koennen im kritischen Timing-Fenster des `show()`-Calls einfallen. Das Problem wird verstaerkt wenn WiFi reconnect und LED-Update gleichzeitig stattfinden.

**How to avoid:**
- Sicherstellen, dass LED-Updates via `ledMutex` (bereits vorhanden) konsequent durch alle Codepfade geschuetzt sind — kein direkter `strip.show()` ausserhalb des LED-Tasks
- LED-Update-Task auf Core 1 pinnen (WiFi laeuft auf Core 0): `xTaskCreatePinnedToCore(..., 1)`
- Bei RMT-basierter NeoPixel-Implementation: RMT-Kanal verwenden statt Bit-Banging (deutlich interrupt-resistenter)
- ESP32 Arduino Core Version im `platformio.ini` explizit pinnen (getestete stabile Version, z.B. 2.0.17)
- **Niemals `strip.show()` aus WiFi-Event-Callbacks oder MQTT-Callbacks aufrufen**

**Warning signs:**
- LED blinkt kurz weiss oder falsche Farbe waehrend WiFi-Reconnects
- Serial-Log zeigt `Guru Meditation Error: Core 0 panic'ed` mit Exception 28 (Load/Store Alignment)
- Crash tritt bevorzugt in den ersten Sekunden nach WiFi-Reconnect auf

**Phase to address:**
Phase 1 (LED-Stabilisierung) — hoechste Prioritaet, da dies das primaere Symptom des Projekts ist

---

### Pitfall 2: Buffer-Overflow durch feste Array-Groesse bei konfigurierbarem numLeds

**What goes wrong:**
`ledColors[DEFAULT_NUM_LEDS]` ist mit 12 Elementen auf dem Stack allokiert. Wenn ein Nutzer `numLeds = 20` konfiguriert, schreibt der Loop `for (int i = 0; i < numLeds; i++) ledColors[i] = ...` in Speicher jenseits des Arrays. Auf dem ESP32 korrumpiert das benachbarte globale Variablen — oft still, manchmal mit Watchdog-Reset, selten mit sofortigem Crash.

**Why it happens:**
Die Array-Groesse wurde nie an die Konfigurierbarkeit von `numLeds` angepasst. Das ist ein klassischer Fehler bei nachtraeglicher Flexibilisierung ohne Anpassung der Datenstrukturen.

**How to avoid:**
Konkret: `MAX_LEDS`-Konstante definieren (z.B. 60), `ledColors[MAX_LEDS]` verwenden, und `numLeds` beim Laden aus Settings gegen `MAX_LEDS` clampen: `numLeds = min(loadedNumLeds, MAX_LEDS)`. Alternativ: dynamische Allokation mit `new uint32_t[numLeds]` beim Setup — aber dann sorgfaeltig auf Heap-Fragmentierung achten.

**Warning signs:**
- Sporadische Watchdog-Resets ohne klaren Stack-Trace
- Einstellungen wie `mqttEnabled` oder `mqttPort` aendern sich unerwartet (benachbarte globale Variablen)
- Tritt nur auf wenn Nutzer LED-Anzahl > 12 konfiguriert hat

**Phase to address:**
Phase 1 (LED-Stabilisierung) — One-liner Fix, sollte als erstes gemacht werden

---

### Pitfall 3: JSON Buffer Pool Memory Leak durch fehlgeschlagene Mutex-Akquisition

**What goes wrong:**
Wenn alle Pool-Buffer belegt sind, allokiert `acquire()` einen neuen Buffer mit `new char[JSON_BUFFER_SIZE]` (16 KB). `release()` versucht den Mutex mit 100 ms Timeout zu akquirieren — scheitert der Timeout, wird `isPoolBuffer = false` nicht erkannt und der Heap-Buffer wird nie freigegeben. Nach mehreren solchen Ereignissen (z.B. unter Last durch gleichzeitige Web-Requests und MQTT-Callbacks) akkumuliert Heap-Verlust.

**Why it happens:**
Das Design setzt voraus, dass `release()` immer den Mutex bekommt. Der Fallback-Fall (Timeout) fehlt. Dies ist ein klassischer Fehler bei Lock-basiertem Ressource-Management ohne RAII.

**How to avoid:**
RAII-Wrapper um den Buffer-Pool ziehen: `struct PoolGuard { char* buf; bool isHeap; ~PoolGuard() { if (isHeap) delete[] buf; else pool.release(buf); } }`. Alternativ: in `release()` den `delete[]` direkt ausfuehren wenn `isPoolBuffer == false`, unabhaengig vom Mutex.

**Warning signs:**
- `ESP.getFreeHeap()` sinkt kontinuierlich ueber Stunden (sichtbar in `/api/diagnostics`)
- Logs zeigen `JSON Buffer Pool: alle Slots belegt` haeufiger als gelegentlich
- System-Neustart nach mehreren Tagen Laufzeit ohne anderen erkennbaren Grund

**Phase to address:**
Phase 1 (Firmware-Stabilitaet) — zusammen mit LED-Stabilisierung

---

### Pitfall 4: Gunicorn mit mehreren Workers verdoppelt den Background Worker

**What goes wrong:**
Der `SentimentUpdateWorker` wird in `app.py` beim Import als Daemon-Thread gestartet. Mit Gunicorn pre-fork model wird der Prozess erst geforkt, dann starten alle Worker-Prozesse: jeder spawnt seinen eigenen Background-Worker-Thread. Mit `-w 2` Workers laufen also 2 Background-Worker gleichzeitig, beide schreiben in PostgreSQL und rufen OpenAI alle 30 Minuten auf — Kosten verdoppeln sich, Race Conditions bei DB-Writes entstehen.

**Why it happens:**
Flask Dev-Server ist single-process, daher tritt das Problem dort nicht auf. Gunicorn's pre-fork model ist der fundamentale Unterschied. Entwickler testen mit `python app.py` und sehen nie das Multi-Worker-Problem.

**How to avoid:**
Zwei valide Ansaetze:
1. **Gunicorn mit `-w 1` starten** (passt fuer diesen Use-Case: ein Geraet, Redis cached die meisten Anfragen) — einfachste Loesung
2. **Background Worker als separaten Prozess auslagern** — eigener Docker-Service mit `CMD ["python", "background_worker.py"]`, Flask-App via Gunicorn ohne Worker-Thread

**Fuer dieses Projekt:** Option 1 reicht voellig. Der Redis-Cache absorbiert alle ESP32-Anfragen ausser der erste nach Cache-Expiry. Single-worker Gunicorn ist produktionsreif fuer diesen Load-Level.

**Warning signs:**
- PostgreSQL-Logs zeigen doppelte `INSERT INTO sentiment_history` zur gleichen Zeit
- OpenAI-Kosten verdoppeln sich nach Gunicorn-Migration
- Logs zeigen zwei Workers mit identischen "Sentiment update completed"-Meldungen kurz nacheinander

**Phase to address:**
Phase 2 (Backend-Hardening) — muss VOR dem Gunicorn-Deployment klar entschieden sein

---

### Pitfall 5: Inkonsistente Sentiment-Thresholds zwischen app.py und background_worker.py verursachen falsches LED-Verhalten

**What goes wrong:**
Die Kategorie-Klassifizierung des gleichen Scores liefert unterschiedliche Ergebnisse je nachdem welcher Code-Pfad aufgerufen wird. `app.py` klassifiziert >= 0.85 als "sehr positiv", `background_worker.py` und `moodlight_extensions.py` benutzen >= 0.30. Ein Score von 0.50 ist laut `app.py` "positiv", laut den anderen beiden "sehr positiv". Der ESP32 empfaengt die Kategorie aus `moodlight_extensions.py` — was die Web-UI anzeigt kann also von dem abweichen, was der LED-Farbmapping-Code erwartet.

**Why it happens:**
Drei separate Definitionen ohne shared constant oder single source of truth. Bei nachtraeglichen Anpassungen wurde nicht sichergestellt, dass alle Dateien aktualisiert wurden.

**How to avoid:**
Eine `SENTIMENT_THRESHOLDS`-Konstante in `database.py` oder einem neuen `constants.py` definieren. Alle drei Dateien importieren diese. Das ist ein 15-Minuten-Fix, der Verwirrung dauerhaft eliminiert.

**Warning signs:**
- Kategorie-Label in `/api/moodlight/current` Response stimmt nicht mit dem ueberein, was die Diagnostics-Page des ESP32 zeigt
- Nach Threshold-Anpassung in einer Datei aendert sich das Verhalten nicht wie erwartet

**Phase to address:**
Phase 2 (Backend-Hardening) — beim RSS/Kategorie-Konsolidierungsschritt

---

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Monolith in einer `.cpp`-Datei | Einfacher Build, kein Linking | Kein Isoliertes Testing, Merge-Konflikte, globale Variablen schwer nachverfolgbar | Vorerst akzeptabel — Splitting ist eigener Milestone |
| `socket.setdefaulttimeout()` global | Einfach zu schreiben | Race condition zwischen Flask-Thread und Background-Worker, alle sockets betroffen | Nie akzeptabel in Multi-Thread-Kontext |
| Flask Dev-Server in Produktion | Kein Konfigurationsaufwand | Kein Signal-Handling, kein graceful shutdown, single-threaded request handling | Nie akzeptabel — erster Fix |
| Duplizierte RSS-Feed-Liste | Schnell copy-pasten | Drift zwischen Dateien, Background-Worker ignoriert API-seitige Aenderungen | Nie akzeptabel nach Stabilisierungsphase |
| Hardcoded `DEFAULT_NUM_LEDS = 12` fuer Array | Keine dynamische Allokation noetig | Buffer-Overflow bei jeder Konfiguration > 12 LEDs | Akzeptabel nur wenn `numLeds` nicht konfigurierbar waere |
| Health-Check-Logik in zwei Timern | Schnell hinzugefuegt | Konfligierende Restart-Entscheidungen, doppelter Resource-Verbrauch | Nie akzeptabel — konsolidieren |
| Static-Variablen in Upload-Handler | Einfacher State-Transfer zwischen Callbacks | State bleibt nach Abbruch erhalten, korrumpiert Folge-Uploads | Akzeptabel nur mit explizitem Reset im UPLOAD_FILE_START |
| firmware.bin in LittleFS zwischenspeichern | Einfache Implementierung | LittleFS Limit (128 KB) macht es physisch unmoeoglich | Nie akzeptabel — Stream-Ansatz notwendig |

---

## Integration Gotchas

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| ArduinoHA (MQTT) + NeoPixel | MQTT-Callback ruft direkt `strip.show()` auf | LED-State als Variable setzen, LED-Update-Task liest Variable in eigenem Zyklus |
| Gunicorn + Flask Background Worker | `-w 2` verdoppelt Worker-Threads | `-w 1` fuer diesen Use-Case oder Worker als separaten Prozess auslagern |
| ESP32 OTA + LittleFS gleichzeitig | OTA-Update waehrend aktiver File-Ops kann FS korrumpieren | OTA-Handler sollte laufende Tasks suspendieren oder `SafeFileOps` nutzen |
| Redis Cache + Gunicorn Workers | Jeder Worker haelt eigene Redis-Verbindung, keine Verbindungswiederverwendung | `redis-py` Connection-Pool konfigurieren, nicht pro-Request neue Verbindung oeffnen |
| feedparser + global socket timeout | Globaler Timeout beeinflusst alle Sockets inkl. PostgreSQL-Verbindungen | `feedparser.parse(url, timeout=10)` direkt (feedparser unterstuetzt Timeout-Parameter) oder via requests mit `timeout=` |
| ESP32 WiFi-Events + Status-LED | `WIFI_EVENT_STA_DISCONNECTED` triggert sofort bei kurzen Unterbrechungen | Debounce: LED-Status erst nach 3-5 Sekunden anhaltender Disconnection aendern |
| ESP32-targz + LittleFS (128 KB) | TGZ-Upload + Extraktion + Backup gleichzeitig im 128 KB FS | Nur UI-Dateien (kein Firmware.bin) ins FS, firmware.bin direkt aus Stream via Update.write() |
| Combined-TGZ-Upload + Watchdog | Langer Extraktions-/Flash-Vorgang triggert Watchdog-Reset | `esp_task_wdt_reset()` in langen Operationen aufrufen, oder Watchdog-Timeout erhoehen waehrend Update |

---

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Heap-Allokation in Loop bei jedem LED-Update | `getFreeHeap()` sinkt stetig, System crasht nach Stunden/Tagen | Pre-allokierte Buffer (JSON Buffer Pool bereits vorhanden — korrekt verwenden) | Ab ~2KB Heap-Rest crasht malloc() |
| ESP32 WebServer blockiert Loop() | HTTP-Anfragen blockieren LED-Updates und MQTT-Heartbeat | HTTP-Handling ist bereits im Loop — sicherstellen dass Handler schnell zurueckkehren | Bei langsamen HTTP-Clients mit traegem TCP-Stack |
| PostgreSQL ThreadedConnectionPool mit 1-5 Connections | Connection exhaustion wenn Gunicorn Workers > Pool-Groesse | Pool-Groesse = Workers * 2, oder -w 1 fuer diesen Use-Case | Bei mehr als 5 gleichzeitigen Anfragen mit Flask-Dev-Server-Migration auf Gunicorn |
| Redis TTL 5 Minuten vs. 30-Minuten Update-Zyklus | ESP32 bekommt veraltete Daten fuer bis zu 5 Minuten nach Sentiment-Update | TTL auf 25-28 Minuten setzen (knapp unter Worker-Intervall), oder bei DB-Write Cache invalidieren (bereits implementiert) | Nicht kritisch, aber bei manuellen Cache-Invalidierungen beachten |
| tarGzExpanderNoTempFile() Peak-Heap | Extraktion schlaegt mit Fehlercode -2 fehl, keine Dateien geaendert | Heap-Check vor Upload (min. 65 KB frei), tarGzStreamExpander als Alternative | Bei <60 KB freiem Heap nach WiFi+MQTT+WebServer-Initialisierung |

---

## Security Mistakes

| Mistake | Risk | Prevention |
|---------|------|------------|
| WiFi-Passwort in `/api/export/settings` Response | Exfiltration durch jeden im lokalen Netzwerk, MQTT-Credentials ebenfalls betroffen | Passwort-Felder vor der API-Response maskieren: `"wifiPass": "****"` — Passwort wird beim Import einfach nicht ueberschrieben wenn `****` |
| `.env` nicht in `.gitignore` | `OPENAI_API_KEY` und `POSTGRES_PASSWORD` koennen aus Versehen committed werden | `.env` und `sentiment-api/.env` sofort in `.gitignore` eintragen |
| Binary-Release-Artifacts in Git | Repository-Bloat, kein praktischer Nutzen | `releases/` in `.gitignore` eintragen, GitHub Releases fuer Binaries nutzen |
| `/api/feedconfig` modifiziert globalen State ohne Auth | Jeder kann RSS-Feeds aendern, Aenderungen gehen bei Container-Restart verloren | Endpoint entfernen (kein valider Use-Case) oder hinter IP-Whitelist legen |
| Open Setup-AP ohne Passwort | Jeder in Reichweite kann das Geraet konfigurieren | Default-AP-Passwort setzen oder AP nur nach Button-Press aktivieren — Out-of-scope aber vermerken |

---

## UX Pitfalls

| Pitfall | User Impact | Better Approach |
|---------|-------------|-----------------|
| LED blinkt bei jedem kurzen WiFi-Reconnect | Wirkt defekt, stoert im Wohnzimmer-Betrieb | Reconnect-Debounce: Status-LED-Aenderung erst nach > 5 Sek. anhaltender Disconnection |
| `/api/dashboard` und `/api/logs` geben `{...}` zurueck | 500-Error wenn aufgerufen, verwirrend in Diagnostics | Endpoints entfernen oder durch sinnvolle Daten ersetzen |
| Duplicate Health-Check-Timer mit eigener Restart-Logik | Geraet kann unvorhersehbar neustarten, kein klarer Grund im Log | Ein Health-Check, eine Restart-Entscheidung, klares Logging |
| Sentiment-Kategorie-Label stimmt nicht mit LED-Farbe ueberein | Nutzer sieht "positiv" in der App aber LED zeigt Neutral-Blau | Threshold-Konsolidierung — eine Definition, ein Verhalten |
| Zwei separate Upload-Felder in Diagnostics-UI | Nutzer muss wissen: erst UI, dann Firmware, kein Fehler wenn Reihenfolge falsch | Ein Upload-Feld, Combined-TGZ, Reihenfolge intern geregelt |

---

## "Looks Done But Isn't" Checklist

**Combined OTA Milestone:**
- [ ] **LittleFS-Kapazitaet:** `LittleFS.totalBytes()` auf Geraet gecheckt — genuegt fuer UI-TGZ ohne firmware.bin?
- [ ] **Firmware-Stream-Path:** firmware.bin wird aus TGZ-Stream direkt via `Update.write()` geflasht, niemals auf LittleFS geschrieben
- [ ] **Atomaritaet:** Firmware wird VOR UI-Extraktion geflasht; bei Fehler der UI-Extraktion ist Firmware trotzdem aktuell
- [ ] **Build-Script-Exit-Code:** `pio run` Fehler bricht Build-Script ab (kein `|| true` Workaround)
- [ ] **Watchdog-API-Fix:** `esp_task_wdt_init()` mit neuer Config-Struct-API, `pio run` erfolgreich ohne Fehler
- [ ] **Version-Manifest:** Combined-TGZ enthaelt `VERSION.txt` im Root, Handler liest daraus, kein Dateiname-Parsing
- [ ] **Upload-State-Reset:** `isTgzFile`, `uploadPath`, `extractedVersion` werden bei jedem `UPLOAD_FILE_START` explizit zurueckgesetzt
- [ ] **Cleanup komplett:** `removeDirRecursive("/extract")` statt flacher Schleife; `/temp/`-Datei wird in ALLEN Exit-Pfaden geloescht

**Phase 1/2 (weiterhin relevant):**
- [ ] **Gunicorn-Migration:** Background Worker laeuft wirklich nur einmal — pruefen mit `docker logs | grep "Sentiment update started"` (darf nicht zweimal parallel auftauchen)
- [ ] **LED Buffer-Fix:** `numLeds` wird gegen `MAX_LEDS` geclampt beim Laden aus Settings — pruefen mit Konfiguration von 20 LEDs
- [ ] **JSON Buffer Pool Leak:** `ESP.getFreeHeap()` bleibt nach 24h Dauerbetrieb stabil (< 5% Drift) — Heap-Plot aus Diagnostics
- [ ] **Credentials-Maskierung:** `/api/export/settings` gibt `"wifiPass": "****"` zurueck — curl-Test
- [ ] **Socket-Timeout:** `socket.setdefaulttimeout()` ist nicht mehr im Code — grep-Verifikation
- [ ] **RSS-Duplikat entfernt:** `background_worker.py` importiert Feeds aus `app.py` oder `constants.py` — keine zweite Definition mehr
- [ ] **Threshold-Konsistenz:** Gleicher Score gibt gleiche Kategorie in allen drei Dateien — Unit-Test oder manuelle Verifikation mit Score 0.25 und 0.40

---

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| NeoPixel-Crash durch WiFi-Interrupt | MEDIUM | ESP32 OTA-Update mit gefixtem LED-Task-Core-Pinning; keine Hardware-Intervention noetig |
| Buffer-Overflow hat globale Variablen korrumpiert | LOW | Geraet neu starten loest es (Stack-Allokation), dann Fix deployen |
| JSON Buffer Pool Leak hat Heap erschoepft | LOW | Auto-Watchdog-Reset loest Symptom; Fix verhindert Wiederholung |
| Gunicorn verdoppelt Background-Worker | LOW | `docker-compose down && docker-compose up -d` mit `-w 1` Config; keine Datenverlust |
| .env-Datei versehentlich committed | HIGH | Credentials sofort rotieren (OpenAI API Key, DB-Passwort); `git filter-branch` oder BFG fuer History-Bereinigung |
| Inkonsistente Thresholds fuehren zu falschem LED-Verhalten | LOW | Backend-Fix deployen (kein ESP32 OTA noetig), Cache invalidieren |
| Combined-TGZ-Upload schlaegt wegen LittleFS-Voll fehl | MEDIUM | Gerät noch erreichbar via HTTP, altes UI intakt; Architektur-Fix (Stream-Ansatz) deployen via altem UI |
| UI aktualisiert, Firmware-Flash fehlgeschlagen | LOW-MEDIUM | Wenn Rueckfallstrategie implementiert: `/backup/` Dateien wiederherstellen via dediziertem Endpoint oder Neustart loest UI-Mismatch |
| Build-Script committet ohne validen Build | MEDIUM | `git revert` des Version-Bump-Commits; manuell korrekten Build erstellen und neu committen |

---

## Pitfall-to-Phase Mapping

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| esp_task_wdt_init API-Inkompatibilitaet | Combined OTA - Schritt 0 (Blocker) | `pio run` erfolgreich ohne Compile-Error |
| LittleFS 128 KB Limit fuer Combined-TGZ | Combined OTA - Phase 1 (Architektur-Entscheidung) | UI-TGZ wird extrahiert; firmware.bin niemals auf LittleFS |
| tarGzExpanderNoTempFile() Heap-Anforderung | Combined OTA - Phase 1 | Heap-Check-Log zeigt > 65 KB vor Extraktion; kein Fehlercode -2 |
| Version-Extraktion aus Dateiname fragil | Combined OTA - Phase 1 | Manifest in TGZ, Version-Datei korrekt geschrieben |
| Keine Atomaritaet bei Combined-Update | Combined OTA - Phase 1 | Firmware zuerst geflasht; bei UI-Fehler Firmware trotzdem aktuell |
| Static-State-Bug bei Upload-Abbruch | Combined OTA - Phase 1 | Upload nach Abbruch funktioniert normal |
| Build-Script stil bei pio-Fehler | Combined OTA - Phase 1 | Build-Script bricht mit Exit-Code 1 bei Compile-Fehler |
| LittleFS /extract Cleanup unvollstaendig | Combined OTA - Phase 1 | Zweiter Upload nach Fehler hinterlaesst keine Artefakte |
| NeoPixel/WiFi-Timing-Crash | Phase 1: LED-Stabilisierung | LED bleibt nach 24h ohne Blinken stabil; WiFi-Reconnect-Test |
| Buffer-Overflow bei numLeds > 12 | Phase 1: LED-Stabilisierung | Konfiguration mit 20 LEDs setzen, kein Crash nach 1h |
| JSON Buffer Pool Memory Leak | Phase 1: Firmware-Stabilitaet | getFreeHeap() stabil nach 24h Dauerbetrieb |
| Doppelter Background-Worker mit Gunicorn | Phase 2: Backend-Hardening | Logs pruefen: genau ein "Sentiment update" alle 30 min |
| Inkonsistente Sentiment-Thresholds | Phase 2: Backend-Hardening | Score 0.35 ergibt "positiv" ueberall; Score 0.80 ergibt "sehr positiv" ueberall |
| Credentials in API-Response | Phase 2: Backend-Hardening | curl /api/export/settings zeigt `****` fuer Passwoerter |
| Globaler Socket-Timeout | Phase 2: Backend-Hardening | Kein `setdefaulttimeout` im Code; feedparser-Calls mit explizitem Timeout |
| Redundante Health-Checks | Phase 1: Firmware-Stabilitaet | Ein Timer, ein Restart-Log-Eintrag |
| LED blinkt bei kurzen Reconnects | Phase 1: Status-LED-Verhalten | 3-Sek-Reconnect-Test: LED bleibt ruhig |
| .env in .gitignore fehlt | Phase 2 (sofort) | `git status` zeigt .env als untracked, nicht als modified |

---

## Sources

- ESP32-S3 NeoPixel/WiFi crash: [GitHub Issue #429](https://github.com/adafruit/Adafruit_NeoPixel/issues/429)
- ESP32 Core 3.x NeoPixel regression: [Arduino Forum](https://forum.arduino.cc/t/neopixel-crash-with-75-pixels-using-esp32-core-3-0-x/1273500)
- ESP32 NeoPixel timing issue (1ms interrupt): [GitHub Issue #139](https://github.com/adafruit/Adafruit_NeoPixel/issues/139)
- ESP32 NeoPixel crash mit Arduino-esp32: [GitHub Issue #9903](https://github.com/espressif/arduino-esp32/issues/9903)
- LittleFS und Interrupt-Konflikte: [GitHub Issue #8802](https://github.com/espressif/arduino-esp32/issues/8802)
- ESP32 Heap-Fragmentierung: [Hubble Community Guide](https://hubble.com/community/guides/esp32-memory-fragmentation-why-your-device-crashes-after-running-for-days/)
- FreeRTOS Watchdog und Task-Priority: [ESP32 Forum](https://esp32.com/viewtopic.php?t=31155)
- Gunicorn pre-fork global state Problem: [Medium](https://medium.com/@jgleeee/sharing-data-across-workers-in-a-gunicorn-flask-application-2ad698591875)
- Gunicorn Background Thread Zuverlaessigkeit: [Gunicorn Issue #2800](https://github.com/benoitc/gunicorn/issues/2800)
- Flask Background Worker Best Practices: [Miguel Grinberg Blog](https://blog.miguelgrinberg.com/post/the-flask-mega-tutorial-part-xxii-background-jobs)
- min_spiffs.csv Partition-Schema: `/Users/simonluthe/.platformio/packages/framework-arduinoespressif32/tools/partitions/min_spiffs.csv` (direkt analysiert)
- esp_task_wdt_init Breaking Change: esp-idf v5.x CHANGELOG, Arduino-ESP32 Core 3.x Migration Guide
- ESP32-targz tarGzExpanderNoTempFile() Heap-Anforderungen: Direkte Code-Analyse, ESP32-targz README
- Direkte Code-Analyse: `firmware/src/moodlight.cpp` Zeilen 1312-1530 (Upload-Handler), 2910-2977 (Firmware-Update-Handler)
- Direkte Code-Analyse: `build-release.sh` (Build-Script-Logik)
- Direkte Code-Analyse: `.planning/codebase/CONCERNS.md` (2026-03-25)
- Direkte Code-Analyse: `.planning/codebase/ARCHITECTURE.md` (2026-03-25)

---
*Pitfalls research for: ESP32 IoT Firmware Stabilization + Flask Backend Hardening + Combined OTA Update*
*Researched: 2026-03-25 (Phase 1/2), 2026-03-26 (Combined OTA)*
