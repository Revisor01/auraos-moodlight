# Feature Landscape: OTA Update + Build Automation

**Domain:** ESP32 Embedded OTA + Build-Release Automation
**Researched:** 2026-03-26
**Confidence:** HIGH (derived from direct codebase analysis + clear project context)

---

## Context

This is a milestone that adds two tightly coupled capabilities to an existing, running system:

1. **Combined Update:** One TGZ file replaces two separate uploads (UI-TGZ first, then Firmware-BIN). The ESP32 receives a single archive, extracts UI files to LittleFS, then flashes firmware — sequentially in one handler.
2. **Build Automation:** `./build-release.sh` gains version-bump logic, packages the Combined TGZ, and commits. "One click" from developer intent to release artifact + commit.

This is not a greenfield feature design. The existing handlers (`handleUiUpload`, firmware upload via `Update.begin`) already work. The question is: what must change to make the combined flow reliable, and what is explicitly out of scope?

---

## Current State Baseline

**What exists today:**

| Component | Current State | Gap |
|-----------|--------------|-----|
| `handleUiUpload()` | Receives TGZ, writes to `/temp/`, extracts to `/extract/`, copies files, saves version from filename | Works standalone; knows nothing about an embedded firmware blob |
| Firmware upload handler | Uses Arduino `Update.begin()` / `Update.write()` / `Update.end()` streaming; extracts version from filename | Works standalone; triggered by separate form field |
| `build-release.sh` | Reads version from `config.h`, packs UI-TGZ, compiles firmware, copies BIN, creates checksums and README | No version-bump logic; produces two files, not one combined file |
| `diagnostics.html` | Full diagnostics page; no update UI section present at all | Two separate upload forms need to be replaced with one |
| Version state | `MOODLIGHT_VERSION` in `config.h` (firmware), `ui-version.txt` on LittleFS (UI, written during upload) | Two separate version files; milestone goal is one unified version |

---

## Table Stakes

Features that must exist for this milestone to ship. Missing any one of these leaves the core value ("one click builds and deploys") unfulfilled.

### ESP32-side: New Combined Upload Handler

| Feature | Why Required | Complexity | Notes |
|---------|--------------|------------|-------|
| **Detect combined vs. plain TGZ by filename convention** | Handler must decide: is this a combined update or a plain UI-only TGZ? Filename-based routing (`Combined-X.X-AuraOS.tgz`) is the simplest contract — no magic bytes parsing needed. | LOW | Existing `handleUiUpload` already reads filename. Extend to branch on `Combined-` prefix. Plain `UI-` TGZ must still work (backward compat). |
| **Stream combined TGZ to LittleFS, extract, separate UI files from firmware.bin** | The combined archive contains `firmware.bin` + UI files. Extraction via ESP32-targz places everything in `/extract/`. The handler then installs UI files as today, plus finds `/extract/firmware.bin` for the next step. | MEDIUM | LittleFS space constraint: min_spiffs partition is small. The combined archive + extraction temp files + existing UI files must all fit simultaneously. This is the highest-risk point of the whole milestone — see PITFALLS.md. |
| **Flash `/extract/firmware.bin` via `Update` API after UI install** | After UI files are copied from `/extract/` to `/`, the handler calls `Update.begin(fwSize)`, streams the binary, calls `Update.end(true)`, then triggers restart. This is the second phase of the sequential operation. | MEDIUM | Must happen after UI copy completes, not concurrently. Update must be the last operation before restart — no LittleFS writes after `Update.end()` because ESP restarts. |
| **Single unified version written to `/firmware-version.txt` (or config.h-derived)** | The milestone goal is "one version for everything." After a combined update, one version identifier should be readable via `/api/firmware-version`. The separate `/ui-version.txt` can be removed or unified. | LOW | Simplest approach: combined handler writes the version (from filename) once to `/firmware-version.txt`. Firmware's compile-time `MOODLIGHT_VERSION` is the authoritative source; written file is a runtime readback for the UI. |
| **Single upload field in diagnostics.html** | Replace two separate upload sections (UI first, firmware second) with one combined upload section. Must display one current version. | LOW | The existing diagnostics.html has no update UI at all — this section must be added from scratch. |
| **Graceful error handling: rollback or clear state on failure** | If extraction succeeds but firmware flash fails, the device must not be left with new UI files and old firmware that may be incompatible. Minimum: restore `/backup/` files on flash failure, or restart with old firmware (which it will, since failed `Update.end()` doesn't commit). | MEDIUM | Arduino `Update` API already handles failed flash by not committing — device reboots into old firmware. UI files however ARE already overwritten by this point. The safest pattern: do UI install last, firmware first — but that conflicts with normal memory layout. Alternative: install UI to `/backup/` first, only move to `/` after firmware flashes successfully. |

### Build Script: Version Bump + Combined Package

| Feature | Why Required | Complexity | Notes |
|---------|--------------|------------|-------|
| **`./build-release.sh [major\|minor\|patch]` bumps version in `config.h`** | The core value is "one click, no manual versioning." The script must parse the current `MAJOR.MINOR` (or `MAJOR.MINOR.PATCH`) from `#define MOODLIGHT_VERSION`, increment the appropriate component, and write it back with `sed`. | LOW | Current version format is `"9.0"` — simple MAJOR.MINOR. Script needs to handle at least `major` and `minor`; `patch` is optional. Must error if no bump type given and no explicit version passed. |
| **Build combined TGZ: UI files + `firmware.bin` in one archive** | Replaces the current two-file output. One `tar -czf` that includes both the UI files (HTML/CSS/JS) and `firmware/.pio/build/esp32dev/firmware.bin` as `firmware.bin`. | LOW | Filename convention: `Combined-X.X-AuraOS.tgz`. Existing `UI-X.X-AuraOS.tgz` and `Firmware-X.X-AuraOS.bin` generation can be removed or kept for backward compat (decision point — see below). |
| **Checksums cover the combined file** | SHA256 checksum for the combined TGZ. Consistent with existing `checksums.txt` approach. | LOW | Trivially extend existing checksum step. |
| **Git commit after successful build** | The "one click" contract includes committing the version bump. Script should `git add firmware/src/config.h && git commit -m "Bump: vX.X"`. The releases/ directory stays out of git (`.gitignore`). | LOW | Must not commit binary release files. Only `config.h` changes. |
| **Fix pre-existing `esp_task_wdt_init` build error before any of the above works** | `pio run` fails due to `esp_task_wdt_init(30, false)` being incompatible with Arduino ESP32 Core 3.x API. Without this fix, the build script's `pio run` step will always fail. This is a blocker, not an enhancement. | LOW | Fix: replace with `esp_task_wdt_config_t` struct initialization. See PITFALLS.md for exact fix. |

---

## Differentiators

Nice to have for this milestone, but not required for the core value. Defer unless implementation is trivial.

| Feature | Value Proposition | Complexity | Decision |
|---------|-------------------|------------|----------|
| **Keep separate UI-TGZ and Firmware-BIN as additional outputs** | Backward compatibility: if combined update has issues, fallback to two-step still works | LOW | Keep for first iteration. Remove in a later milestone once combined flow is proven stable. |
| **Progress feedback in upload UI** | Show bytes received / extraction progress during upload | MEDIUM | The HTTP upload is chunked; progress events are available in JS via `XMLHttpRequest.upload.onprogress`. Nice UX but not blocking. |
| **Pre-flight LittleFS space check before accepting upload** | Reject upload early if insufficient space, with clear error message | LOW | Worth adding — prevents a confusing mid-extraction failure. Small addition to the upload handler's `UPLOAD_FILE_START` branch. |
| **Build script: `--dry-run` flag** | Show what version would be bumped to without actually changing anything | LOW | Useful for CI but not needed for solo hobby use. |
| **Build script: automatic `git tag vX.X`** | Tags the commit for easier navigation in git history | LOW | One line addition. Useful but not core. |
| **Version shown in diagnostics.html header** | Display current firmware version prominently, fetched from `/api/firmware-version` | LOW | The `<span class="version" id="version">v1.0</span>` is already in the header but never populated. Wire it up. |

---

## Anti-Features

Explicitly NOT building in this milestone.

| Anti-Feature | Why It Will Be Requested | Why Not Now | What Instead |
|--------------|--------------------------|-------------|--------------|
| **Pull-based OTA (device polls a server for updates)** | Standard IoT update pattern for production devices | This is a single home device on a LAN. Push via browser is simpler, faster, and has no infrastructure cost. Pull-based OTA requires an update server, version manifest endpoint, and scheduled polling. | Continue with push-based browser upload. |
| **Signed/verified firmware images** | Security best practice — prevents flashing arbitrary code | Requires a signing key infrastructure, verification in the bootloader or pre-flash, and build pipeline changes. Overkill for a private home device with no external attack surface. | Out of scope per PROJECT.md (no auth). |
| **Delta/incremental firmware updates** | Reduces upload size | ESP32 delta update tools (e.g., `esp_delta_ota`) require a patching step on-device that consumes significant RAM. On a 4MB flash / 320KB RAM device with `min_spiffs`, this is a memory constraint risk. | Full image OTA is fine for a ~800KB firmware. |
| **Rollback to previous firmware via web UI** | Recovery without serial access | Requires a dual-partition OTA scheme (OTA0/OTA1 swap). The current partition table is `min_spiffs.csv` which maximizes app partition size but leaves no room for a second app partition. Partition table changes are a separate infrastructure milestone. | The existing backup of UI files + Arduino `Update` API's automatic "don't commit on failure" is sufficient recovery for hobby use. |
| **Build script runs OTA upload automatically** | End-to-end automation: build → deploy | Network-dependent step in a build script makes CI fragile. On a home network, the device IP may change. Separating build from deploy is the correct pattern. | Build produces the artifact; deploy is a manual browser upload or a separate `deploy.sh` if desired. |
| **GitHub Actions CI build** | Automated CI/CD for firmware | Firmware CI requires cross-compilation toolchain setup in Actions, which adds significant setup complexity. The backend already has CI. Firmware builds are fast locally. | Local `./build-release.sh` is sufficient for a one-device hobby project. |
| **Semantic versioning (MAJOR.MINOR.PATCH with semver rules)** | Proper versioning discipline | The current `MAJOR.MINOR` format (`"9.0"`) is simpler and sufficient. Full semver adds script complexity for no user-visible benefit on a single-device project. | Keep MAJOR.MINOR. Support `major` and `minor` bump arguments. |

---

## Feature Dependencies

```
[BUG] esp_task_wdt_init fix
    └──blocks──> all firmware builds
    └──must be first task in milestone

[SCRIPT] Version bump in build-release.sh
    └──requires──> working pio build (depends on wdt fix)
    └──produces──> updated config.h with new MOODLIGHT_VERSION

[SCRIPT] Combined TGZ packaging
    └──requires──> UI files + firmware.bin both exist after build
    └──produces──> Combined-X.X-AuraOS.tgz

[ESP32] Combined upload handler
    └──reads──> Combined-X.X-AuraOS.tgz (filename prefix detection)
    └──uses──> existing ESP32-targz library for extraction
    └──uses──> existing Arduino Update API for firmware flash
    └──writes──> UI files to LittleFS (as today)
    └──writes──> firmware via OTA

[UI] diagnostics.html update section
    └──calls──> combined upload endpoint (new handler)
    └──displays──> version from /api/firmware-version

[VERSION] Unified version state
    └──source of truth──> MOODLIGHT_VERSION in config.h (compile-time)
    └──runtime readback──> /firmware-version.txt on LittleFS
    └──eliminates──> separate /ui-version.txt
```

### Critical Ordering Constraint

The combined handler must decide: flash firmware first, then install UI — or UI first, then firmware.

**Recommendation: UI files first, firmware flash second.**

Rationale: If firmware flash fails, Arduino `Update` API does not commit — device reboots into old firmware. The UI files are already overwritten, but old firmware can serve old UI from backup (since backup is created before overwrite). If firmware flash succeeds, device reboots into new firmware that serves the new UI.

Reverse order (firmware first, then UI) would leave new firmware running old UI if the UI copy step fails — harder to detect and recover from.

---

## MVP for This Milestone

### Must Ship

- [ ] Fix `esp_task_wdt_init` build error (blocker for everything)
- [ ] `build-release.sh [major|minor]` bumps version in `config.h`
- [ ] `build-release.sh` produces `Combined-X.X-AuraOS.tgz` (UI + firmware.bin)
- [ ] `build-release.sh` commits version bump (`git add config.h && git commit`)
- [ ] ESP32 combined upload handler: detects `Combined-` prefix, extracts, installs UI, flashes firmware
- [ ] Single upload form in `diagnostics.html` with version display
- [ ] Unified version: one version number, one file on LittleFS

### Defer

- Pull-based OTA — wrong pattern for this use case
- Signed firmware — out of scope per PROJECT.md
- GitHub Actions firmware CI — unnecessary overhead
- Rollback via UI — requires partition table changes, separate milestone

---

## Sources

- Direct codebase analysis: `firmware/src/moodlight.cpp` (lines 1312–1520: `handleUiUpload`, lines 2930–2977: firmware upload handler)
- Direct codebase analysis: `build-release.sh` (current two-file release flow)
- Direct codebase analysis: `firmware/data/diagnostics.html` (no update UI present)
- Project requirements: `.planning/PROJECT.md` (active requirements list, constraints, out-of-scope items)
- ESP32 Arduino `Update` API behavior on failure: does not commit if `Update.end()` fails — device reboots into previous partition
- ESP32-targz library: `tarGzExpanderNoTempFile()` in current use; streams directly to LittleFS
- LittleFS space constraint: `min_spiffs.csv` partition — confirmed constraint in PROJECT.md

---

*Feature research for: Combined OTA Update + Build Automation milestone*
*Researched: 2026-03-26*
