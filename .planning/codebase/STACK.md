# Technology Stack

**Analysis Date:** 2026-03-25

## Languages

**Primary:**
- C++ (Arduino/ESP-IDF) - Firmware in `firmware/src/` (~6040 lines)
- Python 3.12 - Backend API in `sentiment-api/` (~4 modules)

**Secondary:**
- HTML/CSS/JavaScript - Web interface in `firmware/data/`
- SQL - Database schema in `sentiment-api/init.sql`

## Runtime

**Firmware:**
- ESP32 (Espressif32 platform via PlatformIO)
- Arduino framework on ESP-IDF/FreeRTOS
- LittleFS filesystem for config and web files

**Backend:**
- Python 3.12-slim (Docker container)
- Flask WSGI server (direct `python app.py`, no gunicorn)
- Background worker thread for periodic sentiment updates

## Build Tools

**Firmware:**
- PlatformIO - Build, upload, filesystem upload, serial monitor
- Config: `firmware/platformio.ini`
- Board: `esp32dev`
- Partition scheme: `min_spiffs.csv` (more flash for app, less for SPIFFS)
- Build flags: `-Os` (size optimization), `-DNDEBUG`, `-DCORE_DEBUG_LEVEL=0`

**Backend:**
- Docker + Docker Compose (version 3.8)
- Config: `sentiment-api/docker-compose.yaml`, `sentiment-api/Dockerfile`
- GitHub Actions for GHCR image builds: `.github/workflows/build-sentiment-api.yml`

**PlatformIO Commands:**
```bash
pio run                    # Build firmware
pio run -t upload          # Flash to device
pio run -t uploadfs        # Upload web interface (data/ dir)
pio device monitor         # Serial monitor (115200 baud)
pio run -t clean           # Clean build
```

## Firmware Dependencies

**Libraries (via `firmware/platformio.ini` lib_deps):**
- `adafruit/Adafruit NeoPixel@^1.12.5` - WS2812B LED strip control
- `bblanchon/ArduinoJson@^7.4.0` - JSON parsing with buffer pooling
- `dawidchyrzynski/home-assistant-integration@^2.1.0` - ArduinoHA MQTT library
- `adafruit/DHT sensor library@^1.4.6` - Temperature/humidity sensor
- `tobozo/ESP32-targz@^1.2.7` - OTA firmware update via .tar.gz

**ESP32 SDK Components (built-in):**
- WiFi, HTTPClient, WebServer, DNSServer - Networking
- Preferences - NVS key-value storage
- LittleFS - Flash filesystem
- ESPmDNS - mDNS/Bonjour discovery
- FreeRTOS - Task management, semaphores
- esp_task_wdt - Watchdog timer

## Backend Dependencies

**Python packages (via `sentiment-api/requirements.txt`):**
- `Flask==3.1.0` - HTTP API framework
- `feedparser==6.0.11` - RSS feed parsing
- `requests==2.32.2` - HTTP client (unused directly, likely legacy)
- `openai==1.70.0` - OpenAI API client (GPT-4o-mini)
- `psycopg2-binary==2.9.9` - PostgreSQL driver
- `redis==5.0.1` - Redis client

**Infrastructure (via Docker Compose):**
- `postgres:16-alpine` - PostgreSQL 16 database
- `redis:7-alpine` - Redis 7 cache (256MB, LRU eviction, AOF persistence)

## Configuration

**Firmware:**
- Hardware pins and defaults: `firmware/src/config.h`
- Runtime config stored in ESP32 NVS (Preferences library)
- Web interface files served from LittleFS (`firmware/data/`)

**Backend:**
- `.env` file in `sentiment-api/` (see `.env.example` for template)
- Required env vars: `OPENAI_API_KEY`, `POSTGRES_PASSWORD`
- Optional: `DEFAULT_HEADLINES_PER_SOURCE`, `REDIS_MAX_MEMORY`
- Database URL constructed in docker-compose: `postgresql://moodlight:{POSTGRES_PASSWORD}@postgres:5432/moodlight`
- Redis URL: `redis://redis:6379/0`

## Platform Requirements

**Firmware Development:**
- PlatformIO CLI or IDE plugin
- USB connection to ESP32 dev board
- ESP32 with 4MB flash (min_spiffs partition)

**Backend Development:**
- Docker and Docker Compose
- OpenAI API key with GPT-4o-mini access

**Production Deployment:**
- Server: `server.godsapp.de` at `/opt/auraos-moodlight/sentiment-api/`
- Host volumes: `/opt/stacks/auraos-moodlight/data/postgres` and `/opt/stacks/auraos-moodlight/data/redis`
- Port: 6237 (Flask API)
- GitHub Actions builds Docker image to GHCR on push to `main`
- Portainer webhook triggers auto-deploy after successful build

## Version Info

- Firmware version: `9.0` (defined as `MOODLIGHT_VERSION` in `firmware/src/config.h`)
- Product name: `AuraOS`
- API version label: `v9.1` (per recent commit history)

---

*Stack analysis: 2026-03-25*
