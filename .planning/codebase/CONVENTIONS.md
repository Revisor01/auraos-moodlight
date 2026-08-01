# Coding Conventions

> Generated: 2026-03-25 | Focus: quality

## Language in Code

**The codebase uses a mix of German and English.** Follow these rules:

- **Comments:** German for user-facing/log messages, English for technical inline comments
- **Debug/log messages:** German (e.g., `"Einstellungen geladen:"`, `"Starte Access Point Modus..."`)
- **Variable names:** English (e.g., `moodUpdateInterval`, `wifiConfigured`, `lightOn`)
- **Class names:** English (e.g., `WatchdogManager`, `MemoryMonitor`, `SafeFileOps`)
- **Function names:** English (e.g., `saveSettings()`, `loadSettings()`, `handleStaticFile()`)
- **Python docstrings:** German (e.g., `"""Bestimmt Kategorie aus Sentiment-Score"""`)
- **Web interface:** German (all UI text, button labels, error messages)

**Rule:** Keep variable/function names in English. Keep user-visible strings, logs, and comments in German.

## Naming Patterns

### C++ (Firmware)

**Files:**
- PascalCase for library files: `MoodlightUtils.h`, `MoodlightUtils.cpp`
- camelCase for main application: `moodlight.cpp`
- ALL_CAPS for config: `config.h`

**Classes:**
- PascalCase: `WatchdogManager`, `MemoryMonitor`, `SafeFileOps`, `CSVBuffer`, `NetworkDiagnostics`, `SystemHealthCheck`, `TaskManager`

**Class members:**
- Underscore-prefixed private members: `_lastFeedTime`, `_isEnabled`, `_monitoredTask`
- camelCase public methods: `begin()`, `feed()`, `autoFeed()`, `analyzeStack()`

**Global variables:**
- camelCase: `lastMoodUpdate`, `wifiConfigured`, `mqttEnabled`, `ledPin`
- ALL_CAPS for constants/defines: `DEFAULT_LED_PIN`, `JSON_BUFFER_SIZE`, `MAX_RECONNECT_DELAY`

**Functions (standalone):**
- camelCase: `saveSettings()`, `loadSettings()`, `startAPMode()`, `handleStaticFile()`
- Prefix `handle` for HTTP handlers: `handleUiUpload()`, `handleStaticFile()`
- Prefix `setup` for initialization: `setupHA()`, `setupWebServer()`
- Prefix `update` for periodic operations: `updateLEDs()`, `updateStatusLED()`
- Prefix `get`/`set` for accessors: `getCurrentUiVersion()`, `getColorDefinition()`

**Structs:**
- PascalCase: `ColorDefinition`, `JsonBufferPool`

### Python (Backend API)

**Files:**
- snake_case: `background_worker.py`, `moodlight_extensions.py`, `database.py`

**Classes:**
- PascalCase: `Database`, `RedisCache`, `SentimentUpdateWorker`

**Functions/methods:**
- snake_case: `get_database()`, `analyze_sentiment_openai()`, `get_headlines_per_source()`
- Underscore-prefixed private: `_ensure_connection()`, `_perform_update()`, `_worker_loop()`

**Constants:**
- ALL_CAPS: `MAX_RECONNECT_ATTEMPTS`, `RECONNECT_DELAY_SECONDS`, `DEFAULT_HEADLINES_PER_SOURCE_MAIN`

### JavaScript (Web Interface)

**Functions:**
- camelCase: `toggleDarkMode()`, `refreshLog()`, `refreshStatus()`, `refreshSentiment()`

**Variables:**
- camelCase: `refreshStatusInterval`, `refreshLogInterval`

### Web Files (HTML/CSS/JS)

**HTML files:** lowercase: `index.html`, `setup.html`, `mood.html`, `diagnostics.html`
**CSS files:** lowercase: `style.css`, `mood.css`
**JS files:** lowercase: `script.js`, `setup.js`, `mood.js`
**Directory structure:** `data/css/`, `data/js/`

## Code Style

### Formatting

**No automated formatter configured.** The codebase does not use clang-format, Prettier, or any linting/formatting tool.

**C++ indentation:** 4 spaces (observed in `MoodlightUtils.cpp`, `moodlight.cpp`)
**Python indentation:** 4 spaces (observed in `app.py`, `database.py`)
**JavaScript indentation:** 2 spaces (observed in `script.js`)
**Brace style:** K&R / Allman mix - both styles appear in `firmware/src/moodlight.cpp`:
```cpp
// Sometimes opening brace on same line:
void updateLEDs() {
    if (!lightOn) {

// Sometimes on next line (especially for multi-line conditions):
if (wifiConfigured || wifiSSID.isEmpty())
{
    debug(F("Keine WiFi-Konfiguration vorhanden."));
```

**Rule for new code:** Use opening brace on same line (K&R style) for consistency with newer code sections.

### Linting

**No linting tools configured** for any part of the project:
- No `.eslintrc`, `.prettierrc`, `biome.json`, or equivalent
- No `flake8`, `pylint`, `ruff`, or `mypy` configuration for Python
- No `clang-tidy` or `.clang-format` for C++

### Python Type Hints

The Python backend uses type hints selectively:
```python
# database.py uses type hints consistently:
def save_sentiment(self, sentiment_score: float, category: str, headlines_analyzed: int, ...) -> int:
def get_latest_sentiment(self) -> Optional[Dict[str, Any]]:

# app.py uses them less:
def analyze_sentiment_openai(headlines_batch: list) -> list:
```

**Rule:** Use type hints for all Python function signatures. Use `Optional`, `Dict`, `List` from `typing`.

## Import Organization

### C++ (Firmware)

Order in `firmware/src/moodlight.cpp`:
1. Arduino core: `#include <Arduino.h>`
2. Third-party libraries: `<Adafruit_NeoPixel.h>`, `<ArduinoJson.h>`, `<ArduinoHA.h>`
3. ESP-IDF / system headers: `<WiFi.h>`, `<HTTPClient.h>`, `"esp_log.h"`, `"esp_wifi.h"`
4. FreeRTOS: `"freertos/FreeRTOS.h"`, `"freertos/task.h"`
5. Filesystem: `"LittleFS.h"`
6. Local headers: `"config.h"`, `"MoodlightUtils.h"`

Note: `MoodlightUtils.h` is included late in the file (line 314 of `moodlight.cpp`), after function definitions. This is unusual and exists because utility classes depend on the `debug()` function defined in main.

### Python (Backend)

Order in `sentiment-api/app.py`:
1. Standard library: `os`, `re`, `math`, `socket`, `logging`
2. Third-party: `flask`, `feedparser`, `openai`
3. Local modules: `from database import ...`, `from moodlight_extensions import ...`

## Error Handling

### C++ Firmware Patterns

**Debug logging with severity prefixes:**
```cpp
// Normal messages:
debug(F("Einstellungen geladen:"));

// Warnings (checked in release mode):
debug(F("WARNUNG: Kein JSON-Puffer verfügbar, verwende Heap!"));

// Errors (always printed in release mode):
debug("ERROR: ...");
debug("CRITICAL: ...");
```

In release mode (`#ifndef DEBUG_MODE`), only messages starting with `"ERROR:"` or `"CRITICAL:"` are printed. Use these prefixes for important messages.

**Fallback chains:**
```cpp
// Settings: JSON file -> Preferences -> Defaults
bool fileLoadSuccess = loadSettingsFromFile();
if (!fileLoadSuccess) {
    // Fallback to Preferences (legacy storage)
}
```

**Retry with safe file operations:**
```cpp
// SafeFileOps::readFile with maxRetries parameter
String readFile(const char* path, int maxRetries = 3);
```

**Watchdog feeding during long operations:**
```cpp
MoodlightUtils::safeDelay(ms);  // Delay that feeds watchdog
watchdog.autoFeed(15000);        // Auto-feed if interval elapsed
```

**Semaphore-guarded LED access:**
```cpp
if (xSemaphoreTake(ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
    // ... modify LED state ...
    xSemaphoreGive(ledMutex);
}
```

### Python Backend Patterns

**Try/except with logging and fallback return values:**
```python
try:
    # ... operation ...
except Exception as e:
    logger.error(f"Fehler beim Speichern der Sentiment-Daten: {e}")
    raise  # Or return default value

# For API endpoints, always return JSON with status:
return jsonify({"status": "error", "message": "..."}), 500
```

**Database auto-reconnect:**
```python
def _ensure_connection(self) -> bool:
    for attempt in range(MAX_RECONNECT_ATTEMPTS):
        try:
            # test connection, reconnect if needed
        except Exception as e:
            if attempt < MAX_RECONNECT_ATTEMPTS - 1:
                time.sleep(RECONNECT_DELAY_SECONDS)
```

**Context manager for cursor safety:**
```python
with self.get_cursor(cursor_factory=RealDictCursor) as cur:
    cur.execute(query, params)
```

## Logging

### C++ Firmware

**Framework:** Custom `debug()` function writing to Serial + ring buffer.

```cpp
// Two overloads:
void debug(const String &message);           // Dynamic strings
void debug(const __FlashStringHelper *message);  // Flash strings (save RAM)
```

**Pattern:** Use `F()` macro for static strings to save RAM:
```cpp
debug(F("Statischer Text"));                    // Good - stored in Flash
debug(String(F("Prefix: ")) + variable);        // Good - only variable on heap
debug("Einstellungen geladen:");                 // Avoid - wastes RAM
```

**Ring buffer:** Last 20 log entries stored in `logBuffer[]`, accessible via `/logs` HTTP endpoint.

**Debug mode:** Controlled by `#define DEBUG_MODE` in `firmware/src/moodlight.cpp`. When disabled, only `ERROR:` and `CRITICAL:` prefixed messages print.

### Python Backend

**Framework:** Python `logging` module.

```python
# Module-level logger:
logger = logging.getLogger(__name__)

# Configuration in app.py:
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
```

**Pattern:** Use `logger.info()` for operations, `logger.error()` with `exc_info=True` for exceptions, `logger.debug()` for verbose output.

## Configuration Management

### Firmware Settings

**Dual storage with migration:** Settings stored in both JSON file (`/data/settings.json` on LittleFS) and ESP32 Preferences (NVS). JSON is primary, Preferences is fallback.

- Defaults defined as `#define` in `firmware/src/config.h`
- Runtime values in global variables in `firmware/src/moodlight.cpp`
- Persisted via `saveSettings()` -> writes to both JSON + Preferences
- Loaded via `loadSettings()` -> tries JSON first, falls back to Preferences

**Key configuration keys** (JSON property names are abbreviated):
- `moodInterval`, `dhtInterval`, `autoMode`, `lightOn`
- `wifiSSID`, `wifiPass`, `wifiConfigured`
- `apiUrl`, `mqttServer`, `mqttUser`, `mqttPass`
- `dhtPin`, `dhtEnabled`, `ledPin`, `numLeds`, `mqttEnabled`
- `color0` through `color4`

### Backend Configuration

**Environment variables only.** No config files. Key vars:
- `OPENAI_API_KEY` - OpenAI API key
- `DATABASE_URL` - PostgreSQL connection string
- `REDIS_URL` - Redis connection string
- `FLASK_ENV` - development/production
- `DEFAULT_HEADLINES_PER_SOURCE` - optional override

Reference: `sentiment-api/.env.example` (exists but not read for security)

## Memory Management (ESP32-Specific)

**JSON buffer pool** to reduce heap fragmentation:
```cpp
// Pre-allocated buffers (2 x 16KB) in firmware/src/moodlight.cpp
struct JsonBufferPool {
    char buffers[JSON_BUFFER_COUNT][JSON_BUFFER_SIZE];  // 2 x 16384 bytes
    bool inUse[JSON_BUFFER_COUNT];
    SemaphoreHandle_t mutex;
};
```

**Flash strings** (`F()` macro) for all static debug strings to keep them in program memory instead of RAM.

**Static buffers** in debug function to avoid repeated allocations:
```cpp
static char timeBuffer[16];
static char messageBuffer[256];
```

**MemoryMonitor** class tracks heap usage and warns when low:
- Configured in `firmware/src/MoodlightUtils.h`
- `checkHeapBefore()` verifies sufficient memory before operations
- Persists lowest heap value in NVS for tracking across reboots

**FreeRTOS semaphores** for thread safety (LED access via `ledMutex`).

## Comment/Documentation Style

### C++ Headers
Section separators with `=====`:
```cpp
// ===== WATCHDOG-MANAGEMENT =====
// ===== SPEICHER-UEBERWACHUNG =====
// ===== SICHERE DATEIOPERATIONEN =====
```

### C++ Methods
German comments above methods describing purpose:
```cpp
// Initialisiere den Watchdog mit einer bestimmten Timeout-Zeit
bool begin(uint32_t timeoutSeconds = 30, bool panicOnTimeout = false);

// Fuettere den Watchdog (sollte regelmaessig aufgerufen werden)
void feed();
```

### Python
Module-level docstrings in German:
```python
"""
Database Layer fuer Moodlight System
PostgreSQL + Redis Integration mit robustem Connection-Handling
"""
```

Method docstrings with Args/Returns in German:
```python
def save_sentiment(self, sentiment_score: float, ...) -> int:
    """
    Speichere Sentiment-Analyse in Datenbank

    Args:
        sentiment_score: Sentiment-Wert (-1.0 bis +1.0)
    Returns:
        ID des eingefuegten Datensatzes
    """
```

### Version Comments
Changes from previous versions are documented inline:
```cpp
// v9.0: headlinesPS removed - only for legacy API endpoints
// v9.0: CSVBuffer removed - stats from backend
// v9.0: archiveTask removed - archiving handled in backend
```

## API Response Format

### Backend JSON Responses

Always include `"status"` field:
```json
{
    "status": "success",
    "total_sentiment": -0.25,
    "statistics": { ... }
}
```

Error responses include `"message"`:
```json
{
    "status": "error",
    "message": "Keine Sentiment-Daten verfuegbar"
}
```

### Firmware JSON Responses (WebServer)

Use `ArduinoJson` `JsonDocument` for building responses. Serialize with `serializeJson()`.

## Module Design

### Firmware

**Single large file architecture:** `firmware/src/moodlight.cpp` is 4500 lines containing all application logic (web server handlers, MQTT, WiFi, LED control, settings management).

**Utility library extracted:** `firmware/src/MoodlightUtils.h/.cpp` contains reusable system management classes.

**No barrel files or module exports** - C++ include-based.

### Backend

**Modular Flask architecture:**
- `sentiment-api/app.py` - Main Flask app, routes, analysis logic
- `sentiment-api/database.py` - Database + Redis wrappers (singleton pattern)
- `sentiment-api/background_worker.py` - Background thread for periodic updates
- `sentiment-api/moodlight_extensions.py` - Additional API endpoints (registered via `register_moodlight_endpoints(app)`)

**Singleton pattern** for database and cache instances:
```python
_db = None
def get_database() -> Database:
    global _db
    if _db is None:
        _db = Database(database_url)
        _db.connect()
    return _db
```

## Open Questions

- No `.editorconfig` file exists - consider adding one for consistent indentation
- The brace style is inconsistent in C++ code - needs a standard
- RSS feed list is duplicated between `app.py` and `background_worker.py` - should be shared
- `get_category_from_score()` is duplicated between `moodlight_extensions.py` and `background_worker.py`

---

*Convention analysis: 2026-03-25*
