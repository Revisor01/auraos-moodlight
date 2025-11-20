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