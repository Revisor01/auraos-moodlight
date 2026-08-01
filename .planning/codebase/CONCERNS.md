# Technical Concerns & Risks
> Generated: 2026-03-25 | Focus: concerns

## Critical

### No Authentication on ESP32 Web Server or Backend API
- **Impact**: Anyone on the local network can access the ESP32 web interface and change all settings (WiFi credentials, MQTT passwords, factory reset, firmware upload). The backend API at `analyse.godsapp.de` has no authentication on any endpoint, including the `/api/feedconfig` POST endpoint that modifies the global RSS feed list, and `/api/moodlight/cache/clear` POST which clears the Redis cache.
- **Evidence**:
  - `firmware/src/moodlight.cpp` lines 2200-2226: `/savewifi` accepts WiFi credentials via POST with no auth
  - `firmware/src/moodlight.cpp` lines 2520-2551: `/factoryreset` wipes all settings with no auth
  - `firmware/src/moodlight.cpp` lines 1284+: UI upload (firmware update) has no auth
  - `sentiment-api/app.py` lines 459-507: `/api/feedconfig` POST modifies global `rss_feeds` dict without authentication
  - `sentiment-api/moodlight_extensions.py` lines 310-328: `/api/moodlight/cache/clear` POST without auth
- **Recommendation**: Add basic auth or API key to the backend API admin endpoints. For the ESP32, add optional web password support in setup mode. At minimum, protect `/factoryreset`, `/savewifi`, `/savemqtt`, `/savehardware`, and firmware upload endpoints.

### WiFi and MQTT Credentials Exposed via API
- **Impact**: Credentials stored in plaintext on the device are returned in full via unprotected HTTP GET endpoints. Anyone on the network can exfiltrate WiFi password, MQTT credentials, and API URL.
- **Evidence**:
  - `firmware/src/moodlight.cpp` line 672: `doc["wifiPass"] = wifiPassword;` in `saveSettingsToFile()`
  - `firmware/src/moodlight.cpp` lines 2088-2096: `/api/export/settings` returns all credentials including `wifiPass` and `mqttPass` over HTTP GET
  - `firmware/src/moodlight.cpp` line 2139: `/api/settings/mqtt` GET returns MQTT password in plaintext
- **Recommendation**: Never return passwords in API responses. Use masked values (e.g., `"****"`) for display. Separate credential storage from exportable settings.

### Backend API Uses HTTP (Not HTTPS)
- **Impact**: All communication between ESP32 and the backend is unencrypted. Sentiment data, device tracking information, and API requests travel in plaintext.
- **Evidence**:
  - `firmware/src/config.h` line 26: `#define DEFAULT_NEWS_API_URL "http://analyse.godsapp.de/api/moodlight/current"` - explicit HTTP
  - ESP32 uses `WiFiClient` (not `WiFiClientSecure`) in `firmware/src/moodlight.cpp` line 100
- **Recommendation**: Enable HTTPS on the backend and switch the default API URL to HTTPS. Use `WiFiClientSecure` on the ESP32 with certificate pinning or CA bundle.

### Open Access Point with No Password
- **Impact**: The setup AP `Moodlight-Setup` broadcasts with an empty password. Anyone nearby can connect and configure the device, including setting WiFi credentials that redirect traffic.
- **Evidence**:
  - `firmware/src/config.h` line 16: `#define DEFAULT_AP_PASSWORD ""`
  - `firmware/src/moodlight.cpp` line 921: `WiFi.softAP(DEFAULT_AP_NAME, DEFAULT_AP_PASSWORD);`
- **Recommendation**: Generate a random AP password on first boot and display it via serial output. Or require physical button press to enable AP mode.

## Moderate

### Monolithic 4500-Line Firmware File
- **Impact**: `firmware/src/moodlight.cpp` is 4500 lines with all logic in a single file: web server handlers, WiFi management, MQTT, LED control, OTA updates, sensor reading, settings management. This makes maintenance error-prone, increases merge conflicts, and makes testing impossible.
- **Evidence**: `firmware/src/moodlight.cpp` - 4500 lines with ~50 global variables (lines 136-210), ~30 web server route handlers, and multiple subsystems mixed together.
- **Recommendation**: Split into separate files: `wifi_manager.cpp`, `web_handlers.cpp`, `mqtt_handler.cpp`, `led_controller.cpp`, `settings.cpp`, `ota_update.cpp`. Use classes with clear interfaces rather than global state.

### Duplicated RSS Feed Lists
- **Impact**: The RSS feed list is defined in two places with identical content. Changes to one must be manually replicated to the other, creating a risk of drift.
- **Evidence**:
  - `sentiment-api/app.py` lines 55-68: `rss_feeds` dict
  - `sentiment-api/background_worker.py` lines 137-150: identical `rss_feeds` dict in `_fetch_headlines()`
- **Recommendation**: Extract the feed list to a shared config module or have the background worker import it from `app.py`.

### Duplicated Category Logic
- **Impact**: The sentiment category classification (score ranges to category names) is defined in three places with slightly different thresholds.
- **Evidence**:
  - `sentiment-api/app.py` lines 205-209: thresholds at 0.85, 0.2, -0.85, -0.2
  - `sentiment-api/background_worker.py` lines 216-227: thresholds at 0.30, 0.10, -0.20, -0.50
  - `sentiment-api/moodlight_extensions.py` lines 24-35: thresholds at 0.30, 0.10, -0.20, -0.50
- **Recommendation**: Consolidate into a single shared function. The `app.py` thresholds (0.85/0.2) differ significantly from the other two (0.30/0.10), which may cause inconsistent category labels.

### JSON Buffer Pool Memory Leak Risk
- **Impact**: When all pool buffers are in use, the fallback allocates from heap with `new char[]`. If the `release()` call fails to acquire the mutex (100ms timeout), the heap allocation is never freed.
- **Evidence**:
  - `firmware/src/moodlight.cpp` lines 342-356: `acquire()` falls back to `new char[JSON_BUFFER_SIZE]` (16KB)
  - `firmware/src/moodlight.cpp` lines 359-372: `release()` attempts mutex with 100ms timeout; if it fails, buffer is never freed
- **Recommendation**: Use RAII pattern or ensure `release()` always frees heap-allocated buffers even if mutex acquisition fails. Add a `wasPooled` flag or use a different tracking mechanism.

### Unreachable Code After Return
- **Impact**: Dead code in `fetchBackendStatistics()` after the `if/else` blocks both return.
- **Evidence**: `firmware/src/moodlight.cpp` lines 1067-1072: code after the `if (httpCode == HTTP_CODE_OK)` block that can never execute because both branches return or fall through to `http.end()`.
- **Recommendation**: Remove the unreachable lines 1067-1072.

### Flask Development Server in Production
- **Impact**: The backend runs `app.py` directly via `python app.py`, which uses Flask's built-in development server. This is single-threaded (aside from the background worker thread), not production-hardened, and not suitable for handling concurrent requests.
- **Evidence**:
  - `sentiment-api/Dockerfile` line 18: `CMD ["python", "app.py"]`
  - `sentiment-api/app.py` line 574: `app.run(host='0.0.0.0', port=6237, debug=is_debug_mode)`
- **Recommendation**: Use gunicorn or uWSGI as the WSGI server. Example: `CMD ["gunicorn", "-w", "2", "-b", "0.0.0.0:6237", "app:app"]`. Add gunicorn to `requirements.txt`.

### Global Socket Timeout Manipulation
- **Impact**: The backend modifies the global default socket timeout via `socket.setdefaulttimeout()`, which affects all network operations in the process, not just feed fetching. In a multi-threaded environment (background worker + Flask), this can cause race conditions.
- **Evidence**:
  - `sentiment-api/app.py` lines 368-377: sets global timeout in request handler
  - `sentiment-api/background_worker.py` lines 157-166: sets global timeout in worker thread
- **Recommendation**: Use per-connection timeouts via feedparser's or requests' timeout parameters instead of modifying the global socket timeout.

### Fixed LED Array Size vs. Configurable LED Count
- **Impact**: The `ledColors` array is fixed at `DEFAULT_NUM_LEDS` (12) elements, but `numLeds` is configurable at runtime. If a user configures more than 12 LEDs, the code writes beyond the array bounds.
- **Evidence**:
  - `firmware/src/moodlight.cpp` line 148: `volatile uint32_t ledColors[DEFAULT_NUM_LEDS];` - fixed size 12
  - `firmware/src/moodlight.cpp` lines 443-449: loops `for (int i = 0; i < numLeds; i++)` writing to `ledColors[i]`
  - `firmware/src/moodlight.cpp` line 580: same pattern in `processLEDUpdates()`
- **Recommendation**: Either make `ledColors` dynamically allocated when `numLeds` changes, or add a bounds check: `min(numLeds, DEFAULT_NUM_LEDS)`. Alternatively, define a `MAX_LEDS` constant and validate `numLeds` against it.

### Duplicate System Health Check Logic
- **Impact**: Two separate health check timers run in the main loop with overlapping functionality, wasting resources and potentially triggering conflicting restart decisions.
- **Evidence**:
  - `firmware/src/moodlight.cpp` lines 4364-4472: Health check every `HEALTH_CHECK_INTERVAL` (1 hour) with restart logic
  - `firmware/src/moodlight.cpp` lines 4489-4499: Another health check every 300000ms (5 min) with its own restart logic
- **Recommendation**: Consolidate into a single health check routine with a clear interval.

## Low Priority

### feedconfig Endpoint Modifies Global State Non-Persistently
- **Impact**: The `/api/feedconfig` POST endpoint modifies the in-memory `rss_feeds` dict, but changes are lost on container restart. The background worker also has its own hardcoded feed list that is never modified.
- **Evidence**: `sentiment-api/app.py` lines 459-507: modifies `rss_feeds` global; not persisted to DB or file.
- **Recommendation**: Either persist feed configuration to the database or remove the endpoint. Currently it provides a false sense of configurability.

### Incomplete Dashboard and Logs Endpoints
- **Impact**: Two API endpoints return empty/placeholder data.
- **Evidence**:
  - `sentiment-api/app.py` line 349: `/api/dashboard` returns `jsonify({...})` - literal ellipsis
  - `sentiment-api/app.py` line 354: `/api/logs` returns `jsonify({...})` - literal ellipsis
- **Recommendation**: Implement or remove these endpoints. They will cause 500 errors if called.

### Temporary File Left in Web Data
- **Impact**: A `.tmp.html` file is committed to the web interface directory.
- **Evidence**: `firmware/data/setup.html.tmp.html` - appears to be an editing artifact.
- **Recommendation**: Delete `firmware/data/setup.html.tmp.html` and add `*.tmp.html` to `.gitignore`.

### No Test Suite
- **Impact**: Neither the firmware nor the backend API have any automated tests. Changes to sentiment calculation, API endpoints, or firmware logic cannot be validated without manual testing.
- **Evidence**: No test files found anywhere in the repository. `firmware/test/` contains only a `README`.
- **Recommendation**: Add unit tests for the backend (pytest for Flask endpoints, sentiment scoring logic) as a starting point. Firmware testing is harder but at least the settings serialization/deserialization could be tested on host.

### No Input Validation on Hardware Pin Settings
- **Impact**: Users can set arbitrary GPIO pin numbers via the web interface, potentially causing hardware damage or crashes.
- **Evidence**: `firmware/src/moodlight.cpp` lines 2488-2500: `ledPin` and `dhtPin` are set directly from JSON input with only type checking, no range validation.
- **Recommendation**: Add validation against valid ESP32 GPIO pins (avoid strapping pins, input-only pins for output use, etc.).

### OpenAI API Cost Not Bounded
- **Impact**: The background worker runs every 30 minutes, each time sending 12 headlines to GPT-4o-mini. There is no daily or monthly budget limit. A misconfiguration or runaway loop could generate unexpected costs.
- **Evidence**:
  - `sentiment-api/background_worker.py` line 66: `time.sleep(self.interval_seconds)` - runs continuously
  - `sentiment-api/app.py` lines 112-119: each call to OpenAI API with no rate limiting or budget tracking
- **Recommendation**: Add a daily API call counter with a configurable maximum. Log costs per call if possible.

### Binary Release Artifacts in Git
- **Impact**: The `releases/v9.0/` directory contains binary firmware files (`.bin`, `.tgz`) committed to git, bloating repository size.
- **Evidence**: `releases/v9.0/Firmware-9.0-AuraOS.bin`, `releases/v9.0/UI-9.0-AuraOS.tgz`
- **Recommendation**: Use GitHub Releases for binary distribution instead of committing to the repository. Add `releases/` to `.gitignore`.

### .env Not in .gitignore
- **Impact**: The `.gitignore` does not explicitly exclude `.env` files. While a `.env` file does not appear to be committed, the omission is risky for the `sentiment-api/` directory which requires `OPENAI_API_KEY` and `POSTGRES_PASSWORD`.
- **Evidence**: `/Users/simonluthe/Documents/auraos-moodlight/.gitignore` - no `.env` pattern present. `sentiment-api/.env.example` exists, implying `.env` is expected.
- **Recommendation**: Add `.env` and `sentiment-api/.env` to `.gitignore`.

## Open Questions
- Is the `analyse.godsapp.de` backend exposed to the public internet or only accessible from the local network? If public, the security concerns are critical.
- What is the expected number of concurrent ESP32 devices? The connection pool is sized at 5 (`database.py` line 48), which may be insufficient for 100+ devices mentioned in `moodlight_extensions.py`.
- The `sysHealth.isRestartRecommended()` criteria are not visible in the code explored - what conditions trigger a recommended restart? This drives automatic reboots.
- The `min_spiffs.csv` partition scheme (`platformio.ini` line 25) allocates minimal filesystem space - is this sufficient for 24 rotating system stat files plus settings, version files, and OTA staging?
