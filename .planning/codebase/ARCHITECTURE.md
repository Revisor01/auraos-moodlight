# Architecture

> Generated: 2026-03-25 | Focus: arch

## Pattern Overview

**Overall:** Two-tier IoT architecture with a cloud backend (Flask API + PostgreSQL + Redis) and embedded edge devices (ESP32 with NeoPixel LEDs).

**Key Characteristics:**
- ESP32 devices are thin clients that poll a centralized backend for pre-computed sentiment scores
- Backend performs all heavy lifting: RSS feed fetching, OpenAI GPT-4o-mini sentiment analysis, data persistence
- Communication is HTTP-only from device to backend (no WebSocket, no push)
- Home Assistant integration via MQTT runs independently on the ESP32
- Redis caching layer (5 min TTL) protects the backend from high device request volume

## System Components

### Backend: Sentiment Analysis API

**Location:** `sentiment-api/`
**Runtime:** Python 3.12 (Flask), Docker Compose
**URL:** `http://analyse.godsapp.de` (port 6237)

**Components:**
- `sentiment-api/app.py` - Flask application, RSS feed fetching, OpenAI sentiment analysis, route definitions
- `sentiment-api/moodlight_extensions.py` - Optimized device-facing API endpoints (`/api/moodlight/*`) with Redis caching
- `sentiment-api/database.py` - PostgreSQL wrapper (`Database` class) + Redis cache wrapper (`RedisCache` class), both singletons
- `sentiment-api/background_worker.py` - Threaded background worker that runs sentiment analysis every 30 minutes
- `sentiment-api/init.sql` - Database schema with tables, indexes, triggers, and views

**Infrastructure (Docker Compose):**
- `news-analyzer` - Flask app container (port 6237)
- `postgres` - PostgreSQL 16 Alpine (internal network only)
- `redis` - Redis 7 Alpine with 256MB max, LRU eviction, AOF persistence (internal network only)
- All connected via `moodlight-net` bridge network

### Firmware: ESP32 Moodlight Device

**Location:** `firmware/`
**Framework:** Arduino on ESP32 via PlatformIO
**Main file:** `firmware/src/moodlight.cpp` (~4400 lines, monolithic)

**Components:**
- `firmware/src/moodlight.cpp` - All device logic: WiFi, web server, MQTT, LED control, sentiment fetching
- `firmware/src/MoodlightUtils.h` / `.cpp` - Utility classes: WatchdogManager, MemoryMonitor, SafeFileOps, CSVBuffer, NetworkDiagnostics, SystemHealthCheck, TaskManager
- `firmware/src/config.h` - Hardware pin definitions, default values, API endpoint URLs, version string
- `firmware/data/` - Web interface files served from LittleFS filesystem

## Data Flow

### Primary: News to LED Color

1. **Background Worker** (`sentiment-api/background_worker.py`) runs every 30 minutes
2. Worker fetches 1 headline per source from 12 German RSS feeds (Zeit, Tagesschau, FAZ, etc.)
3. Headlines are sent as a batch to OpenAI GPT-4o-mini for sentiment scoring (-1.0 to +1.0 per headline)
4. Individual scores are averaged, then amplified via `tanh(avg * 2.0)` for more visual variance
5. Result is stored in PostgreSQL `sentiment_history` table and Redis cache is invalidated
6. ESP32 device polls `GET /api/moodlight/current` every 30 minutes (configurable)
7. Backend returns cached sentiment score (Redis TTL 5 min) or fetches from PostgreSQL
8. ESP32 maps score to 1 of 5 color categories via `mapSentimentToLED()`:
   - `>= 0.20` -> index 4 (Violet, "sehr positiv")
   - `>= 0.10` -> index 3 (Indigo, "positiv")
   - `>= -0.20` -> index 2 (Blue, "neutral")
   - `>= -0.50` -> index 1 (Orange, "negativ")
   - `< -0.50` -> index 0 (Red, "sehr negativ")
9. NeoPixel LEDs are updated with the corresponding color + optional pulse animation

### Secondary: Device Tracking

1. ESP32 sends `X-Device-ID`, `X-Device-Name`, `X-Firmware-Version` headers with each API request
2. Backend registers/updates device in `device_statistics` table via upsert
3. Device stats available at `/api/moodlight/devices` and `/api/moodlight/stats`

### Tertiary: Home Assistant Integration

1. ESP32 connects to configured MQTT broker using ArduinoHA library
2. Exposes entities: sentiment score, sentiment category, temperature, humidity, light control, mode selector, update intervals
3. HA can control: light on/off, brightness, RGB color, auto/manual mode, update intervals, manual refresh
4. MQTT heartbeat every 60 seconds with uptime, WiFi signal, system status

## Communication Patterns

**ESP32 -> Backend API:**
- Protocol: HTTP GET
- Endpoint: `http://analyse.godsapp.de/api/moodlight/current`
- Interval: 30 minutes (configurable via `moodUpdateInterval`, default `DEFAULT_MOOD_UPDATE_INTERVAL = 1800000ms`)
- Error handling: 5 consecutive failures -> neutral fallback mode, status LED turns red
- 1 hour without successful update -> forced neutral mode

**ESP32 -> MQTT Broker:**
- Protocol: MQTT via ArduinoHA library
- Optional (controlled by `mqttEnabled` flag)
- Reconnect with exponential backoff
- Heartbeat interval: 60 seconds

**ESP32 -> Local Clients (Web UI):**
- Protocol: HTTP (WebServer on port 80)
- Serves static files from LittleFS (`firmware/data/`)
- REST API endpoints for configuration, status, firmware updates
- Captive portal mode for initial WiFi setup (AP at 192.168.4.1)

**Backend Internal:**
- Flask app -> PostgreSQL: psycopg2 with ThreadedConnectionPool (1-5 connections), auto-reconnect
- Flask app -> Redis: redis-py with 5 min TTL for current sentiment cache
- Background worker -> Flask app: shares `analyze_headlines_openai_batch` function reference

## Layers (Firmware)

**Configuration Layer:**
- Purpose: Hardware defaults and compile-time constants
- Location: `firmware/src/config.h`
- Runtime settings stored in LittleFS JSON file (`/data/settings.json`) and ESP32 Preferences

**System Utilities Layer:**
- Purpose: Cross-cutting concerns (watchdog, memory, file I/O, network diagnostics)
- Location: `firmware/src/MoodlightUtils.h`, `firmware/src/MoodlightUtils.cpp`
- Depends on: Arduino, ESP-IDF, LittleFS, WiFi
- Used by: Main application (`moodlight.cpp`)

**Application Layer:**
- Purpose: All business logic (sentiment, LEDs, web server, MQTT, sensors)
- Location: `firmware/src/moodlight.cpp`
- Contains: Everything in one file - web routes, sentiment fetching, LED control, HA integration, captive portal, OTA updates
- Depends on: MoodlightUtils, all libraries

## Layers (Backend)

**API Layer:**
- Purpose: HTTP endpoints for devices and admin
- Location: `sentiment-api/app.py` (legacy + RSS analysis), `sentiment-api/moodlight_extensions.py` (optimized device endpoints)
- Key routes: `/api/moodlight/current`, `/api/moodlight/history`, `/api/moodlight/trend`, `/api/moodlight/stats`, `/api/moodlight/devices`

**Analysis Layer:**
- Purpose: RSS feed fetching and OpenAI sentiment analysis
- Location: `sentiment-api/app.py` (functions `analyze_sentiment_openai`, `analyze_headlines_openai_batch`)
- Called by: Background worker and legacy `/api/news` endpoint

**Persistence Layer:**
- Purpose: Database and cache operations
- Location: `sentiment-api/database.py`
- Classes: `Database` (PostgreSQL), `RedisCache` (Redis)
- Singleton pattern via `get_database()` and `get_cache()`

**Background Processing Layer:**
- Purpose: Periodic sentiment updates
- Location: `sentiment-api/background_worker.py`
- Class: `SentimentUpdateWorker` (daemon thread, 30 min interval)

## Key Abstractions

**Sentiment Score:**
- Float value from -1.0 to +1.0
- Computed as `tanh(raw_average * 2.0)` where raw_average is mean of per-headline GPT-4o-mini scores
- Stored in PostgreSQL with category, headline count, metadata
- Categories: "sehr negativ", "negativ", "neutral", "positiv", "sehr positiv"

**LED Color Index:**
- Integer 0-4 mapped from sentiment score via `mapSentimentToLED()`
- Maps to configurable `customColors[5]` array (default: Red, Orange, Blue, Indigo, Violet)
- Thread-safe LED updates via FreeRTOS mutex (`ledMutex`)

**JSON Buffer Pool:**
- Pre-allocated char buffers (2 x 16KB) to avoid heap fragmentation on ESP32
- Mutex-protected acquire/release with heap fallback

**Device Operating Modes:**
- Config Mode: Captive portal AP for initial WiFi setup (5 min timeout)
- Auto Mode: LED color driven by sentiment score from backend
- Manual Mode: LED color/brightness controlled via HA or web UI

## Entry Points

**Backend:**
- Location: `sentiment-api/app.py` (line 559: `if __name__ == '__main__':`)
- Starts Flask app on port 6237, launches background worker
- Docker CMD: `python app.py`

**Firmware:**
- Location: `firmware/src/moodlight.cpp` (line 4019: `void setup()`)
- Arduino setup/loop pattern
- Setup sequence: Serial -> Watchdog -> BT disable -> WiFi config -> LittleFS -> Utils init -> Load settings -> Web server -> WiFi connect -> NTP -> MQTT -> HA

## Error Handling

**Backend Strategy:**
- Try/except around all endpoint handlers with logging and JSON error responses
- Database auto-reconnect with 3 retry attempts and 1s delay
- Background worker catches all exceptions, continues on next cycle
- RSS feed timeout (10s) with per-feed error isolation

**Firmware Strategy:**
- Exponential backoff for WiFi reconnect (5s base, 5min max)
- Consecutive failure counter for sentiment API (5 failures -> neutral fallback)
- 1-hour absolute timeout for successful sentiment update
- Watchdog timer (30s) with auto-feed
- Memory monitoring with heap tracking
- Safe file operations with backup and retry (SafeFileOps)
- Status LED indicates error state (red blink = API error, blue blink = WiFi connecting)

## Deployment Architecture

**Backend (Docker on Hetzner server):**
- Server: `server.godsapp.de` at `/opt/auraos-moodlight/sentiment-api/`
- CI: GitHub Actions builds Docker image on push to `main` (path filter: `sentiment-api/**`)
- Image pushed to GHCR (`ghcr.io/{repo}/sentiment-api`)
- Portainer webhook triggers deployment
- Persistent volumes: PostgreSQL at `/opt/stacks/auraos-moodlight/data/postgres`, Redis at `/opt/stacks/auraos-moodlight/data/redis`

**Firmware (OTA or USB):**
- Build: `pio run` in `firmware/` directory
- Release: `./build-release.sh` creates versioned firmware binary + UI .tgz in `releases/vX.X/`
- OTA: Upload firmware .bin and UI .tgz via `http://<device-ip>/diagnostics.html`
- USB: `pio run -t upload` + `pio run -t uploadfs`

## Cross-Cutting Concerns

**Logging (Backend):** Python `logging` module, INFO level default, DEBUG in development
**Logging (Firmware):** Custom `debug()` function writing to ring buffer (20 entries) + Serial, accessible via `/logs` endpoint
**Validation (Backend):** Input parameter parsing with type coercion and defaults, sentiment score clamped to [-1.0, 1.0]
**Validation (Firmware):** `constrain()` on all numeric inputs, URL parameter type checking
**Authentication:** None - all endpoints are open (no auth on API, no auth on device web UI)
**Time:** NTP sync on ESP32 (pool.ntp.org, CET timezone), PostgreSQL uses `Europe/Berlin` timezone

## Open Questions

- The RSS feed list is duplicated in `sentiment-api/app.py` (line 55) and `sentiment-api/background_worker.py` (line 137) - should be a shared constant
- `sentiment-api/app.py` has `/api/dashboard` and `/api/logs` endpoints that return empty `{...}` - appear to be stubs
- No authentication on any endpoint - the API and device web UI are fully open
- The `device_requests` table defined in `init.sql` does not appear to be used by any code
