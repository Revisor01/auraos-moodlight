# Testing Patterns

> Generated: 2026-03-25 | Focus: quality

## Test Framework

**No test framework is configured for any part of the project.**

### Firmware (C++/PlatformIO)

- **Runner:** None configured
- **Config:** `firmware/platformio.ini` has no test configuration
- **Test directory:** `firmware/test/` exists but contains only the PlatformIO boilerplate `README` file
- **No test files exist anywhere in the firmware source**

PlatformIO supports unit testing natively via `pio test`, but this has not been set up.

### Backend (Python/Flask)

- **Runner:** None configured
- **No pytest, unittest, or any test framework** in `sentiment-api/requirements.txt`
- **No test files** exist in `sentiment-api/`
- **No test directory** exists

### Web Interface (JavaScript)

- **No test framework** (no Jest, Vitest, Mocha, or equivalent)
- **No test files** exist in `firmware/data/js/`

## How the Project Is Currently Tested

**Manual testing only.** The project relies entirely on:

1. **Serial monitor debugging:** `pio device monitor` at 115200 baud with ESP32 exception decoder
2. **Web interface manual testing:** Accessing device at its IP address
3. **Debug logging:** The `debug()` function logs to Serial and a ring buffer accessible via `/logs` HTTP endpoint
4. **Build verification:** Successful `pio run` compilation is the only automated check

### Debug Mode

Controlled by `#define DEBUG_MODE` in `firmware/src/moodlight.cpp` (line 31). When enabled:
- All `debug()` calls print to Serial with timestamp and free heap
- Ring buffer stores last 20 entries for web-based log viewing

When disabled:
- Only `ERROR:` and `CRITICAL:` prefixed messages are printed

### Build Verification Commands

```bash
# Firmware: compile only (no device needed)
cd firmware && pio run

# Firmware: compile and upload to connected ESP32
cd firmware && pio run -t upload

# Firmware: upload web interface files to device filesystem
cd firmware && pio run -t uploadfs

# Firmware: serial monitor for debugging
cd firmware && pio device monitor

# Backend: run locally (requires env vars)
cd sentiment-api && python app.py

# Release build (creates firmware binary + UI package)
./build-release.sh
```

## CI/CD Setup

### GitHub Actions

**One workflow exists:** `.github/workflows/build-sentiment-api.yml`

**Purpose:** Builds and pushes Docker image for the sentiment API backend to GitHub Container Registry (GHCR).

**Triggers:**
- Push to `main` branch with changes in `sentiment-api/` or the workflow file itself
- Manual `workflow_dispatch`

**Steps:**
1. Checkout repository
2. Login to GHCR
3. Extract Docker metadata (tags: `latest` + commit SHA)
4. Build and push Docker image from `./sentiment-api` context
5. Trigger Portainer webhook for auto-deployment (if configured)

**What is NOT covered by CI:**
- No firmware build verification in CI
- No Python tests or linting in CI
- No JavaScript/HTML validation
- No code quality checks (no SonarQube, CodeClimate, etc.)

### Deployment

**Backend deployment:** Two options documented in `CLAUDE.md`:
1. **Automated via CI:** Push to main -> GitHub Actions builds image -> Portainer webhook deploys
2. **Manual:** SSH to server, `git pull`, `docker-compose build && up -d`

**Firmware deployment:** Fully manual:
1. Build with `pio run` or `./build-release.sh`
2. Upload OTA via device web interface (`/diagnostics.html`)
3. Or USB upload via `pio run -t upload`

## Linting / Formatting Tools

**None configured for any part of the project.**

| Tool Category | C++ Firmware | Python Backend | JavaScript UI |
|--------------|-------------|---------------|--------------|
| Formatter | None | None | None |
| Linter | None | None | None |
| Type checker | N/A | None (no mypy) | None |
| Config file | None | None | None |

### Build Flags (Firmware)

The `firmware/platformio.ini` includes optimization flags that serve as minimal quality controls:
```ini
build_flags =
    -Os                              # Optimize for size
    -DNDEBUG                         # Remove debug assertions
    -DCORE_DEBUG_LEVEL=0             # Suppress ESP-Core debug output
    -DARDUINOJSON_ENABLE_COMMENTS=0  # Disable JSON comment parsing
    -D CONFIG_SPIRAM_CACHE_WORKAROUND=1
```

## Coverage

**No coverage tracking exists.** No coverage tools, no coverage reports, no coverage targets.

## Test Types

### Unit Tests
- **Not implemented.** No unit tests exist for any component.

### Integration Tests
- **Not implemented.** No automated tests for API endpoints, MQTT integration, or database operations.

### E2E Tests
- **Not implemented.** No automated end-to-end testing from ESP32 device to backend API.

### Hardware-in-the-Loop Tests
- **Not implemented.** No automated tests involving actual ESP32 hardware.

## Quality Gaps Identified

### Critical Gaps

**1. No automated tests whatsoever**
- The entire project (firmware + backend + web UI) has zero automated tests
- Every change requires manual verification
- Risk: Regressions go unnoticed until deployment

**2. No CI for firmware**
- Firmware compilation is not verified in CI
- A broken firmware commit could go unnoticed on `main`
- Fix: Add a PlatformIO build step to GitHub Actions

**3. No Python linting or type checking**
- No `flake8`, `ruff`, `mypy`, or equivalent
- Type hints exist in `database.py` but are never checked
- Fix: Add `ruff` and `mypy` to CI pipeline

### High Priority Gaps

**4. No API endpoint tests for backend**
- Flask provides `test_client()` for easy endpoint testing
- Critical endpoints like `/api/moodlight/current` and `/api/moodlight/history` are untested
- The sentiment analysis function `analyze_headlines_openai_batch()` has complex logic that should be unit tested

**5. No database migration system**
- Schema defined only in `sentiment-api/init.sql`
- No Alembic, Flyway, or equivalent migration tool
- Schema changes require manual SQL execution

**6. No frontend validation**
- JavaScript in `firmware/data/js/` has no tests
- HTML forms have no automated validation testing
- Web interface behavior is verified only by manual testing on the device

### Medium Priority Gaps

**7. Release build script has no validation**
- `build-release.sh` creates release artifacts but does not run any tests
- Checksums are generated but not verified after creation
- No smoke test of the built firmware

**8. Duplicated code between modules**
- RSS feed list duplicated in `app.py` (line 55-68) and `background_worker.py` (line 137-150)
- Category determination duplicated in `moodlight_extensions.py` and `background_worker.py`
- No tests to catch drift between duplicated logic

## Recommended Testing Strategy

### Quick Wins (Backend)

```bash
# Add to requirements.txt:
pytest==8.0.0
pytest-flask==1.3.0
```

**Testable areas in `sentiment-api/`:**
- `database.py`: `Database.save_sentiment()`, `get_latest_sentiment()`, `get_sentiment_history()`
- `moodlight_extensions.py`: All API endpoints via Flask test client
- `background_worker.py`: `_get_category()`, `_fetch_headlines()` (with mocked feedparser)
- `app.py`: `analyze_headlines_openai_batch()` with mocked OpenAI client

### Quick Wins (Firmware)

PlatformIO native test runner can test utility classes:
- `MoodlightUtils::formatString()`
- `MoodlightUtils::formatTime()`
- `MemoryMonitor::formatBytes()`
- Color conversion: `uint32ToColorDef()`, `getColorDefinition()`

### CI Pipeline Additions

```yaml
# Add to .github/workflows/:
# 1. Firmware build check
- name: Build Firmware
  run: cd firmware && pio run

# 2. Python tests + linting
- name: Lint Python
  run: cd sentiment-api && pip install ruff && ruff check .
- name: Test Python
  run: cd sentiment-api && pip install pytest && pytest
```

## Open Questions

- Should PlatformIO Unity test framework be used, or a custom approach for ESP32 testing?
- Is there a staging environment for the backend API, or only production?
- Are there any manual test checklists or QA processes not documented in the repo?

---

*Testing analysis: 2026-03-25*
