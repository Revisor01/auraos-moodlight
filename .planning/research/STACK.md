# Technology Stack: Combined OTA Update (UI + Firmware)

**Project:** AuraOS Moodlight
**Milestone:** Combined Update File (v2.x milestone)
**Researched:** 2026-03-26
**Confidence:** HIGH — verified from local installed library source + partition table + actual release file sizes

---

## The Central Question: Can ESP32-targz Process a TGZ with firmware.bin Inside?

**Yes — but only via streaming, not via two-stage extraction.**

The answer has a hard constraint from the partition table that must drive the entire architecture.

### The 128 KB Wall

`min_spiffs.csv` (verified at `/Users/simonluthe/.platformio/packages/framework-arduinoespressif32/tools/partitions/min_spiffs.csv`):

```
spiffs, data, spiffs, 0x3D0000, 0x20000
```

`0x20000 = 128 KB` for the entire LittleFS partition. Current firmware.bin is **1.2 MB** (verified from `releases/v9.0/Firmware-9.0-AuraOS.bin`). Writing firmware.bin to LittleFS — even as a temporary file — is physically impossible. Any approach that stages the firmware on LittleFS before flashing will always fail with `ESP32_TARGZ_FS_FULL_ERROR (-100)`.

This rules out the naive two-stage approach (extract TGZ fully, then flash firmware.bin from LittleFS).

---

## Verified Library API

**Source verified from local installation:**
`firmware/.pio/libdeps/esp32dev/ESP32-targz/src/libunpacker/LibUnpacker.hpp`

### TarGzUnpacker — available methods

```cpp
// Extract TGZ to filesystem (used by current UI handler)
bool tarGzExpanderNoTempFile(fs::FS sourceFS, const char* sourceFile,
                             fs::FS destFS, const char* destFolder="/tmp");

// Stream TGZ directly to filesystem — no LittleFS temp file for the source
bool tarGzStreamExpander(Stream *stream, fs::FS &destFs,
                         const char* destFolder = "/", int64_t streamSize = -1);

// Stream TGZ directly to OTA flash partition (ESP32 only, requires HAS_OTA_SUPPORT)
// Naming convention: entries ending in ".ino.bin" -> U_FLASH partition
//                    entries ending in ".spiffs.bin" -> LittleFS partition image (full replace)
bool tarGzStreamUpdater(Stream *stream);
```

### Filter callbacks on TarUnpacker

```cpp
// Return true from the callback to SKIP that entry during extraction
void setTarExcludeFilter(tarExcludeFilter cb);  // tarExcludeFilter: (header_translated_t*) -> bool
void setTarIncludeFilter(tarIncludeFilter cb);  // tarIncludeFilter: (header_translated_t*) -> bool
```

These filters control whether a file is written to the destination filesystem. They cannot
redirect output to a different target (no per-entry routing). An excluded entry is simply skipped.

---

## The Only Viable Architecture: Per-Pass Streaming from HTTP Upload

Since firmware.bin cannot be staged on LittleFS, the combined TGZ must be processed in streaming
mode during the HTTP upload itself — not after `UPLOAD_FILE_END`. The upload stream is fed to the
unpacker as bytes arrive.

### What "streaming mode" means for ESP8266WebServer

The `HTTPUpload` structure provides a `buf` (uint8_t*) and `currentSize` on each `UPLOAD_FILE_WRITE`
call. This is a chunk-by-chunk push model. `tarGzStreamExpander` and `tarGzStreamUpdater` both
accept a `Stream*`. The handler must present the incoming chunks as a `Stream` to the unpacker.

The project already receives uploads in chunks (see `handleUiUpload()` lines 1400–1413 in
`moodlight.cpp`). The combined handler replaces the write-to-file loop with a stream feed.

### Single-pass vs. two-pass

The combined TGZ contains both UI files and firmware. A single pass can only route to one
destination. Two passes require reading the archive twice — but the source archive would need to
be stored somewhere first for the second read. Since storing it on LittleFS is impossible (1.2 MB
> 128 KB), two-pass is only viable if the archive is small enough to store. It is not.

**Conclusion: One pass is required. The archive must be designed so one pass handles both.**

---

## Recommended Architecture: tarGzStreamUpdater with Naming Convention

`tarGzStreamUpdater` is purpose-built for this. From the source (verified):

- Entries ending in `".ino.bin"` → streamed directly to `U_FLASH` via `Update.write()`. No intermediate storage. ~32 KB gzip dictionary RAM only.
- Entries ending in `".spiffs.bin"` → written as a binary image to the LittleFS OTA partition (full partition replace). **Not used in this project** since the existing file-by-file LittleFS update model must be preserved.

**The approach:** Pack the combined TGZ with:
- UI files at the root (normal paths: `index.html`, `css/style.css`, etc.)
- Firmware named `firmware.ino.bin`

Then use **two separate upload endpoints** (or a detection-based single endpoint) using two
different unpacker passes over the same stream:

```
Pass 1: tarGzStreamExpander — exclude *.bin — writes UI files to LittleFS
Pass 2: tarGzStreamUpdater  — detects firmware.ino.bin — flashes via Update API
```

But this still requires reading the stream twice. For a streaming-only model, a second read of
the same upload is not possible once the HTTP connection is gone.

### Resolution: Two Separate Routes Within One Upload Handler

The practical solution is to split the upload into two HTTP requests from the browser, but use
a **single combined TGZ file** for both:

1. `/update/ui` — client uploads the combined TGZ; server runs `tarGzStreamExpander` with exclude filter for `*.bin`
2. `/update/firmware` — client uploads the same combined TGZ again; server runs `tarGzStreamUpdater`

This is equivalent to the current two-upload workflow but with one file instead of two. The user
picks the same `.tgz` file twice. The UX is simplified (one file, two clicks) rather than the
goal of one file, one click.

### True Single-Upload Solution

To achieve one file, one click, the handler must buffer or process the stream in one pass while
routing based on filename. Since ESP32-targz does not support per-entry routing, this requires
a custom tar reader. The implementation path:

1. During `UPLOAD_FILE_WRITE`, feed chunks to `TarGzUnpacker::tarGzStreamExpander` for UI files
   while intercepting the firmware entry via a custom write callback
2. Override `tarStreamWriteCallback` — but this is a static method on `TarUnpacker` and is not
   user-overridable without modifying the library

The library does expose `gzStreamWriter` as a settable callback on `GzUnpacker`:

```cpp
void setStreamWriter(gzStreamWriter cb);  // gzStreamWriter: (unsigned char* buff, size_t buffsize) -> bool
```

This allows routing the decompressed output of a `.gz` file to any destination. However, this
only works for single-file `.gz`, not for multi-entry `.tar.gz`.

**Bottom line:** True single-pass, single-upload combined processing is not achievable with
ESP32-targz v1.2.7 without forking the library. The recommended approach is two-route with
one combined TGZ file.

---

## Recommended Stack (No New Dependencies)

### Core Technologies

| Component | Technology | Version | Rationale |
|---|---|---|---|
| TGZ extraction (UI pass) | ESP32-targz `tarGzStreamExpander` | 1.2.7 (installed) | Already used; streaming mode avoids LittleFS staging |
| Firmware flash pass | ESP32-targz `tarGzStreamUpdater` | 1.2.7 (installed) | Purpose-built for streaming TGZ → OTA flash |
| Filesystem | LittleFS | built-in | Already configured |
| Upload transport | ESP8266WebServer | built-in | Already used; chunk-based `UPLOAD_FILE_WRITE` compatible |

No new library dependencies required. Zero changes to `platformio.ini` (beyond the pre-existing
`esp_task_wdt_init` build fix).

---

## Build Script: Creating the Combined TGZ (macOS bash)

macOS `tar` does not support `--transform` or append (`-r`) to compressed archives.
The correct macOS-compatible approach is uncompressed `.tar` first, then `gzip`:

```bash
VERSION=$(grep '^#define MOODLIGHT_VERSION' firmware/src/config.h | cut -d'"' -f2)
RELEASE_DIR="releases/v${VERSION}"
mkdir -p "$RELEASE_DIR"

# Step 1: Create uncompressed tar from UI files
cd firmware/data
tar -cf "/tmp/combined.tar" \
    --exclude="*.tmp.*" \
    --exclude="*.tgz" \
    --exclude="*.tar" \
    index.html setup.html mood.html diagnostics.html js/ css/
cd ../..

# Step 2: Rename firmware.bin and append to the uncompressed tar
# Name must end in .ino.bin for tarGzStreamUpdater to flash it to U_FLASH
cp firmware/.pio/build/esp32dev/firmware.bin /tmp/firmware.ino.bin
tar -rf /tmp/combined.tar -C /tmp firmware.ino.bin

# Step 3: Gzip the combined tar
gzip -c /tmp/combined.tar > "${RELEASE_DIR}/AuraOS-${VERSION}-Combined.tgz"

# Cleanup
rm -f /tmp/combined.tar /tmp/firmware.ino.bin
```

**Version bump logic** (add before the build steps):

```bash
BUMP_TYPE=${1:-patch}  # major | minor | patch

bump_version() {
    local current=$1
    local type=$2
    IFS='.' read -r major minor patch <<< "$current"
    patch=${patch:-0}
    case $type in
        major) echo "$((major+1)).0" ;;
        minor) echo "${major}.$((minor+1))" ;;
        patch) echo "${major}.${minor}" ;;  # AuraOS uses major.minor only
    esac
}

NEW_VERSION=$(bump_version "$VERSION" "$BUMP_TYPE")
sed -i '' "s/#define MOODLIGHT_VERSION \"${VERSION}\"/#define MOODLIGHT_VERSION \"${NEW_VERSION}\"/" \
    firmware/src/config.h
```

---

## Upload Handler Design (ESP32 side)

### Route 1: `/update/combined-ui` (new)

```cpp
server.on("/update/combined-ui", HTTP_POST, /* response handler */, []() {
    HTTPUpload& upload = server.upload();
    static TarGzUnpacker *unpacker = nullptr;

    if (upload.status == UPLOAD_FILE_START) {
        unpacker = new TarGzUnpacker();
        unpacker->setTarVerify(false);
        unpacker->setupFSCallbacks(targzTotalBytesFn, targzFreeBytesFn);
        // Exclude firmware.bin — prevents trying to write 1.2MB to 128KB LittleFS
        unpacker->setTarExcludeFilter([](TAR::header_translated_t *h) -> bool {
            return String(h->filename).endsWith(".bin");
        });
        // NOTE: tarGzStreamExpander must be called here, not at UPLOAD_FILE_END
        // The Stream* approach requires the stream to be active during extraction
    }
    // ... feed upload.buf chunks to the stream
});
```

### Route 2: `/update/combined-firmware` (new)

```cpp
server.on("/update/combined-firmware", HTTP_POST, /* response + restart */, []() {
    HTTPUpload& upload = server.upload();
    static TarGzUnpacker *unpacker = nullptr;

    if (upload.status == UPLOAD_FILE_START) {
        unpacker = new TarGzUnpacker();
        unpacker->setTarVerify(false);
        unpacker->setupFSCallbacks(targzTotalBytesFn, targzFreeBytesFn);
        // tarGzStreamUpdater streams .ino.bin entries directly to U_FLASH
    }
    // ... feed upload.buf chunks to the stream
    // At UPLOAD_FILE_END: Update.end(true); ESP.restart();
});
```

---

## Diagnostics UI Change

Replace two upload sections (UI upload + Firmware upload) with two upload buttons that
both target the same combined `.tgz` file:

```
[ Choose file: AuraOS-10.0-Combined.tgz ]
[ Update UI ]  [ Update Firmware ]
```

Each button POSTs the file to a different endpoint. The user selects the file once; both buttons
are visible. To do a full update: click "Update UI", wait for completion, then click
"Update Firmware", wait for restart.

A single "Full Update" button that uploads the file twice sequentially (one AJAX per endpoint)
is the cleanest UX and fully achievable with the existing HTML/JS pattern in `diagnostics.html`.

---

## Memory Budget

| During UI extraction pass | RAM usage |
|---|---|
| gzip dictionary | ~32 KB |
| tar entry buffer | ~4 KB (GZIP_BUFF_SIZE defined in library) |
| LittleFS write buffer | ~4 KB |
| ESP8266WebServer upload buf | ~1–4 KB |
| **Total additional** | **~40–44 KB** |
| Available heap (typical idle) | ~100–150 KB |
| **Verdict** | Comfortable |

| During firmware flash pass | RAM usage |
|---|---|
| gzip dictionary | ~32 KB |
| tar entry buffer | ~4 KB |
| Update.write buffer | ~512 B (loop buffer in handler) |
| **Total additional** | **~37 KB** |
| **Verdict** | Comfortable |

No PSRAM required. Heap stays well above the ~50 KB danger zone.

---

## Alternatives Considered

| Approach | Why Rejected |
|---|---|
| Store combined TGZ on LittleFS, then two-pass extract | firmware.bin (1.2 MB) > entire LittleFS (128 KB) — physically impossible |
| Single HTTP upload, one-pass processing via custom TAR write callback | `tarStreamWriteCallback` is a static method, not user-settable without forking ESP32-targz |
| `tarGzStreamUpdater` with `.spiffs.bin` for UI | Replaces entire LittleFS partition as binary image; incompatible with file-by-file update model |
| Two-file upload (keep current UI-TGZ + Firmware-BIN) | Explicitly out of scope per PROJECT.md Core Value |
| Custom gzip streaming with manual TAR header parsing | Valid but ~200 lines of low-level C++; tarGzStreamUpdater with naming convention achieves the same for the firmware path with ~5 lines |
| ElegantOTA / AsyncElegantOTA | Would require migrating from ESP8266WebServer to AsyncWebServer — large refactor, not warranted for this feature |

---

## Pre-existing Bug: esp_task_wdt_init

From `PROJECT.md`: `esp_task_wdt_init(30, false)` is incompatible with Arduino ESP32 Core 3.x API.
This blocks `pio run`. The combined update milestone **cannot ship** without fixing this first.

Fix (in `MoodlightUtils.cpp` or wherever called):

```cpp
// Arduino ESP32 Core 3.x changed the API to require esp_task_wdt_config_t
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 30000,
        .idle_core_mask = 0,
        .trigger_panic = false
    };
    esp_task_wdt_init(&wdt_config);
#else
    esp_task_wdt_init(30, false);
#endif
```

---

## Sources

| Source | Confidence | Used For |
|---|---|---|
| `firmware/.pio/libdeps/esp32dev/ESP32-targz/src/libunpacker/LibUnpacker.hpp` (local) | HIGH | All API signatures |
| `firmware/.pio/libdeps/esp32dev/ESP32-targz/src/types/esp32_targz_types.h` (local) | HIGH | Callback types, error codes |
| `/Users/simonluthe/.platformio/packages/framework-arduinoespressif32/tools/partitions/min_spiffs.csv` (local) | HIGH | 128 KB LittleFS partition size |
| `releases/v9.0/Firmware-9.0-AuraOS.bin` (local) | HIGH | 1.2 MB firmware size |
| `releases/v9.0/UI-9.0-AuraOS.tgz` (local) | HIGH | 35 KB UI archive size |
| [ESP32-targz GitHub](https://github.com/tobozo/ESP32-targz) | MEDIUM | tarGzStreamUpdater naming convention confirmed |
| [ESP32-targz Update_from_gz_stream example](https://github.com/tobozo/ESP32-targz/blob/master/examples/ESP32/Update_from_gz_stream/Update_from_gz_stream.ino) | MEDIUM | `.ino.bin` / `.spiffs.bin` suffix requirement |
