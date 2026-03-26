# Phase 4: Combined-Update-Handler - Research

**Researched:** 2026-03-26
**Domain:** ESP32 OTA via HTTP upload — ESP32-targz streaming API + Arduino WebServer upload pattern
**Confidence:** HIGH — alle Kernbefunde direkt aus lokaler Library-Quelle verifiziert

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

Infrastructure phase — alle technischen Entscheidungen liegen bei Claude. Key constraints aus Research:

- **128KB LittleFS-Limit**: firmware.bin (1.2MB) kann NICHT auf LittleFS gespeichert werden. Streaming via `tarGzStreamUpdater` ist die einzige Option.
- **Two-Route-Architektur**: Dieselbe Datei wird zweimal hochgeladen — einmal für UI (tarGzStreamExpander mit bin-exclude), einmal für Firmware (tarGzStreamUpdater erkennt .ino.bin).
- **ESP32-targz API**: `tarGzStreamExpander` für UI-Extraktion (exclude *.bin), `tarGzStreamUpdater` für Firmware-Flash (erkennt .ino.bin Endung automatisch).
- **HAS_OTA_SUPPORT**: Prüfen ob dieses Define in platformio.ini gesetzt werden muss damit tarGzStreamUpdater verfügbar ist.
- **State-Reset**: Alle static-Variablen im Upload-Handler müssen bei UPLOAD_FILE_START zurückgesetzt werden.
- **Bestehende Handler beibehalten**: Die alten `/update/ui` und `/update/firmware` Endpoints als Fallback behalten.

### Claude's Discretion

Alle technischen Implementierungsdetails.

### Deferred Ideas (OUT OF SCOPE)

Keine.

</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| OTA-01 | Neuer HTTP-Endpoint `/update/combined-ui` — extrahiert UI-Dateien aus Combined-TGZ, überspringt `*.bin` | `tarGzStreamExpander` + `setTarExcludeFilter` — verifiziert in LibUnpacker.cpp:548-596 |
| OTA-02 | Neuer HTTP-Endpoint `/update/combined-firmware` — streamt `firmware.ino.bin` direkt via `Update.write()` ohne LittleFS-Staging | `tarGzStreamUpdater` — verifiziert in LibUnpacker.cpp:2152-2203; erkennt `.ino.bin` automatisch über `tarHeaderUpdateCallBack` |
| OTA-03 | Version aus `VERSION.txt` im TGZ lesen und in `firmware-version.txt` + `ui-version.txt` speichern | `tarGzStreamExpander` extrahiert alle Dateien inkl. `VERSION.txt` nach LittleFS; danach `LittleFS.open("/VERSION.txt")` lesen; build-release.sh muss `VERSION.txt` ins Archiv aufnehmen |
| OTA-04 | Sauberer State-Reset bei Upload-Abbruch — keine stale static-Variablen | Static-Variablen werden bei `UPLOAD_FILE_START` explizit zurückgesetzt; `UPLOAD_FILE_ABORTED`-Status behandeln |

</phase_requirements>

---

## Summary

Phase 4 implementiert zwei neue HTTP-Upload-Endpoints für das Combined-TGZ (`AuraOS-X.Y.tgz`), das UI-Dateien und `firmware.ino.bin` enthält. Die zentrale Herausforderung ist, dass `tarGzStreamExpander` und `tarGzStreamUpdater` beide ein `Stream*`-Objekt erwarten, der Arduino-WebServer aber einen Push-basierten Callback-Upload liefert. Die Lösung ist ein **ChunkStream-Wrapper** — eine `Stream`-Subklasse die Upload-Chunks via `feed()` entgegennimmt und `available()` + `readBytes()` für die Library bereitstellt. Die Library wartet intern mit `vTaskDelay(1)` auf neue Daten (Timeout 10 Sekunden), was das Push-Pull-Mismatch auflöst.

`HAS_OTA_SUPPORT` muss **nicht** in `platformio.ini` gesetzt werden — die Library definiert es automatisch bei `#ifdef ESP32` (verifiziert in `ESP32-targz-lib.hpp:49`). Die `tarGzStreamUpdater`-Funktion ist damit ohne weitere Konfiguration verfügbar.

`VERSION.txt` muss noch ins Build-Script (`build-release.sh`) aufgenommen werden — aktuell fehlt sie im Archiv. Der UI-Handler extrahiert sie nach LittleFS, liest sie aus und schreibt den Inhalt in `/firmware-version.txt` und `/ui-version.txt`.

**Primary recommendation:** ChunkStream-Wrapper implementieren, beide Routen mit `tarGzStreamExpander` (UI) bzw. `tarGzStreamUpdater` (Firmware) aufbauen, `VERSION.txt` ins Archiv aufnehmen.

---

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| ESP32-targz | 1.2.7 (installiert) | TGZ-Streaming-Extraktion + OTA-Flash | Bereits im Projekt; `tarGzStreamUpdater` ist purpose-built für genau diesen Use-Case |
| Arduino WebServer (ESP8266WebServer) | built-in | HTTP-Upload-Handling | Bereits im Projekt; einzige unterstützte Upload-Pattern |
| Arduino Update API | built-in | OTA-Flash | Wird intern von `tarGzStreamUpdater` verwendet |
| LittleFS | built-in | Filesystem | Bereits konfiguriert |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Custom ChunkStream | neu zu implementieren | Stream-Adapter zwischen Push-Upload und Pull-Library | Immer — ohne ihn funktioniert `tarGzStreamExpander`/`tarGzStreamUpdater` nicht mit WebServer-Upload |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| ChunkStream-Wrapper | Datei auf LittleFS zwischenspeichern + tarGzExpanderNoTempFile | Für UI-only möglich (UI-TGZ ~15-35KB passt auf 128KB LittleFS), aber firmware.bin (~1.2MB) passt definitiv nicht — kein universeller Ansatz |
| tarGzStreamUpdater | Direktes Update.write() mit manuellem TAR-Header-Parser | ~200 Zeilen C++ nur für Firmware-Parsing; tarGzStreamUpdater erledigt das mit 5 Zeilen |

**Installation:** Keine neuen Dependencies. `ESP32-targz@^1.2.7` bereits in `platformio.ini`.

---

## Architecture Patterns

### ChunkStream-Wrapper (kritische Komponente)

Der Arduino-WebServer liefert Upload-Daten Push-basiert: `UPLOAD_FILE_WRITE`-Callback mit `upload.buf` (uint8_t*) und `upload.currentSize`. Die ESP32-targz-Library erwartet Pull-basiert ein `Stream*`-Objekt.

**Lösung:** `ChunkStream`-Klasse die von `Stream` erbt:

```cpp
// Datei: firmware/src/ChunkStream.h (neue Datei)
class ChunkStream : public Stream {
public:
    // Feed-Methode: wird in UPLOAD_FILE_WRITE aufgerufen
    void feed(const uint8_t* data, size_t len) {
        // Daten in internen Ring-/Linear-Buffer kopieren
    }

    // Stream-Interface (von Library abgefragt)
    int available() override { return _bufLen; }
    int read() override { /* ein Byte aus Buffer */ }
    size_t readBytes(char* buf, size_t len) override { /* N Bytes aus Buffer */ }
    int peek() override { /* erstes Byte ohne Entfernen */ }
    size_t write(uint8_t) override { return 0; } // nicht benötigt

    void reset() { _bufLen = 0; _readPos = 0; _writePos = 0; }
};
```

**Warum kein Blocking-Problem:** Die Library prüft `stream->available()` und wartet mit `vTaskDelay(1)` in einer Timeout-Schleife (10 Sekunden, `targz_read_timeout`). Da der WebServer-Task und die Library im gleichen Task laufen, aber der `UPLOAD_FILE_WRITE`-Callback durch den Upload-Fortschritt getriggert wird, muss der Buffer so implementiert werden, dass `tarGzStreamExpander`/`tarGzStreamUpdater` beim ersten `UPLOAD_FILE_WRITE`-Call gestartet werden — entweder via FreeRTOS-Task oder durch Puffern aller Chunks und Stream-Start bei `UPLOAD_FILE_END`.

**WICHTIG: Architektur-Entscheidung** (siehe Pitfall ARCH-1 unten): Die Library läuft synchron und blockiert. Sie kann nicht während des Upload-Callbacks aufgerufen werden — sie wird nach dem letzten Chunk (`UPLOAD_FILE_END`) aufgerufen, was bedeutet die Chunks müssen **alle zwischengespeichert** werden.

Für den UI-Pass ist das LittleFS-Speichern (alter Ansatz: `tarGzExpanderNoTempFile`) nur für UI-TGZ (~15-35KB) machbar. Für den kombinierten TGZ (~1.2MB) ist es nicht machbar. Daher:

- **UI-Endpoint `/update/combined-ui`**: Kombiniertes TGZ ist ~1.2MB — passt nicht auf LittleFS. Die Library muss während des Uploads streamen. Einzige Lösung: ChunkStream mit PSRAM-Backup-Buffer ODER FreeRTOS-Task der Library parallel zum Upload laufen lässt.
- **Firmware-Endpoint `/update/combined-firmware`**: Gleiche Größe, gleiche Einschränkung.

**Praktische Architektur (KEINE PSRAM auf Standard-ESP32-DevKit):**

Alternativ-Ansatz der ohne PSRAM auskommt: Die Chunks werden in einem Heap-Ring-Buffer gehalten, die Library wird in einem separaten FreeRTOS-Task gestartet der synchron Chunks verbraucht während der Upload-Task sie liefert. Oder: der Upload wird in Blöcken an die Library weitergegeben mit `yield()`-Calls.

Da der aktuelle Code keinen FreeRTOS-Task für den Upload nutzt, ist der sicherste Ansatz:

**Empfohlener Ansatz: Chunk-Buffer im Heap (max. erlaubte Upload-Größe prüfen)**

Die Library-Methoden `tarGzStreamExpander` und `tarGzStreamUpdater` können auch nach dem Upload aufgerufen werden, wenn alle Daten in einem Heap-Objekt (oder einer temporären Datei) liegen. Für den UI-Pass: Da UI-Dateien ohne firmware.ino.bin < 35KB sind und der Filter *.bin überspringt, ist das kombinierte TGZ trotzdem ~1.2MB wegen firmware.ino.bin. Also: temporäre Datei nicht möglich.

**Finale Entscheidung: FreeRTOS-Task + Blocking ChunkStream mit Mutex/Semaphore**

```
Upload-Task (WebServer-Task):
  UPLOAD_FILE_START: ChunkStream.reset(), startet Library-Task (xTaskCreate)
  UPLOAD_FILE_WRITE: ChunkStream.feed(buf, size), signalisiert Semaphore
  UPLOAD_FILE_END:   signalisiert EOF, wartet auf Library-Task Completion

Library-Task (separater FreeRTOS-Task, Stack ~8KB):
  tarGzStreamExpander(&chunkStream, LittleFS, "/")
  → liest von ChunkStream; wenn available()==0: wartet auf Semaphore (max. 10s)
  Task beendet sich, signalisiert Completion-Semaphore
```

### Recommended Project Structure

```
firmware/src/
├── moodlight.cpp          # Haupt-App — neue Routen hier registrieren (~Zeile 2077)
├── ChunkStream.h          # Neue Datei: Stream-Adapter fuer WebServer-Upload
├── MoodlightUtils.h/.cpp  # Unverändert
└── config.h               # Unverändert (Version-String)
```

### Pattern 1: Zwei-Routen-Upload mit gleichem TGZ

```
Browser:
  File: AuraOS-10.0.tgz (einmal ausgewählt)
  Request 1: POST /update/combined-ui    → tarGzStreamExpander + exclude *.bin
  Request 2: POST /update/combined-firmware → tarGzStreamUpdater → ESP.restart()

ESP32:
  /update/combined-ui:
    - Extrahiert UI-Dateien nach /  (index.html, setup.html, etc.)
    - Extrahiert VERSION.txt nach /
    - Liest /VERSION.txt, schreibt in /ui-version.txt + /firmware-version.txt
    - Löscht /VERSION.txt + /firmware.ino.bin (falls irrtümlich extrahiert via Filter)
    - Kein Restart
  /update/combined-firmware:
    - tarGzStreamUpdater streamt firmware.ino.bin → U_FLASH via Update.write()
    - KEIN LittleFS-Zugriff
    - Update.end(true) + ESP.restart()
```

### Pattern 2: Exclude-Filter für UI-Pass

```cpp
// Source: verifiziert in LibUnpacker.cpp:548-596
TarGzUnpacker *unpacker = new TarGzUnpacker();
unpacker->setTarVerify(false);
unpacker->setupFSCallbacks(targzTotalBytesFn, targzFreeBytesFn);
// Return true = SKIP diese Datei
unpacker->setTarExcludeFilter([](TAR::header_translated_t *h) -> bool {
    return String(h->filename).endsWith(".bin");
});
// KEIN setTarExcludeFilter für tarGzStreamUpdater — nicht nötig, der ignoriert
// Nicht-bin-Dateien automatisch in tarHeaderUpdateCallBack (return ESP32_TARGZ_OK ohne Update.begin)
unpacker->tarGzStreamExpander(&chunkStream, LittleFS, "/");
```

### Pattern 3: VERSION.txt lesen und verteilen

```cpp
// Nach erfolgreichem tarGzStreamExpander-Aufruf:
if (LittleFS.exists("/VERSION.txt")) {
    File vf = LittleFS.open("/VERSION.txt", "r");
    if (vf) {
        String version = vf.readStringUntil('\n');
        version.trim();
        vf.close();
        // In beide Version-Dateien schreiben
        File fw = LittleFS.open("/firmware-version.txt", "w");
        if (fw) { fw.print(version); fw.close(); }
        File uw = LittleFS.open("/ui-version.txt", "w");
        if (uw) { uw.print(version); uw.close(); }
        LittleFS.remove("/VERSION.txt"); // Aufräumen
    }
}
```

### Pattern 4: build-release.sh — VERSION.txt ins Archiv aufnehmen

```bash
# Vor dem tar-Befehl: VERSION.txt erzeugen
echo "${NEW_VERSION}" > /tmp/VERSION.txt
tar -rf /tmp/auraos_combined.tar -C /tmp VERSION.txt
```

### Anti-Patterns to Avoid

- **tarGzStreamUpdater mit setTarExcludeFilter aufrufen**: Der `tarGzStreamUpdater` verwendet `tarHeaderUpdateCallBack` statt `tarHeaderCallBack` — die Filter-Callbacks (`tarSkipThisEntryOut`) werden nie ausgewertet. Filter auf dem tarGzStreamUpdater-Pfad sind wirkungslos.
- **tarGzStreamExpander direkt in UPLOAD_FILE_WRITE aufrufen**: Die Library blockiert synchron. Sie muss entweder in einem separaten FreeRTOS-Task oder nach Abschluss des Uploads laufen.
- **static-Variablen nicht zurücksetzen**: Bei `UPLOAD_FILE_START` müssen alle static-Variablen explizit auf Initialwert gesetzt werden.
- **Firmware vor UI flashen wenn beide aus gleichem Upload**: Bei Firmware-Route kein LittleFS-Schreiben nötig — kein Konflikt. Aber bei UI-Route: VERSION.txt muss VOR dem Firmware-Flash gelesen werden, da nach `ESP.restart()` kein LittleFS-Schreiben mehr möglich ist.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| TGZ-Streaming → OTA-Flash | Eigenen TAR-Header-Parser + Update.write()-Loop | `tarGzStreamUpdater` | Implementiert TAR-Header-Parsing, `.ino.bin`-Erkennung, `Update.begin/write/end`-Lifecycle — ~400 LOC Library-Code statt selbst schreiben |
| TGZ-Streaming → LittleFS | Datei-by-Datei-Extract-Loop | `tarGzStreamExpander` | Puffer-Management, GZ-Dictionary, Fehlerbehandlung bereits gelöst |
| Firmware-Version aus Dateiname parsen | String-Parsing mit `substring()`/`indexOf()` | `VERSION.txt` im Archiv | Dateiname-Parsing ist fragil (Pitfall OTA-3); maschinenlesbares Manifest ist robust |

**Key insight:** Firmwareflash-Details (Partition-Größencheck, CRC-Validation, OTA-Slot-Switching) sind nicht trivial — das `Update`-API plus `tarGzStreamUpdater` nehmen das vollständig ab.

---

## HAS_OTA_SUPPORT: Verifiziertes Ergebnis

**Frage:** Muss `HAS_OTA_SUPPORT` in `platformio.ini` gesetzt werden?

**Antwort: NEIN.**

In `firmware/.pio/libdeps/esp32dev/ESP32-targz/src/ESP32-targz-lib.hpp`, Zeile 49:

```cpp
#if defined ESP32
  #include <Update.h>
  #define HAS_OTA_SUPPORT  // ← automatisch definiert für alle ESP32-Targets
```

`tarGzStreamUpdater` ist damit auf ESP32 ohne zusätzliche Build-Flags verfügbar. `platformio.ini` benötigt keine Änderung für dieses Feature.

---

## tarGzStreamUpdater — Behavior bei Nicht-Bin-Einträgen

Verifiziert in `LibUnpacker.cpp:784-847` (`tarHeaderUpdateCallBack`):

```cpp
if( String( header->filename ).endsWith("ino.bin") ) {
    partition = U_FLASH;
} else if( /* spiffs.bin / littlefs.bin / ffat.bin */ ) {
    partition = U_PART;
} else {
    // nicht relevant — kein Update.begin(), kein Schreiben
    return ESP32_TARGZ_OK;  // ← silent skip
}
```

`VERSION.txt`, `index.html`, `css/`, `js/` werden von `tarGzStreamUpdater` **komplett ignoriert** — sie werden weder nach LittleFS noch nach Flash geschrieben. `tarStreamWriteUpdateCallback` prüft `tarBlockIsUpdateData` (gesetzt nur nach `.ino.bin`-Header) und kehrt bei `false` sofort mit `ESP32_TARGZ_OK` zurück.

**Konsequenz:** Kein Exclude-Filter auf dem Firmware-Endpoint nötig. Der Filter auf dem UI-Endpoint filtert `.bin`-Dateien heraus, damit `firmware.ino.bin` nicht nach LittleFS geschrieben wird (1.2MB > 128KB LittleFS).

---

## UPLOAD_FILE_ABORTED — State-Reset bei Abbruch (OTA-04)

Der Arduino-WebServer sendet `UPLOAD_FILE_ABORTED`-Status wenn der Client die Verbindung unterbricht. Der Handler muss explizit darauf reagieren:

```cpp
else if (upload.status == UPLOAD_FILE_ABORTED) {
    // Static-Variablen zurücksetzen
    isValidFile = false;
    extractedVersion = "";
    // ChunkStream reset
    if (chunkStream) chunkStream->reset();
    // Laufende Library-Task abbrechen (falls FreeRTOS-Task läuft)
    // ...
    debug(F("Upload abgebrochen — State zurückgesetzt"));
}
```

Und bei `UPLOAD_FILE_START` müssen alle static-Variablen explizit zurückgesetzt werden:

```cpp
if (upload.status == UPLOAD_FILE_START) {
    // Reset aller static-Variablen (OTA-04)
    isValidFile = false;
    extractedVersion = "";
    updateError = false;
    // ...
}
```

---

## build-release.sh — Fehlende VERSION.txt

**Aktueller Stand:** `build-release.sh` packt `index.html`, `setup.html`, `mood.html`, `diagnostics.html`, `js/`, `css/`, und `firmware.ino.bin` ins Archiv. `VERSION.txt` fehlt.

**Nötige Änderung (minimal):**

```bash
# Nach Zeile 94 (gzip -c /tmp/auraos_combined.tar > ...) einfügen:
# VERSION.txt für ESP32-Handler erzeugen und ins Archiv aufnehmen
echo "${NEW_VERSION}" > /tmp/VERSION.txt
# Neues TAR aus existierendem + VERSION.txt (muss vor gzip passieren)
```

Da das bestehende Build-Script bereits `gzip`-komprimiert und dann weitergeht, muss `VERSION.txt` **vor** dem `gzip`-Schritt ins unkomprimierte TAR:

```bash
# Nach "Schritt 2: firmware.bin als firmware.ino.bin anhängen":
echo "${NEW_VERSION}" > /tmp/VERSION.txt
tar -rf /tmp/auraos_combined.tar -C /tmp VERSION.txt
# Dann Schritt 3: gzip
```

Ebenfalls muss die TGZ-Verifikation (Zeile 99-108) einen Check für `VERSION.txt` aufnehmen:

```bash
if ! echo "$TAR_CONTENTS" | grep -q "VERSION.txt"; then
    echo "FEHLER: VERSION.txt fehlt im TGZ"
    exit 1
fi
```

---

## Common Pitfalls

### Pitfall ARCH-1: tarGzStreamExpander/Updater blockiert synchron — kein Aufruf während UPLOAD_FILE_WRITE

**What goes wrong:** Wenn man versucht `tarGzStreamExpander` direkt in `UPLOAD_FILE_WRITE` aufzurufen (um die Daten "live" zu verarbeiten), blockiert die Library den aktuellen Task. Der WebServer-Task bekommt keine weiteren Chunks und die Library wartet auf `stream->available()` — Deadlock.

**Why it happens:** Die Library ist eine synchrone, blocking-Implementierung. Kein async-API.

**How to avoid:** Library entweder (a) nach `UPLOAD_FILE_END` aufrufen wenn alle Daten zwischengespeichert sind, oder (b) in einem separaten FreeRTOS-Task mit synchronisiertem ChunkStream-Buffer.

**Für dieses Projekt:** Option (b) ist nötig da das TGZ ~1.2MB groß ist und nicht auf LittleFS passt.

**Warning signs:** WebServer hängt nach erstem `UPLOAD_FILE_WRITE` beim Combined-Upload; Timeout nach ~30s; Watchdog-Reset.

---

### Pitfall ARCH-2: tarGzStreamUpdater + setTarExcludeFilter funktioniert nicht

**What goes wrong:** `setTarExcludeFilter` wird auf dem Unpacker gesetzt, aber `tarGzStreamUpdater` ignoriert den Filter komplett — weil es `tarHeaderUpdateCallBack` statt `tarHeaderCallBack` nutzt. Der Filter-Check (`tarSkipThisEntryOut`) steht nur in `tarHeaderCallBack`.

**Why it happens:** Zwei verschiedene Callback-Tabellen in `tarGzStreamExpander` vs. `tarGzStreamUpdater` (verifiziert in `LibUnpacker.cpp:2258-2263` vs. `2167-2172`).

**How to avoid:** Kein Exclude-Filter auf dem Firmware-Endpoint setzen — unnötig, da `tarGzStreamUpdater` Nicht-Bin-Dateien sowieso ignoriert. Exclude-Filter nur auf UI-Endpoint.

---

### Pitfall OTA-3: VERSION.txt muss explizit ins Archiv — aktuell fehlt sie

**What goes wrong:** Handler versucht `/VERSION.txt` zu lesen — Datei existiert nicht auf LittleFS nach Extraktion, weil sie nicht im Archiv war. `extractedVersion` bleibt leer, `/firmware-version.txt` und `/ui-version.txt` werden nicht aktualisiert.

**How to avoid:** `build-release.sh` muss `/tmp/VERSION.txt` erzeugen und vor dem gzip-Schritt ins TAR aufnehmen.

---

### Pitfall OTA-4: Static-State nach Abbruch

**What goes wrong:** Upload bricht ab, `UPLOAD_FILE_END` wird nie aufgerufen. Static-Variablen (`isValidFile = true`, alter `uploadPath`) bleiben vom abgebrochenen Upload. Nächster Upload-Versuch startet in inkonsistentem State.

**How to avoid:** `UPLOAD_FILE_START` setzt alle static-Variablen explizit zurück. `UPLOAD_FILE_ABORTED` behandeln.

---

### Pitfall LEGACY-1: Bestehender handleUiUpload() ist nicht kompatibel mit 1.2MB Combined-TGZ

**What goes wrong:** `handleUiUpload()` (Zeilen 1312-1531) schreibt das TGZ nach `/temp/<filename>.tgz` auf LittleFS. Mit Combined-TGZ (~1.2MB) schlägt `LittleFS.open(uploadPath, "a")` ab ca. 50-60KB geschriebener Daten fehl — LittleFS voll. Der Handler merkt es (prüft `bytesWritten != upload.currentSize`) aber bricht den Upload nicht ab, sondern loggt nur.

**How to avoid:** Für Combined-TGZ-Routen NIEMALS in LittleFS zwischenspeichern. Immer Streaming-Ansatz (FreeRTOS-Task + ChunkStream).

---

## Code Examples

### Beispiel-Routen-Registrierung

```cpp
// Source: verifiziert in moodlight.cpp:2071-2076 (bestehende Pattern)
static bool combinedUiError = false;
static bool combinedFwError = false;

server.on("/update/combined-ui", HTTP_POST,
    []() {
        // Response-Lambda (läuft nach Upload)
        server.sendHeader("Connection", "close");
        if (combinedUiError) {
            server.send(500, "text/html",
                "<html><body><h1>UI-Update fehlgeschlagen</h1>"
                "<a href='/diagnostics.html'>Zurueck</a></body></html>");
        } else {
            server.send(200, "text/html",
                "<html><body><h1>UI-Update erfolgreich</h1>"
                "<a href='/diagnostics.html'>Zurueck</a></body></html>");
        }
    },
    handleCombinedUiUpload   // Upload-Callback-Funktion
);

server.on("/update/combined-firmware", HTTP_POST,
    []() {
        server.sendHeader("Connection", "close");
        if (combinedFwError) {
            server.send(500, "text/html", "Firmware-Update fehlgeschlagen");
        } else {
            server.send(200, "text/html",
                "<html><body><h1>Firmware-Update erfolgreich</h1>"
                "<p>Geraet wird neu gestartet...</p>"
                "<script>setTimeout(()=>{window.location.href='/';},12000);</script>"
                "</body></html>");
            delay(1000);
            ESP.restart();
        }
    },
    handleCombinedFirmwareUpload
);
```

### Beispiel-Handler-Skeleton (UI)

```cpp
// Vor setupWebServer() — definiert als freie Funktion wie handleUiUpload()
void handleCombinedUiUpload() {
    HTTPUpload& upload = server.upload();
    static bool isValidFile = false;
    static bool taskStarted = false;
    // ChunkStream-Instanz (static oder global — muss über Callbacks persistent sein)
    static ChunkStream* stream = nullptr;
    static TaskHandle_t extractTask = nullptr;

    if (upload.status == UPLOAD_FILE_START) {
        // OTA-04: State reset
        isValidFile = false;
        taskStarted = false;
        combinedUiError = false;
        if (stream) { stream->reset(); }
        else { stream = new ChunkStream(8192); } // 8KB Ring-Buffer

        String filename = upload.filename;
        isValidFile = filename.endsWith(".tgz") || filename.endsWith(".tar.gz");
        if (!isValidFile) return;

        // FreeRTOS-Task starten der auf ChunkStream wartet
        // xTaskCreate(uiExtractTask, "uiExtract", 8192, (void*)stream, 1, &extractTask);
    }
    else if (upload.status == UPLOAD_FILE_WRITE && isValidFile) {
        if (stream) stream->feed(upload.buf, upload.currentSize);
        // Task läuft parallel und verbraucht Daten aus stream
    }
    else if (upload.status == UPLOAD_FILE_END && isValidFile) {
        if (stream) stream->setEOF(); // signalisiert Ende des Streams
        // Warten bis Task fertig (oder Timeout)
        // ...
    }
    else if (upload.status == UPLOAD_FILE_ABORTED) {
        // OTA-04: Abbruch
        isValidFile = false;
        if (stream) stream->reset();
        if (extractTask) { vTaskDelete(extractTask); extractTask = nullptr; }
        combinedUiError = true;
    }
}
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| tarGzExpanderNoTempFile() (read from LittleFS file) | tarGzStreamExpander() (read from Stream) | Phase 4 | Kein LittleFS-Staging nötig — löst 128KB-Limit |
| Version aus Dateiname parsen | VERSION.txt im Archiv | Phase 4 | Robustheit; kein fragiles String-Parsing |
| Zwei separate Dateien (UI-TGZ + Firmware-BIN) | Eine Combined-TGZ | Phase 3 (Build) / Phase 4 (Handler) | Einheitlicher Upload-Workflow |

---

## Open Questions

1. **ChunkStream-Buffer-Größe und FreeRTOS-Stack**
   - Was wir wissen: Library benötigt ~40KB RAM während Extraktion; FreeRTOS-Stack für Task typisch 4-8KB
   - Was unklar ist: Optimale Ring-Buffer-Größe; ob `vTaskDelay(1)` im Library-Core ausreicht oder ob explizite Semaphore nötig ist
   - Recommendation: Mit 4KB Ring-Buffer starten; Library-Timeout von 10s (`targz_read_timeout`) gibt genug Puffer; falls Instabilität: auf 8KB erhöhen

2. **FreeRTOS-Task-Priority und WebServer-Task**
   - Was wir wissen: ESP32 Arduino-WebServer läuft auf Core 1; FreeRTOS-Tasks können auf Core 0 oder 1 laufen
   - Was unklar ist: Ob Extract-Task auf anderem Core als WebServer-Task laufen muss um Deadlock zu vermeiden
   - Recommendation: `xTaskCreatePinnedToCore(..., 0)` für Extract-Task auf Core 0; WebServer auf Core 1 — vermeidet Scheduling-Konflikt

3. **Watchdog-Timeout während Firmware-Flash**
   - Was wir wissen: `tarGzStreamUpdater` kann ~30-60s brauchen für 1.2MB; WDT timeout ist 30s (nach FIX-01)
   - Was unklar ist: Ob `tarGzStreamUpdater` intern `vTaskDelay`/`yield` aufruft um WDT zu resetten
   - Recommendation: Im Firmware-Handler vor `tarGzStreamUpdater`-Aufruf `esp_task_wdt_delete(NULL)` für den aktuellen Task aufrufen; nach Abschluss ggf. wieder registrieren

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| ESP32-targz | OTA-01, OTA-02 | ja | 1.2.7 (in .pio/libdeps) | — |
| LittleFS | OTA-01, OTA-03 | ja | built-in Arduino ESP32 | — |
| Arduino Update API | OTA-02 | ja | built-in (HAS_OTA_SUPPORT auto-gesetzt) | — |
| PlatformIO / pio | Build | ja | (Projekt baut nach FIX-01) | — |

---

## Sources

### Primary (HIGH confidence)
- `firmware/.pio/libdeps/esp32dev/ESP32-targz/src/ESP32-targz-lib.hpp` — HAS_OTA_SUPPORT auto-define bei ESP32 (Zeile 49)
- `firmware/.pio/libdeps/esp32dev/ESP32-targz/src/libunpacker/LibUnpacker.hpp` — API-Signaturen: `tarGzStreamExpander`, `tarGzStreamUpdater`, `setTarExcludeFilter`
- `firmware/.pio/libdeps/esp32dev/ESP32-targz/src/libunpacker/LibUnpacker.cpp:548-596` — Exclude-Filter-Implementierung (`tarSkipThisEntryOut`, `tarSkipThisEntry`)
- `firmware/.pio/libdeps/esp32dev/ESP32-targz/src/libunpacker/LibUnpacker.cpp:782-905` — `tarHeaderUpdateCallBack` und `tarStreamWriteUpdateCallback` — `.ino.bin`-Erkennung, Silent-Skip bei Nicht-Bin-Dateien
- `firmware/.pio/libdeps/esp32dev/ESP32-targz/src/libunpacker/LibUnpacker.cpp:1183-1193` — `vTaskDelay(1)` Polling-Loop mit `targz_read_timeout=10000ms`
- `firmware/.pio/libdeps/esp32dev/ESP32-targz/src/libunpacker/LibUnpacker.cpp:2150-2205` — `tarGzStreamUpdater`-Implementierung
- `firmware/.pio/libdeps/esp32dev/ESP32-targz/src/libunpacker/LibUnpacker.cpp:2208-2309` — `tarGzStreamExpander`-Implementierung
- `firmware/src/moodlight.cpp:1312-1531` — bestehender `handleUiUpload()` als Referenz-Pattern
- `firmware/src/moodlight.cpp:2915-2978` — bestehender Firmware-Update-Handler als Referenz-Pattern
- `firmware/src/moodlight.cpp:2071-2076` — bestehende Routen-Registrierung
- `firmware/platformio.ini` — keine `HAS_OTA_SUPPORT` Build-Flag nötig bestätigt
- `build-release.sh` — fehlende VERSION.txt identifiziert

### Secondary (MEDIUM confidence)
- `firmware/.pio/libdeps/esp32dev/ESP32-targz/examples/ESP32/Update_from_gz_stream/Update_from_gz_stream.ino` — tarGzStreamUpdater usage pattern mit `Stream*` von HTTPClient

---

## Metadata

**Confidence breakdown:**
- HAS_OTA_SUPPORT: HIGH — direkt aus Quellcode verifiziert, kein platformio.ini-Change nötig
- tarGzStreamUpdater API: HIGH — Quelleode vollständig gelesen inkl. Callback-Implementierungen
- Exclude-Filter: HIGH — sowohl positive Wirkung (tarGzStreamExpander) als auch fehlende Wirkung (tarGzStreamUpdater) aus Quellcode bewiesen
- VERSION.txt-Handling: HIGH — fehlt in build-release.sh (verifiziert), Lese-Pattern klar
- ChunkStream-Architektur: MEDIUM — FreeRTOS-Task-Ansatz ist korrekte Lösung aber konkrete Implementierungsdetails (Buffer-Größe, Semaphore vs. vTaskDelay) müssen beim Implementieren validiert werden
- State-Reset (OTA-04): HIGH — Pattern aus bestehendem Code klar, UPLOAD_FILE_ABORTED-Handling fehlt in aktuellem handleUiUpload

**Research date:** 2026-03-26
**Valid until:** 2026-09-26 (Library v1.2.7 stabil; ESP32-Arduino-Core-Änderungen möglich aber unwahrscheinlich kurzfristig)
