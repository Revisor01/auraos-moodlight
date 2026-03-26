# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-based smart moodlight project called "AuraOS" that analyzes world sentiment through news APIs and adjusts LED colors accordingly. The device integrates with Home Assistant and provides a comprehensive web interface for configuration and monitoring.

## Build and Development Commands

**PlatformIO commands:**
- `platformio run` or `pio run` - Build the project
- `platformio run --target upload` or `pio run -t upload` - Build and upload to device  
- `platformio run --target uploadfs` or `pio run -t uploadfs` - Upload filesystem (web interface files)
- `platformio device monitor` or `pio device monitor` - Serial monitor for debugging
- `platformio run --target clean` or `pio run -t clean` - Clean build files

**File system operations:**
- Web interface files are stored in `data/` directory and uploaded via `uploadfs` target
- Configuration uses LittleFS filesystem on ESP32

## Architecture and Key Components

### Core Structure
- **Main application**: `src/moodlight.cpp` - Central application logic with sentiment analysis and LED control
- **Utilities library**: `src/MoodlightUtils.h/.cpp` - Comprehensive utility classes for system management
- **Configuration**: `src/config.h` - Hardware pins, default values, and version info
- **Web interface**: `data/` directory contains HTML/CSS/JS for device configuration

### Key Classes and Systems

**MoodlightUtils library provides:**
- `WatchdogManager` - ESP32 watchdog management and task monitoring
- `MemoryMonitor` - Heap memory tracking and leak detection
- `SafeFileOps` - Robust file operations with backup and retry logic
- `CSVBuffer` - Buffered CSV data logging for sentiment history
- `NetworkDiagnostics` - WiFi signal analysis and network health checks
- `SystemHealthCheck` - Overall system status monitoring
- `TaskManager` - FreeRTOS task creation and management

**Main Application Features:**
- Sentiment analysis from external news API (`analyse.godsapp.de`)
- NeoPixel LED strip control with color transitions
- Home Assistant MQTT integration using ArduinoHA library
- DHT sensor for temperature/humidity monitoring
- Captive portal setup mode for initial WiFi configuration
- Web-based configuration interface with dark/light theme
- Firmware update via web interface using ESP32-targz
- CSV data export and import functionality

### Hardware Configuration
- **Target**: ESP32 development board
- **LED Strip**: NeoPixel on pin 26 (configurable)
- **DHT Sensor**: Pin 17 (configurable)
- **Default LED count**: 12 LEDs
- **Filesystem**: LittleFS for web files and configuration storage

### Web Interface Structure
- `index.html` - Main dashboard with system status and controls
- `setup.html` - Configuration interface for WiFi, MQTT, and hardware settings
- `mood.html` - Sentiment statistics and historical data visualization
- `diagnostics.html` - System health monitoring and troubleshooting
- CSS and JavaScript files in respective subdirectories

### Development Notes
- Uses ArduinoJSON 7.4.0 for JSON parsing with optimized buffer management
- Implements JSON buffer pooling to reduce memory fragmentation
- German language interface and comments throughout codebase
- Comprehensive error handling and system recovery mechanisms
- Modular design with separation of concerns between utility classes and main application

## Backend API Server

The sentiment analysis backend is located in `sentiment-api/` directory:

**Production Deployment:**
- Server: `ssh root@server.godsapp.de`
- Location: `/opt/auraos-moodlight/sentiment-api/`
- URL: `http://analyse.godsapp.de`

**Deployment Workflow:**
1. Make changes locally in `sentiment-api/` directory
2. Commit and push to GitHub: `git add . && git commit -m "message" && git push`
3. SSH to server: `ssh root@server.godsapp.de`
4. Navigate to project: `cd /opt/auraos-moodlight/sentiment-api/`
5. Pull changes: `git pull`
6. Rebuild and restart: `docker-compose build && docker-compose up -d`
7. Check logs: `docker-compose logs -f news-analyzer`

**Backend Stack:**
- Flask API with OpenAI GPT-4o-mini for sentiment analysis
- PostgreSQL for persistent storage
- Redis for caching (5 min TTL)
- Docker Compose orchestration
- Background worker for automatic sentiment updates (30 min interval)

**API Endpoints:**
- `/` - Service info (JSON)
- `/api/moodlight/current` - Current sentiment data
- `/api/moodlight/history?hours=168` - Historical data (default: last 7 days)

<!-- GSD:project-start source:PROJECT.md -->
## Project

**AuraOS Moodlight**

Ein ESP32-basiertes Smart-Moodlight, das die Weltstimmung durch Nachrichtenanalyse visualisiert. Ein Backend (Flask + PostgreSQL + Redis) analysiert deutsche RSS-Feeds mit GPT-4o-mini und berechnet einen Sentiment-Score (-1.0 bis +1.0). Der ESP32 pollt den Score alle 30 Minuten und stellt ihn als LED-Farbe dar (Rot = sehr negativ bis Violett = sehr positiv). Integration in Home Assistant via MQTT, Web-Interface zur Konfiguration. Privates Projekt, ein Gerät im Wohnzimmer.

**Core Value:** Die Firmware ist modular aufgebaut — jedes Modul hat eine klare Verantwortung, ist einzeln lesbar und änderbar, ohne den Rest des Systems zu verstehen.

### Constraints

- **Hardware**: ESP32 mit 4MB Flash, ~320KB RAM — Speichereffizienz kritisch
- **Partition**: min_spiffs.csv — maximaler Flash für App, minimaler Filesystem-Speicher
- **Framework**: Arduino + PlatformIO — kein Wechsel zu ESPHome geplant
- **Deployment**: OTA über Web-Interface, kein physischer Zugang zum Gerät nötig
- **Backend**: Docker Compose auf Hetzner-Server, CI/CD über GitHub Actions + Portainer
<!-- GSD:project-end -->

<!-- GSD:stack-start source:codebase/STACK.md -->
## Technology Stack

## Languages
- C++ (Arduino/ESP-IDF) - Firmware in `firmware/src/` (~6040 lines)
- Python 3.12 - Backend API in `sentiment-api/` (~4 modules)
- HTML/CSS/JavaScript - Web interface in `firmware/data/`
- SQL - Database schema in `sentiment-api/init.sql`
## Runtime
- ESP32 (Espressif32 platform via PlatformIO)
- Arduino framework on ESP-IDF/FreeRTOS
- LittleFS filesystem for config and web files
- Python 3.12-slim (Docker container)
- Flask WSGI server (direct `python app.py`, no gunicorn)
- Background worker thread for periodic sentiment updates
## Build Tools
- PlatformIO - Build, upload, filesystem upload, serial monitor
- Config: `firmware/platformio.ini`
- Board: `esp32dev`
- Partition scheme: `min_spiffs.csv` (more flash for app, less for SPIFFS)
- Build flags: `-Os` (size optimization), `-DNDEBUG`, `-DCORE_DEBUG_LEVEL=0`
- Docker + Docker Compose (version 3.8)
- Config: `sentiment-api/docker-compose.yaml`, `sentiment-api/Dockerfile`
- GitHub Actions for GHCR image builds: `.github/workflows/build-sentiment-api.yml`
## Firmware Dependencies
- `adafruit/Adafruit NeoPixel@^1.12.5` - WS2812B LED strip control
- `bblanchon/ArduinoJson@^7.4.0` - JSON parsing with buffer pooling
- `dawidchyrzynski/home-assistant-integration@^2.1.0` - ArduinoHA MQTT library
- `adafruit/DHT sensor library@^1.4.6` - Temperature/humidity sensor
- `tobozo/ESP32-targz@^1.2.7` - OTA firmware update via .tar.gz
- WiFi, HTTPClient, WebServer, DNSServer - Networking
- Preferences - NVS key-value storage
- LittleFS - Flash filesystem
- ESPmDNS - mDNS/Bonjour discovery
- FreeRTOS - Task management, semaphores
- esp_task_wdt - Watchdog timer
## Backend Dependencies
- `Flask==3.1.0` - HTTP API framework
- `feedparser==6.0.11` - RSS feed parsing
- `requests==2.32.2` - HTTP client (unused directly, likely legacy)
- `openai==1.70.0` - OpenAI API client (GPT-4o-mini)
- `psycopg2-binary==2.9.9` - PostgreSQL driver
- `redis==5.0.1` - Redis client
- `postgres:16-alpine` - PostgreSQL 16 database
- `redis:7-alpine` - Redis 7 cache (256MB, LRU eviction, AOF persistence)
## Configuration
- Hardware pins and defaults: `firmware/src/config.h`
- Runtime config stored in ESP32 NVS (Preferences library)
- Web interface files served from LittleFS (`firmware/data/`)
- `.env` file in `sentiment-api/` (see `.env.example` for template)
- Required env vars: `OPENAI_API_KEY`, `POSTGRES_PASSWORD`
- Optional: `DEFAULT_HEADLINES_PER_SOURCE`, `REDIS_MAX_MEMORY`
- Database URL constructed in docker-compose: `postgresql://moodlight:{POSTGRES_PASSWORD}@postgres:5432/moodlight`
- Redis URL: `redis://redis:6379/0`
## Platform Requirements
- PlatformIO CLI or IDE plugin
- USB connection to ESP32 dev board
- ESP32 with 4MB flash (min_spiffs partition)
- Docker and Docker Compose
- OpenAI API key with GPT-4o-mini access
- Server: `server.godsapp.de` at `/opt/auraos-moodlight/sentiment-api/`
- Host volumes: `/opt/stacks/auraos-moodlight/data/postgres` and `/opt/stacks/auraos-moodlight/data/redis`
- Port: 6237 (Flask API)
- GitHub Actions builds Docker image to GHCR on push to `main`
- Portainer webhook triggers auto-deploy after successful build
## Version Info
- Firmware version: `9.0` (defined as `MOODLIGHT_VERSION` in `firmware/src/config.h`)
- Product name: `AuraOS`
- API version label: `v9.1` (per recent commit history)
<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->
## Conventions

## Language in Code
- **Comments:** German for user-facing/log messages, English for technical inline comments
- **Debug/log messages:** German (e.g., `"Einstellungen geladen:"`, `"Starte Access Point Modus..."`)
- **Variable names:** English (e.g., `moodUpdateInterval`, `wifiConfigured`, `lightOn`)
- **Class names:** English (e.g., `WatchdogManager`, `MemoryMonitor`, `SafeFileOps`)
- **Function names:** English (e.g., `saveSettings()`, `loadSettings()`, `handleStaticFile()`)
- **Python docstrings:** German (e.g., `"""Bestimmt Kategorie aus Sentiment-Score"""`)
- **Web interface:** German (all UI text, button labels, error messages)
## Naming Patterns
### C++ (Firmware)
- PascalCase for library files: `MoodlightUtils.h`, `MoodlightUtils.cpp`
- camelCase for main application: `moodlight.cpp`
- ALL_CAPS for config: `config.h`
- PascalCase: `WatchdogManager`, `MemoryMonitor`, `SafeFileOps`, `CSVBuffer`, `NetworkDiagnostics`, `SystemHealthCheck`, `TaskManager`
- Underscore-prefixed private members: `_lastFeedTime`, `_isEnabled`, `_monitoredTask`
- camelCase public methods: `begin()`, `feed()`, `autoFeed()`, `analyzeStack()`
- camelCase: `lastMoodUpdate`, `wifiConfigured`, `mqttEnabled`, `ledPin`
- ALL_CAPS for constants/defines: `DEFAULT_LED_PIN`, `JSON_BUFFER_SIZE`, `MAX_RECONNECT_DELAY`
- camelCase: `saveSettings()`, `loadSettings()`, `startAPMode()`, `handleStaticFile()`
- Prefix `handle` for HTTP handlers: `handleUiUpload()`, `handleStaticFile()`
- Prefix `setup` for initialization: `setupHA()`, `setupWebServer()`
- Prefix `update` for periodic operations: `updateLEDs()`, `updateStatusLED()`
- Prefix `get`/`set` for accessors: `getCurrentUiVersion()`, `getColorDefinition()`
- PascalCase: `ColorDefinition`, `JsonBufferPool`
### Python (Backend API)
- snake_case: `background_worker.py`, `moodlight_extensions.py`, `database.py`
- PascalCase: `Database`, `RedisCache`, `SentimentUpdateWorker`
- snake_case: `get_database()`, `analyze_sentiment_openai()`, `get_headlines_per_source()`
- Underscore-prefixed private: `_ensure_connection()`, `_perform_update()`, `_worker_loop()`
- ALL_CAPS: `MAX_RECONNECT_ATTEMPTS`, `RECONNECT_DELAY_SECONDS`, `DEFAULT_HEADLINES_PER_SOURCE_MAIN`
### JavaScript (Web Interface)
- camelCase: `toggleDarkMode()`, `refreshLog()`, `refreshStatus()`, `refreshSentiment()`
- camelCase: `refreshStatusInterval`, `refreshLogInterval`
### Web Files (HTML/CSS/JS)
## Code Style
### Formatting
### Linting
- No `.eslintrc`, `.prettierrc`, `biome.json`, or equivalent
- No `flake8`, `pylint`, `ruff`, or `mypy` configuration for Python
- No `clang-tidy` or `.clang-format` for C++
### Python Type Hints
## Import Organization
### C++ (Firmware)
### Python (Backend)
## Error Handling
### C++ Firmware Patterns
### Python Backend Patterns
## Logging
### C++ Firmware
### Python Backend
## Configuration Management
### Firmware Settings
- Defaults defined as `#define` in `firmware/src/config.h`
- Runtime values in global variables in `firmware/src/moodlight.cpp`
- Persisted via `saveSettings()` -> writes to both JSON + Preferences
- Loaded via `loadSettings()` -> tries JSON first, falls back to Preferences
- `moodInterval`, `dhtInterval`, `autoMode`, `lightOn`
- `wifiSSID`, `wifiPass`, `wifiConfigured`
- `apiUrl`, `mqttServer`, `mqttUser`, `mqttPass`
- `dhtPin`, `dhtEnabled`, `ledPin`, `numLeds`, `mqttEnabled`
- `color0` through `color4`
### Backend Configuration
- `OPENAI_API_KEY` - OpenAI API key
- `DATABASE_URL` - PostgreSQL connection string
- `REDIS_URL` - Redis connection string
- `FLASK_ENV` - development/production
- `DEFAULT_HEADLINES_PER_SOURCE` - optional override
## Memory Management (ESP32-Specific)
- Configured in `firmware/src/MoodlightUtils.h`
- `checkHeapBefore()` verifies sufficient memory before operations
- Persists lowest heap value in NVS for tracking across reboots
## Comment/Documentation Style
### C++ Headers
### C++ Methods
### Python
### Version Comments
## API Response Format
### Backend JSON Responses
### Firmware JSON Responses (WebServer)
## Module Design
### Firmware
### Backend
- `sentiment-api/app.py` - Main Flask app, routes, analysis logic
- `sentiment-api/database.py` - Database + Redis wrappers (singleton pattern)
- `sentiment-api/background_worker.py` - Background thread for periodic updates
- `sentiment-api/moodlight_extensions.py` - Additional API endpoints (registered via `register_moodlight_endpoints(app)`)
## Open Questions
- No `.editorconfig` file exists - consider adding one for consistent indentation
- The brace style is inconsistent in C++ code - needs a standard
- RSS feed list is duplicated between `app.py` and `background_worker.py` - should be shared
- `get_category_from_score()` is duplicated between `moodlight_extensions.py` and `background_worker.py`
<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->
## Architecture

## Pattern Overview
- ESP32 devices are thin clients that poll a centralized backend for pre-computed sentiment scores
- Backend performs all heavy lifting: RSS feed fetching, OpenAI GPT-4o-mini sentiment analysis, data persistence
- Communication is HTTP-only from device to backend (no WebSocket, no push)
- Home Assistant integration via MQTT runs independently on the ESP32
- Redis caching layer (5 min TTL) protects the backend from high device request volume
## System Components
### Backend: Sentiment Analysis API
- `sentiment-api/app.py` - Flask application, RSS feed fetching, OpenAI sentiment analysis, route definitions
- `sentiment-api/moodlight_extensions.py` - Optimized device-facing API endpoints (`/api/moodlight/*`) with Redis caching
- `sentiment-api/database.py` - PostgreSQL wrapper (`Database` class) + Redis cache wrapper (`RedisCache` class), both singletons
- `sentiment-api/background_worker.py` - Threaded background worker that runs sentiment analysis every 30 minutes
- `sentiment-api/init.sql` - Database schema with tables, indexes, triggers, and views
- `news-analyzer` - Flask app container (port 6237)
- `postgres` - PostgreSQL 16 Alpine (internal network only)
- `redis` - Redis 7 Alpine with 256MB max, LRU eviction, AOF persistence (internal network only)
- All connected via `moodlight-net` bridge network
### Firmware: ESP32 Moodlight Device
- `firmware/src/moodlight.cpp` - All device logic: WiFi, web server, MQTT, LED control, sentiment fetching
- `firmware/src/MoodlightUtils.h` / `.cpp` - Utility classes: WatchdogManager, MemoryMonitor, SafeFileOps, CSVBuffer, NetworkDiagnostics, SystemHealthCheck, TaskManager
- `firmware/src/config.h` - Hardware pin definitions, default values, API endpoint URLs, version string
- `firmware/data/` - Web interface files served from LittleFS filesystem
## Data Flow
### Primary: News to LED Color
### Secondary: Device Tracking
### Tertiary: Home Assistant Integration
## Communication Patterns
- Protocol: HTTP GET
- Endpoint: `http://analyse.godsapp.de/api/moodlight/current`
- Interval: 30 minutes (configurable via `moodUpdateInterval`, default `DEFAULT_MOOD_UPDATE_INTERVAL = 1800000ms`)
- Error handling: 5 consecutive failures -> neutral fallback mode, status LED turns red
- 1 hour without successful update -> forced neutral mode
- Protocol: MQTT via ArduinoHA library
- Optional (controlled by `mqttEnabled` flag)
- Reconnect with exponential backoff
- Heartbeat interval: 60 seconds
- Protocol: HTTP (WebServer on port 80)
- Serves static files from LittleFS (`firmware/data/`)
- REST API endpoints for configuration, status, firmware updates
- Captive portal mode for initial WiFi setup (AP at 192.168.4.1)
- Flask app -> PostgreSQL: psycopg2 with ThreadedConnectionPool (1-5 connections), auto-reconnect
- Flask app -> Redis: redis-py with 5 min TTL for current sentiment cache
- Background worker -> Flask app: shares `analyze_headlines_openai_batch` function reference
## Layers (Firmware)
- Purpose: Hardware defaults and compile-time constants
- Location: `firmware/src/config.h`
- Runtime settings stored in LittleFS JSON file (`/data/settings.json`) and ESP32 Preferences
- Purpose: Cross-cutting concerns (watchdog, memory, file I/O, network diagnostics)
- Location: `firmware/src/MoodlightUtils.h`, `firmware/src/MoodlightUtils.cpp`
- Depends on: Arduino, ESP-IDF, LittleFS, WiFi
- Used by: Main application (`moodlight.cpp`)
- Purpose: All business logic (sentiment, LEDs, web server, MQTT, sensors)
- Location: `firmware/src/moodlight.cpp`
- Contains: Everything in one file - web routes, sentiment fetching, LED control, HA integration, captive portal, OTA updates
- Depends on: MoodlightUtils, all libraries
## Layers (Backend)
- Purpose: HTTP endpoints for devices and admin
- Location: `sentiment-api/app.py` (legacy + RSS analysis), `sentiment-api/moodlight_extensions.py` (optimized device endpoints)
- Key routes: `/api/moodlight/current`, `/api/moodlight/history`, `/api/moodlight/trend`, `/api/moodlight/stats`, `/api/moodlight/devices`
- Purpose: RSS feed fetching and OpenAI sentiment analysis
- Location: `sentiment-api/app.py` (functions `analyze_sentiment_openai`, `analyze_headlines_openai_batch`)
- Called by: Background worker and legacy `/api/news` endpoint
- Purpose: Database and cache operations
- Location: `sentiment-api/database.py`
- Classes: `Database` (PostgreSQL), `RedisCache` (Redis)
- Singleton pattern via `get_database()` and `get_cache()`
- Purpose: Periodic sentiment updates
- Location: `sentiment-api/background_worker.py`
- Class: `SentimentUpdateWorker` (daemon thread, 30 min interval)
## Key Abstractions
- Float value from -1.0 to +1.0
- Computed as `tanh(raw_average * 2.0)` where raw_average is mean of per-headline GPT-4o-mini scores
- Stored in PostgreSQL with category, headline count, metadata
- Categories: "sehr negativ", "negativ", "neutral", "positiv", "sehr positiv"
- Integer 0-4 mapped from sentiment score via `mapSentimentToLED()`
- Maps to configurable `customColors[5]` array (default: Red, Orange, Blue, Indigo, Violet)
- Thread-safe LED updates via FreeRTOS mutex (`ledMutex`)
- Pre-allocated char buffers (2 x 16KB) to avoid heap fragmentation on ESP32
- Mutex-protected acquire/release with heap fallback
- Config Mode: Captive portal AP for initial WiFi setup (5 min timeout)
- Auto Mode: LED color driven by sentiment score from backend
- Manual Mode: LED color/brightness controlled via HA or web UI
## Entry Points
- Location: `sentiment-api/app.py` (line 559: `if __name__ == '__main__':`)
- Starts Flask app on port 6237, launches background worker
- Docker CMD: `python app.py`
- Location: `firmware/src/moodlight.cpp` (line 4019: `void setup()`)
- Arduino setup/loop pattern
- Setup sequence: Serial -> Watchdog -> BT disable -> WiFi config -> LittleFS -> Utils init -> Load settings -> Web server -> WiFi connect -> NTP -> MQTT -> HA
## Error Handling
- Try/except around all endpoint handlers with logging and JSON error responses
- Database auto-reconnect with 3 retry attempts and 1s delay
- Background worker catches all exceptions, continues on next cycle
- RSS feed timeout (10s) with per-feed error isolation
- Exponential backoff for WiFi reconnect (5s base, 5min max)
- Consecutive failure counter for sentiment API (5 failures -> neutral fallback)
- 1-hour absolute timeout for successful sentiment update
- Watchdog timer (30s) with auto-feed
- Memory monitoring with heap tracking
- Safe file operations with backup and retry (SafeFileOps)
- Status LED indicates error state (red blink = API error, blue blink = WiFi connecting)
## Deployment Architecture
- Server: `server.godsapp.de` at `/opt/auraos-moodlight/sentiment-api/`
- CI: GitHub Actions builds Docker image on push to `main` (path filter: `sentiment-api/**`)
- Image pushed to GHCR (`ghcr.io/{repo}/sentiment-api`)
- Portainer webhook triggers deployment
- Persistent volumes: PostgreSQL at `/opt/stacks/auraos-moodlight/data/postgres`, Redis at `/opt/stacks/auraos-moodlight/data/redis`
- Build: `pio run` in `firmware/` directory
- Release: `./build-release.sh` creates versioned firmware binary + UI .tgz in `releases/vX.X/`
- OTA: Upload firmware .bin and UI .tgz via `http://<device-ip>/diagnostics.html`
- USB: `pio run -t upload` + `pio run -t uploadfs`
## Cross-Cutting Concerns
## Open Questions
- The RSS feed list is duplicated in `sentiment-api/app.py` (line 55) and `sentiment-api/background_worker.py` (line 137) - should be a shared constant
- `sentiment-api/app.py` has `/api/dashboard` and `/api/logs` endpoints that return empty `{...}` - appear to be stubs
- No authentication on any endpoint - the API and device web UI are fully open
- The `device_requests` table defined in `init.sql` does not appear to be used by any code
<!-- GSD:architecture-end -->

<!-- GSD:workflow-start source:GSD defaults -->
## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:
- `/gsd:quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd:debug` for investigation and bug fixing
- `/gsd:execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->

<!-- GSD:profile-start -->
## Developer Profile

> Profile not yet configured. Run `/gsd:profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
