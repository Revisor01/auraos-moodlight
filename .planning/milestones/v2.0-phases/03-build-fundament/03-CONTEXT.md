# Phase 3: Build-Fundament - Context

**Gathered:** 2026-03-26
**Status:** Ready for planning
**Mode:** Auto-generated (infrastructure phase)

<domain>
## Phase Boundary

Das Projekt baut fehlerfrei und `./build-release.sh minor` erzeugt ein versioniertes Combined-TGZ. Fünf Requirements:

1. **FIX-01**: `esp_task_wdt_init(30, false)` kompatibel mit Arduino ESP32 Core 3.x machen — `pio run` muss fehlerfrei bauen
2. **BUILD-01**: Build-Script akzeptiert `major`/`minor`/`patch` und bumpt Version in config.h automatisch
3. **BUILD-02**: Build-Script erzeugt `AuraOS-X.X.tgz` mit UI-Dateien + `firmware.ino.bin`
4. **BUILD-03**: Build-Script committed nach erfolgreichem Build (nicht bei Fehler)
5. **BUILD-04**: Build-Script bricht bei `pio run` Fehler ab — kein `|| true`

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
All implementation choices are at Claude's discretion — infrastructure phase. Key constraints from research:

- **esp_task_wdt_init Fix**: Arduino ESP32 Core 3.x hat die API geändert. Prüfe ob `esp_task_wdt_reconfigure()` oder `esp_task_wdt_init()` mit geänderten Parametern nötig ist. Eventuell reicht ein `#include` oder Conditional Compile.
- **Combined-TGZ Format**: UI-Dateien + `firmware.ino.bin` (mit `.ino.bin` Endung, da ESP32-targz `tarGzStreamUpdater` diese Konvention erwartet). Optional `VERSION.txt` im Root.
- **Build-Script**: macOS-kompatibel (kein GNU tar `--transform`). Zwei-Schritt: erst tar erstellen, dann gzip. `sed -i ''` für macOS.
- **Version-Format**: Semantic Versioning X.Y aus config.h. `MOODLIGHT_VERSION "9.0"` → nach `minor` Bump: `"9.1"`.
- **Commit nur nach Build-Erfolg**: Version in config.h bumpen, bauen, bei Erfolg committen. Bei Fehler: Version zurücksetzen oder gar nicht erst committen.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `build-release.sh` — bestehend, wird komplett überarbeitet
- `firmware/src/config.h` Zeile 5: `#define MOODLIGHT_VERSION "9.0"` — Version-Quelle
- `firmware/data/` — UI-Dateien für TGZ (setup.html, mood.html, index.html, diagnostics.html, js/, css/)

### Established Patterns
- Version wird per grep aus config.h gelesen: `grep '^#define MOODLIGHT_VERSION' firmware/src/config.h | cut -d'"' -f2`
- TGZ-Erstellung mit tar in firmware/data/ directory
- PlatformIO Build: `cd firmware && pio run`

### Integration Points
- `firmware/src/config.h` Zeile 5 — MOODLIGHT_VERSION Define
- `firmware/src/moodlight.cpp` Zeile ~4039 — `esp_task_wdt_init(30, false)` Aufruf
- `firmware/.pio/build/esp32dev/firmware.bin` — Build-Output

</code_context>

<specifics>
## Specific Ideas

No specific requirements — infrastructure phase.

</specifics>

<deferred>
## Deferred Ideas

None.

</deferred>
