# External Integrations

**Analysis Date:** 2026-03-25

## APIs Consumed

### OpenAI API (GPT-4o-mini)
- **Purpose:** Sentiment analysis of German news headlines
- **SDK:** `openai==1.70.0` Python client
- **Model:** `gpt-4o-mini` with `temperature=0.1`
- **Auth:** `OPENAI_API_KEY` env var
- **Files:** `sentiment-api/app.py` (lines 72-156)
- **Request pattern:** Batch of headlines sent as structured prompt, returns numeric scores (-1.0 to +1.0)
- **Timeout:** 45 seconds per request
- **Rate:** Every 30 minutes via background worker

### German News RSS Feeds
- **Purpose:** Source headlines for sentiment analysis
- **Library:** `feedparser==6.0.11`
- **File:** `sentiment-api/app.py` (lines 55-68)
- **Sources (12 feeds):**
  - Zeit, Tagesschau, Sueddeutsche, FAZ, Die Welt, Handelsblatt
  - n-tv, Focus, Stern, Telekom (t-online), TAZ, Deutschlandfunk
- **Default:** 2 headlines per main source, 1 per total source

### Sentiment API (consumed by firmware)
- **Purpose:** ESP32 fetches current mood and history from backend
- **Base URL:** `http://analyse.godsapp.de` (configured in `firmware/src/config.h`)
- **Protocol:** HTTP GET with custom headers
- **Headers sent by device:**
  - `X-Device-ID` - Device identification
  - `X-Device-Name` - Human-readable name
  - `X-Firmware-Version` - Current firmware version
- **Endpoints consumed:**
  - `GET /api/moodlight/current` - Current sentiment score + metadata
  - `GET /api/moodlight/history?hours=168` - Historical data (default 7 days)
- **Files:** `firmware/src/moodlight.cpp`, `firmware/src/config.h`

### NTP Time Server
- **Purpose:** Time synchronization on ESP32
- **Server:** `pool.ntp.org`
- **Timezone:** GMT+1 with DST (+1h)
- **File:** `firmware/src/moodlight.cpp` (lines 131-134)

## APIs Exposed

### Sentiment API (Flask backend on port 6237)
- **File:** `sentiment-api/app.py`, `sentiment-api/moodlight_extensions.py`

**Core endpoints (for ESP32 devices):**
- `GET /api/moodlight/current` - Current sentiment with Redis cache (5 min TTL), device tracking
- `GET /api/moodlight/history` - Historical sentiment data from PostgreSQL
- `GET /api/moodlight/trend` - Sentiment trend analysis
- `GET /api/moodlight/stats` - Aggregated statistics
- `GET /api/moodlight/devices` - Registered device list
- `POST /api/moodlight/cache/clear` - Clear Redis cache

**Legacy/Admin endpoints:**
- `GET /` - Service info (JSON)
- `GET /api/health` - Health check
- `GET /api/dashboard` - Dashboard data
- `GET /api/logs` - Application logs
- `GET /api/news` - Trigger news analysis
- `POST /api/feedconfig` - Configure feed settings
- `GET /api/news/total_sentiment` - Total sentiment score

### ESP32 Web Server (port 80)
- **Purpose:** Local device configuration and monitoring
- **File:** `firmware/src/moodlight.cpp` (line 120)
- **Pages:**
  - `index.html` - Dashboard with system status and controls
  - `setup.html` - WiFi, MQTT, hardware configuration
  - `mood.html` - Sentiment statistics and history
  - `diagnostics.html` - System health monitoring
- **Features:** OTA firmware update, captive portal for initial setup

## MQTT / Home Assistant Integration

**Protocol:** MQTT via ArduinoHA library
**File:** `firmware/src/moodlight.cpp` (lines 98-117)
**Auth:** Configurable MQTT server, user, password (stored in ESP32 Preferences)
**Enable flag:** `mqttEnabled` boolean in device config

**Exposed HA Entities:**
| Entity | Type | ID | Purpose |
|--------|------|----|---------|
| Sentiment Score | HASensor | `sentiment_score` | Current score (-1.0 to +1.0), precision P2 |
| Temperature | HASensor | `temperature` | DHT22 reading, precision P1 |
| Humidity | HASensor | `humidity` | DHT22 reading, precision P0 |
| Light | HALight | `moodlight` | Brightness + RGB control |
| Mode | HASelect | `mode` | Auto/Manual mode selection |
| Refresh | HAButton | `refresh_sentiment` | Trigger immediate sentiment update |
| Update Interval | HANumber | `update_interval` | Mood update interval |
| DHT Interval | HANumber | `dht_interval` | Sensor read interval |
| Category | HASensor | `sentiment_category` | Text category (sehr negativ...sehr positiv) |
| Uptime | HASensor | `uptime` | Device uptime |
| WiFi Signal | HASensor | `wifi_signal` | RSSI value |
| System Status | HASensor | `system_status` | Overall health status |

**Heartbeat:** MQTT heartbeat every 60 seconds (`MQTT_HEARTBEAT_INTERVAL`)

## Data Storage

### PostgreSQL 16
- **Container:** `moodlight-postgres`
- **Connection:** `postgresql://moodlight:{password}@postgres:5432/moodlight`
- **Driver:** `psycopg2-binary` with `ThreadedConnectionPool` (1-5 connections)
- **Schema:** `sentiment-api/init.sql`
- **Tables:**
  - `sentiment_history` - All sentiment analysis results (score, category, metadata as JSONB)
  - `device_statistics` - Registered device tracking (MAC, firmware version, request count)
  - `device_requests` - Detailed per-device request logs
- **Host volume:** `/opt/stacks/auraos-moodlight/data/postgres`
- **Timezone:** `Europe/Berlin`

### Redis 7
- **Container:** `moodlight-redis`
- **Connection:** `redis://redis:6379/0`
- **Purpose:** Response caching with 5-minute TTL
- **Config:** 256MB max memory, LRU eviction, AOF persistence
- **Host volume:** `/opt/stacks/auraos-moodlight/data/redis`

### ESP32 Local Storage
- **LittleFS** - Web interface files (HTML/CSS/JS)
- **Preferences (NVS)** - WiFi credentials, MQTT config, LED settings, custom colors, intervals

## CI/CD & Deployment

### GitHub Actions
- **Workflow:** `.github/workflows/build-sentiment-api.yml`
- **Trigger:** Push to `main` when `sentiment-api/**` changes, or manual dispatch
- **Registry:** GitHub Container Registry (`ghcr.io`)
- **Image:** `ghcr.io/{repo}/sentiment-api:latest` + SHA tag
- **Auto-deploy:** Portainer webhook triggered after successful build

### Manual Deployment Workflow
1. Push to GitHub
2. SSH to `server.godsapp.de`
3. `cd /opt/auraos-moodlight/sentiment-api/`
4. `git pull && docker-compose build && docker-compose up -d`

## Network Architecture

```
[ESP32 Device] --HTTP--> [analyse.godsapp.de:6237] (Flask API)
      |                         |
      |--MQTT--> [MQTT Broker] --> [Home Assistant]
      |
      |--HTTP:80--> [Local Web UI]
      |
      |--mDNS--> [Local network discovery]

[Flask API] --HTTPS--> [OpenAI API] (gpt-4o-mini)
     |
     |--HTTP--> [12x German RSS Feeds]
     |
     |--TCP:5432--> [PostgreSQL]
     |
     |--TCP:6379--> [Redis]
```

## Environment Variables

**Required (backend):**
- `OPENAI_API_KEY` - OpenAI API authentication
- `POSTGRES_PASSWORD` - Database password

**Optional (backend):**
- `DEFAULT_HEADLINES_PER_SOURCE` - Headlines per RSS source (default: 1)
- `FLASK_ENV` - Flask environment (production)
- `DATABASE_URL` - Full PostgreSQL connection string (constructed in docker-compose)
- `REDIS_URL` - Full Redis connection string (constructed in docker-compose)

**Device-side (stored in NVS):**
- WiFi SSID + password
- MQTT server, user, password
- API URL (default: `http://analyse.godsapp.de/api/moodlight/current`)

## Open Questions

- No authentication on the sentiment API endpoints; any device can query freely
- MQTT broker is user-configured; no default broker is specified
- The firmware uses HTTP (not HTTPS) to communicate with the backend API
- `requests` package in requirements.txt appears unused in current code (may be legacy)

---

*Integration audit: 2026-03-25*
