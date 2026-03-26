# Architecture Patterns

**Domain:** ESP32 Combined OTA Update Handler + Build Automation
**Researched:** 2026-03-26
**Confidence:** HIGH — based entirely on direct codebase inspection

---

## Existing Update Architecture (Baseline)

Two independent, sequential upload routes exist in `firmware/src/moodlight.cpp`:

| Route | Handler | Registered at | Response pattern |
|-------|---------|--------------|-----------------|
| `POST /ui-upload` | `handleUiUpload()` (~line 1312-1530) | ~line 2070 | inline HTML, no reboot |
| `POST /update` | inline lambda (~line 2914-2977) | ~line 2915 | inline HTML + `ESP.restart()` |

Both routes use Arduino WebServer's two-argument `server.on()` upload pattern: the first lambda sends the HTTP response, the second lambda streams the upload data. This is the only supported upload pattern with the ESP32 WebServer library and must be preserved in the new handler.

### Existing UI upload flow (handleUiUpload)

```
UPLOAD_FILE_START
  - Validate .tgz extension
  - Parse version from filename (UI-X.X-AuraOS.tgz -> "X.X")
  - Create /temp/, /extract/, /extract/css/, /extract/js/ if absent
  - Clean /temp/ and /extract/ contents
  - Open /temp/<filename>.tgz for write

UPLOAD_FILE_WRITE (repeated chunks)
  - Append chunk to /temp/<filename>.tgz

UPLOAD_FILE_END
  - TARGZUnpacker->tarGzExpanderNoTempFile(LittleFS, uploadPath, LittleFS, "/extract")
  - If success:
      copyFile() each HTML/CSS/JS from /extract/ to live root
      Write version to /ui-version.txt
  - If fail: log error code
  - LittleFS.remove(uploadPath)
  - delete TARGZUnpacker
```

### Existing firmware flash flow (inline lambda)

```
UPLOAD_FILE_START
  - Parse version from filename (Firmware-X.X-AuraOS.bin -> "X.X")
  - setStatusLED(3)
  - Update.begin(UPDATE_SIZE_UNKNOWN)

UPLOAD_FILE_WRITE
  - Update.write(upload.buf, upload.currentSize)

UPLOAD_FILE_END
  - Update.end(true)
  - Write version to /firmware-version.txt

POST response lambda
  - Send success/error HTML
  - delay(1000)
  - ESP.restart()
```

---

## Combined Update Handler Architecture

### Design Principle

The combined TGZ contains all UI files plus `firmware.bin` at the archive root. The ESP32 handler:

1. Receives the streaming upload and writes it to `/temp/combined.tgz` (identical to existing UI upload)
2. Extracts the full archive to `/extract/` using `TARGZUnpacker->tarGzExpanderNoTempFile()`
3. Installs UI files by copying from `/extract/` to live root (identical to existing UI install logic)
4. Flashes firmware by reading `/extract/firmware.bin` and piping it through `Update.begin()/write()/end()`
5. Cleans up and reboots

This approach reuses every existing code path and requires no new library knowledge.

### Memory Safety: The /extract/firmware.bin Problem

**Critical constraint:** With `min_spiffs` partition, LittleFS has approximately 1.5 MB total. After extraction, `/extract/firmware.bin` (~1 MB compiled ESP32 binary) plus the UI HTML/CSS/JS files (~200-400 KB) will likely exhaust LittleFS entirely.

**Resolution options in order of preference:**

1. **Streaming TAR callback (ideal, uncertain feasibility):** ESP32-targz supports per-entry callbacks. If the library exposes a file-write intercept that can redirect a named entry to `Update.write()` instead of LittleFS, the firmware binary never touches the filesystem. Requires investigation against the specific ESP32-targz version in use.

2. **Sequential extraction with cleanup (safe fallback):** Extract UI files first. Before extracting `firmware.bin`, delete all UI files from `/extract/` to free LittleFS space. Then extract only `firmware.bin`. Since `tarGzExpanderNoTempFile()` extracts the entire archive in one call, this would require two separate extraction passes with different file filters — which the library may not support directly.

3. **Separate combined TGZ structure (practical, recommended for initial implementation):** The combined TGZ contains the UI TGZ nested inside as a single file (`ui.tgz`), plus `firmware.bin`. First extract pass unpacks the outer TGZ (two entries: `ui.tgz` + `firmware.bin`). Both are small enough to coexist on LittleFS because the outer TGZ compression means the inner `ui.tgz` is only ~50-100 KB. Then extract `ui.tgz` in a second pass. Then flash firmware from `/extract/firmware.bin`. Clean up after each phase.

**Recommended for implementation:** Start with approach 3 (nested TGZ) as it is implementable without library internals knowledge and keeps LittleFS pressure manageable. Investigate approach 1 as an optimization.

### Processing Sequence (Implementation-Ready)

```
POST /combined-update (multipart upload)
  |
  v
UPLOAD_FILE_START
  - Validate filename: must be Combined-X.X-AuraOS.tgz
  - Parse version string from filename
  - Reset static error flag: combinedUpdateError = false
  - Create /temp/, /extract/ directories if absent
  - Clean /temp/ and /extract/
  - Open /temp/combined.tgz for write

UPLOAD_FILE_WRITE (repeated)
  - Append chunk to /temp/combined.tgz
  - (identical pattern to existing handleUiUpload)

UPLOAD_FILE_END
  Phase 1 — Extract outer archive:
    TARGZUnpacker->tarGzExpanderNoTempFile(LittleFS, "/temp/combined.tgz", LittleFS, "/extract")
    If fail: combinedUpdateError = true; return

  Phase 2 — Install UI files:
    If /extract/ui.tgz exists:
      TARGZUnpacker->tarGzExpanderNoTempFile(LittleFS, "/extract/ui.tgz", LittleFS, "/extract")
    copyFile() for each HTML/CSS/JS from /extract/ to live root
    Write version string to /ui-version.txt
    (identical to existing UI install logic)

  Phase 3 — Flash firmware:
    If /extract/firmware.bin exists:
      Update.begin(UPDATE_SIZE_UNKNOWN)
      Open /extract/firmware.bin
      Read in chunks (1 KB): Update.write(buf, bytesRead)
      Update.end(true)
      If Update.hasError(): combinedUpdateError = true
      LittleFS.remove("/extract/firmware.bin")

  Phase 4 — Cleanup:
    Remove /temp/ contents
    Remove /extract/ contents
    delete TARGZUnpacker

POST response lambda
  - server.sendHeader("Connection", "close")
  - If combinedUpdateError: send error HTML; return (no reboot)
  - Else: send success HTML with 10s redirect countdown
  - delay(1000)
  - ESP.restart()
```

**Critical ordering rule:** UI files must be installed (Phase 2) before firmware is flashed (Phase 3). `ESP.restart()` is called immediately after `Update.end()`. Reversing the order would leave the device with new firmware but old UI after reboot.

### Route Registration Pattern

```cpp
static bool combinedUpdateError = false;

server.on("/combined-update", HTTP_POST,
    []() {
        // Response handler — runs AFTER upload callback
        server.sendHeader("Connection", "close");
        if (combinedUpdateError) {
            server.send(200, "text/html",
                "<html><body><h1>Combined Update Failed!</h1>"
                "<a href='/diagnostics.html'>Return</a></body></html>");
        } else {
            server.send(200, "text/html",
                "<html><body><h1>Combined Update Successful!</h1>"
                "<p>Device restarting...</p>"
                "<script>setTimeout(function(){window.location.href='/';}, 10000);</script>"
                "</body></html>");
            delay(1000);
            ESP.restart();
        }
    },
    handleCombinedUpdate   // upload callback function
);
```

The `combinedUpdateError` flag must be reset to `false` at `UPLOAD_FILE_START`, not at declaration time, to prevent state leakage across multiple requests.

---

## Build Script Architecture

### Target Behavior

`./build-release.sh [major|minor]` is the single command that:

1. Reads current version from `firmware/src/config.h`
2. Computes bumped version
3. Writes new version back to `config.h`
4. Builds firmware via `pio run`
5. Creates combined TGZ in a staging directory
6. Writes checksums
7. Stages and commits to git

### Version Bump Logic

Current format: `#define MOODLIGHT_VERSION "9.0"` — MAJOR.MINOR, no patch component.

```bash
CURRENT=$(grep '^#define MOODLIGHT_VERSION' firmware/src/config.h | cut -d'"' -f2)
MAJOR=$(echo "$CURRENT" | cut -d'.' -f1)
MINOR=$(echo "$CURRENT" | cut -d'.' -f2)

case "${1:-none}" in
  major) NEW_VERSION="$((MAJOR + 1)).0" ;;
  minor) NEW_VERSION="${MAJOR}.$((MINOR + 1))" ;;
  *)     NEW_VERSION="$CURRENT" ;;
esac

# Write back (portable: macOS and GNU)
sed -i '' "s/#define MOODLIGHT_VERSION \"${CURRENT}\"/#define MOODLIGHT_VERSION \"${NEW_VERSION}\"/" \
    firmware/src/config.h 2>/dev/null || \
sed -i  "s/#define MOODLIGHT_VERSION \"${CURRENT}\"/#define MOODLIGHT_VERSION \"${NEW_VERSION}\"/" \
    firmware/src/config.h
```

### Combined TGZ Creation via Staging Directory

The staging approach avoids the tar-repack problem (gzip archives cannot be directly appended to):

```bash
STAGING=$(mktemp -d)

# Copy UI files into staging root
cp firmware/data/index.html firmware/data/setup.html \
   firmware/data/mood.html firmware/data/diagnostics.html "$STAGING/"
cp -r firmware/data/css firmware/data/js "$STAGING/"

# Create nested ui.tgz inside staging
(cd "$STAGING" && tar -czf ui.tgz index.html setup.html mood.html diagnostics.html css/ js/)
rm "$STAGING"/*.html
rm -rf "$STAGING"/css "$STAGING"/js

# Copy firmware binary into staging root
cp firmware/.pio/build/esp32dev/firmware.bin "$STAGING/firmware.bin"

# Pack combined TGZ from staging
tar -czf "${RELEASE_DIR}/Combined-${NEW_VERSION}-AuraOS.tgz" -C "$STAGING" .
rm -rf "$STAGING"
```

This produces an archive with two entries at root: `ui.tgz` and `firmware.bin`.

### Git Commit Step

```bash
git add firmware/src/config.h
git add "releases/v${NEW_VERSION}/"
git commit -m "Bump: Release v${NEW_VERSION}"
```

No automatic `git push` — the push remains a manual step for inspection before OTA deployment.

### Build Failure Guard

Before the commit step, verify both artifacts were actually created:

```bash
if [ ! -f "firmware/.pio/build/esp32dev/firmware.bin" ]; then
    echo "ERROR: firmware.bin not found — build failed"
    exit 1
fi

if [ ! -f "${RELEASE_DIR}/Combined-${NEW_VERSION}-AuraOS.tgz" ]; then
    echo "ERROR: Combined TGZ not created"
    exit 1
fi
```

---

## Integration Points with Existing Firmware

Three locations in `moodlight.cpp` require changes. All other existing code is untouched.

| Integration Point | Location | Change |
|-------------------|----------|--------|
| New upload handler function | After `handleUiUpload()` (~line 1530) | Add `handleCombinedUpdate()` function |
| Route registration | After existing `/ui-upload` route (~line 2075) | Add `server.on("/combined-update", ...)` |
| Version display (optional) | `handleApiStatus()` and `/api/firmware-version` endpoint | No code change needed; both endpoints already read from version files — UI change only |

The legacy `/ui-upload` and `/update` routes stay registered. They serve as recovery paths if the combined handler has a bug and must not be removed in this milestone.

### Version File Unification

After a combined update, both `/ui-version.txt` and `/firmware-version.txt` on LittleFS will contain the same version string (written from the combined TGZ filename). The diagnostics UI currently shows two version fields; it can be updated to show one. The two API endpoints (`/api/ui-version`, `/api/firmware-version`) are backward-compatible and require no firmware changes.

---

## esp_task_wdt_init Build Blocker

`esp_task_wdt_init(30, false)` in `MoodlightUtils.cpp` is incompatible with Arduino ESP32 Core 3.x, which changed the function signature to accept `const esp_task_wdt_config_t*`. This prevents `pio run` from completing and blocks the entire build script.

Fix: replace with the Core 3.x struct-based API:
```cpp
// Arduino ESP32 Core 3.x
const esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 30000,
    .idle_core_mask = 0,
    .trigger_panic = false
};
esp_task_wdt_init(&wdt_config);
```

Or conditionally compile based on `ESP_ARDUINO_VERSION_MAJOR`:
```cpp
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    const esp_task_wdt_config_t wdt_config = { .timeout_ms = 30000, .idle_core_mask = 0, .trigger_panic = false };
    esp_task_wdt_init(&wdt_config);
#else
    esp_task_wdt_init(30, false);
#endif
```

This is a prerequisite for everything else. Without it, `pio run` fails and no combined TGZ can be built.

---

## Component Boundaries

| Component | Responsibility | Communicates With |
|-----------|---------------|-------------------|
| `handleCombinedUpdate()` | Stream upload to LittleFS, orchestrate extract/install/flash sequence, set error flag | `TARGZUnpacker`, `Update` API, `LittleFS`, `copyFile()` |
| `build-release.sh` | Version bump in `config.h`, firmware build, combined TGZ packaging, git commit | PlatformIO CLI, filesystem, git |
| `diagnostics.html` | Single upload form targeting `/combined-update`, version display | `POST /combined-update`, `GET /api/ui-version` |
| `config.h` | Single source of truth for version string at compile time | Read by `build-release.sh`, compiled into firmware binary |
| `/ui-version.txt` on LittleFS | Runtime source of truth for version (survives firmware updates) | Written by `handleCombinedUpdate()`, read by `/api/ui-version` endpoint |

---

## Suggested Build Order for Implementation

Order is determined by hard dependencies:

1. **Fix `esp_task_wdt_init` build error** — blocks `pio run`; must be first. Zero risk, one-line change with version guard.

2. **Update `build-release.sh`** — add version bump + combined TGZ packaging. Test independently by inspecting the output archive with `tar -tzf`. No device needed.

3. **Implement `handleCombinedUpdate()`** — start with UI-only extraction to validate the route and LittleFS space usage. Add firmware flash step once extraction is confirmed working.

4. **Update `diagnostics.html`** — replace two upload forms with one; update version display. Can happen in parallel with step 3 once the route URL is finalized.

5. **End-to-end test** — build combined TGZ, upload via diagnostics page, verify version strings, LED behavior, and stability after reboot.

---

## Anti-Patterns to Avoid

### Writing firmware.bin to LittleFS before flash check
**Why bad:** With `min_spiffs`, a ~1 MB binary can fill LittleFS and silently fail mid-copy. The `copyFile()` helper returns void and does not surface errors, so the failure is invisible until the handler tries to open the file.
**Instead:** Check available LittleFS space before extraction. Use `ESP.getFlashChipSize()` / `LittleFS.totalBytes()` / `LittleFS.usedBytes()` to gate the operation.

### Flashing firmware before installing UI files
**Why bad:** `ESP.restart()` is called immediately after `Update.end()`. If firmware is flashed first, the UI copy loop is unreachable and the device reboots with mismatched UI.
**Instead:** UI install is always Phase 2, firmware flash is always Phase 3. This ordering is non-negotiable.

### Reusing the `combinedUpdateError` static without reset
**Why bad:** A failed upload followed by a retried successful one would still report failure because the static bool was set during the previous attempt and never cleared.
**Instead:** Reset `combinedUpdateError = false` at `UPLOAD_FILE_START`, not at variable declaration.

### Removing legacy /ui-upload and /update routes
**Why bad:** If the combined handler fails silently (e.g., LittleFS full, TAR extraction error), there is no recovery path without physical USB access.
**Instead:** Keep both legacy routes in this milestone. The diagnostics UI is updated to show only the combined upload form, but the routes remain registered for emergency use.

### Version bump before confirmed successful build
**Why bad:** If `pio run` fails after the version bump, `config.h` has been modified but no artifact was produced. The next git commit would record a version bump without a corresponding release.
**Instead:** In `build-release.sh`, bump the version and build the firmware, but only write the git commit at the very end, after all artifacts are verified to exist.

---

## Sources

- Direct inspection: `firmware/src/moodlight.cpp` lines 1312-1530 (`handleUiUpload`), lines 2914-2977 (firmware update handler), lines 2070-2075 (route registration)
- Direct inspection: `build-release.sh` (full file — current build flow)
- Direct inspection: `firmware/src/config.h` (version format)
- Direct inspection: `.planning/codebase/ARCHITECTURE.md` (system overview, LittleFS layout)
- Direct inspection: `.planning/PROJECT.md` (active requirements, constraints, partition info)
- Confidence: HIGH — all findings from live codebase; no external sources required for this architecture dimension
