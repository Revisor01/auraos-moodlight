# Phase 4: Combined-Update-Handler - Context

**Gathered:** 2026-03-26
**Status:** Ready for planning
**Mode:** Auto-generated (infrastructure phase)

<domain>
## Phase Boundary

Der ESP32 verarbeitet ein Combined-TGZ vollständig: UI-Dateien nach LittleFS, firmware.ino.bin direkt in Flash. Vier Requirements:

1. **OTA-01**: Neuer HTTP-Endpoint `/update/combined-ui` — extrahiert UI-Dateien aus Combined-TGZ, überspringt *.bin
2. **OTA-02**: Neuer HTTP-Endpoint `/update/combined-firmware` — streamt firmware.ino.bin direkt via Update.write() ohne LittleFS-Staging
3. **OTA-03**: Version aus VERSION.txt im TGZ lesen und in firmware-version.txt + ui-version.txt speichern
4. **OTA-04**: Sauberer State-Reset bei Upload-Abbruch

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
Infrastructure phase — alle technischen Entscheidungen liegen bei Claude. Key constraints aus Research:

- **128KB LittleFS-Limit**: firmware.bin (1.2MB) kann NICHT auf LittleFS gespeichert werden. Streaming via `tarGzStreamUpdater` ist die einzige Option.
- **Two-Route-Architektur**: Dieselbe Datei wird zweimal hochgeladen — einmal für UI (tarGzStreamExpander mit bin-exclude), einmal für Firmware (tarGzStreamUpdater erkennt .ino.bin).
- **ESP32-targz API**: `tarGzStreamExpander` für UI-Extraktion (exclude *.bin), `tarGzStreamUpdater` für Firmware-Flash (erkennt .ino.bin Endung automatisch).
- **HAS_OTA_SUPPORT**: Prüfen ob dieses Define in platformio.ini gesetzt werden muss damit tarGzStreamUpdater verfügbar ist.
- **State-Reset**: Alle static-Variablen im Upload-Handler müssen bei UPLOAD_FILE_START zurückgesetzt werden.
- **Bestehende Handler beibehalten**: Die alten `/update/ui` und `/update/firmware` Endpoints als Fallback behalten.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `handleUiUpload()` (moodlight.cpp:1312-1520) — bestehender TGZ-Extractions-Handler, Pattern für neuen Handler
- `firmware upload handler` (moodlight.cpp:2930-2977) — bestehender OTA-Flash-Handler
- `copyFile()` — bereits vorhanden für Datei-Kopien
- `removeDirRecursive()` — sollte existieren oder erstellt werden für Cleanup
- ESP32-targz v1.2.7 bereits installiert

### Established Patterns
- `server.on(path, HTTP_POST, response_handler, upload_handler)` für File-Uploads
- TARGZUnpacker mit setupFSCallbacks, setTarExcludeFilter
- Static-Variablen in Upload-Handlern (uploadPath, isTgzFile, extractedVersion)

### Integration Points
- Web-Server Route Registration in setupWebServer()
- `/ui-version.txt` und `/firmware-version.txt` auf LittleFS
- `getCurrentUiVersion()` Funktion

</code_context>

<specifics>
## Specific Ideas

No specific requirements — infrastructure phase.

</specifics>

<deferred>
## Deferred Ideas

None.

</deferred>
