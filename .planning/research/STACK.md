# Stack Research

**Domain:** ESP32 IoT firmware stabilization + Flask backend hardening
**Researched:** 2026-03-25
**Confidence:** MEDIUM — ESP32 patterns from community sources + official Espressif docs; Flask/Gunicorn from PyPI + Flask docs

---

## Recommended Stack

### Core Technologies

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| Adafruit NeoPixel | 1.15.4 | LED strip (WS2812B) | Already in use; keep for now — see LED Stabilization note below for migration path |
| NeoPixelBus (Makuna) | 2.8.4 | LED strip alternative with DMA/RMT/I2S | I2S-DMA method sends data via hardware with no CPU interrupt pressure — WiFi coexistence is the primary motivation for migrating away from Adafruit NeoPixel |
| FreeRTOS (built-in) | ESP-IDF bundled | Task management, mutexes | Already present in Arduino ESP32; `xSemaphoreCreateMutex()` + RAII wrapper is the fix for the JSON buffer pool leak |
| Gunicorn | 25.2.0 | Production WSGI server | Replaces Flask dev server (`app.run()`); multi-process, signals-based graceful shutdown, stable since 2010, Flask docs recommend it explicitly |
| requests | 2.32.3+ | HTTP client for RSS fetch with per-connection timeout | feedparser's maintainer documented that `socket.setdefaulttimeout()` is the only timeout feedparser natively supports — the correct fix is to fetch via `requests` with a `timeout=` parameter, then pass raw content to `feedparser.parse()` |

### Supporting Libraries

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| esp_task_wdt (built-in) | ESP-IDF bundled | Watchdog timer | Already in use via WatchdogManager; keep, no change needed |
| ArduinoHA | 2.1.0 | MQTT + Home Assistant integration | Already in use; keep |
| ArduinoJson | 7.4.0 | JSON parsing | Already in use; the buffer pool RAII fix does not require a version bump |
| feedparser | 6.0.11 | Parse RSS XML after fetch | Keep as the parsing layer only; remove it as the HTTP layer |

### Development Tools

| Tool | Purpose | Notes |
|------|---------|-------|
| PlatformIO | Build, flash, filesystem upload | No change; current workflow is correct |
| Docker Compose + Gunicorn | Backend container entrypoint | Change `CMD` in Dockerfile from `python app.py` to `gunicorn -w 2 -b 0.0.0.0:6237 app:app` |

---

## Installation

### Backend (add to `sentiment-api/requirements.txt`)

```
gunicorn==25.2.0
```

`requests` is already present (`requests==2.32.2`) — bump to `2.32.3` when convenient, not a blocker.

### Firmware (PlatformIO — migration path, not required for initial stabilization)

If the Adafruit NeoPixel timing/WiFi issues persist after the mutex + core-pinning fixes, migrate in `platformio.ini`:

```ini
lib_deps =
    ; Replace: adafruit/Adafruit NeoPixel@^1.12.5
    makuna/NeoPixelBus@^2.8.4
```

---

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| Gunicorn (`sync` workers) | uWSGI | uWSGI has more configuration knobs (Emperor mode, etc.) — only worth it if you need per-request resource limits or uWSGI-specific routing. Overkill for a single-service Flask app. |
| Gunicorn (`sync` workers) | `gevent` async workers | Only when the Flask app has long-blocking I/O and many concurrent clients. This backend has one client (the ESP32) plus periodic background I/O — sync workers are simpler and correct. |
| `requests` + `feedparser.parse(content)` | global `socket.setdefaulttimeout()` | The global timeout is a thread-unsafe hack. Keep the existing call only if library migration is not yet scheduled; replace it as part of the "global socket timeout" fix. |
| NeoPixelBus I2S-DMA | Adafruit NeoPixel (keep) | Adafruit NeoPixel is simpler; keep it if core-pinning + mutex eliminates the flicker. Migrate to NeoPixelBus only if Adafruit NeoPixel's interrupt-based output continues causing WiFi-correlation flicker. |
| FreeRTOS mutex + RAII wrapper | `taskENTER_CRITICAL` / `portDISABLE_INTERRUPTS` | Disabling interrupts is appropriate for very short critical sections (microseconds). The JSON buffer pool's critical section is longer; mutex with RAII is safer and does not starve the WiFi stack. |

---

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| `app.run(debug=True)` in production | Flask's dev server is single-threaded, reloads on every file change, not signal-safe, explicitly documented as not for production | Gunicorn 25.x |
| `socket.setdefaulttimeout()` in a multi-threaded Flask/Gunicorn context | Global process-wide side effect; race condition between Gunicorn workers and background thread | `requests.get(url, timeout=15)` per call |
| `feedparser.parse(url)` for URL fetching (without external timeout) | feedparser does not expose a timeout parameter on `parse()`; the HTTP request can hang indefinitely | `feedparser.parse(requests.get(url, timeout=15).content)` — separate fetch from parse |
| Adafruit NeoPixel with `portDISABLE_INTERRUPTS()` workaround | Disabling interrupts during `strip.show()` pauses the WiFi/MQTT stack for ~300µs per LED; at 12 LEDs this is ~3.6ms of WiFi blackout per update | NeoPixelBus with I2S-DMA method (`NeoEsp32I2s0800KbpsMethod`) |
| `xTaskCreate` without core pinning for LED tasks | Both cores share the interrupt table; without pinning, the scheduler may place the LED task on Core 0 (WiFi core), causing timing collisions | `xTaskCreatePinnedToCore(..., 1)` — pin LED task to Core 1 |
| Gunicorn `--workers $(nproc)` formula blindly | Formula is for general web servers; this Flask instance has one active client + one background worker thread, not concurrent web traffic | `-w 2` is sufficient and avoids unnecessary process overhead on a shared Hetzner server |

---

## Stack Patterns by Variant

**For the LED timing / WiFi coexistence fix (ESP32 firmware):**

- Keep Adafruit NeoPixel for now
- Pin the LED update task to Core 1 via `xTaskCreatePinnedToCore`
- Protect the `strip.show()` call and the `ledColors[]` array with a FreeRTOS mutex
- Suppress status-LED blink during reconnects by checking connection state before triggering any LED side-effects
- If flicker persists after the above: migrate to NeoPixelBus with `NeoEsp32I2s0800KbpsMethod` on Core 1

**For the JSON buffer pool memory leak (ESP32 firmware):**

- Implement a RAII guard class (stack-allocated, destructor calls `release()` unconditionally):
  ```cpp
  struct JsonBufferGuard {
      JsonBufferPool& pool;
      char* buf;
      JsonBufferGuard(JsonBufferPool& p) : pool(p), buf(p.acquire()) {}
      ~JsonBufferGuard() { if (buf) pool.release(buf); }
  };
  ```
- This ensures `release()` is always called even if mutex acquisition in the original `release()` path times out

**For the LED array bounds fix (ESP32 firmware):**

- Define `MAX_LEDS` constant (e.g., 50) in `config.h`
- Declare `ledColors[MAX_LEDS]` statically; validate `numLeds` against `MAX_LEDS` on write
- No dynamic allocation — heap fragmentation on ESP32 is worse than a slightly oversized static array

**For the Flask backend hardening:**

- Replace `CMD ["python", "app.py"]` with `CMD ["gunicorn", "-w", "2", "-b", "0.0.0.0:6237", "--timeout", "120", "app:app"]`
  - `--timeout 120` covers slow OpenAI API calls that may block a sync worker
- For feed fetching, wrap `feedparser.parse(url)` calls:
  ```python
  import requests, feedparser
  response = requests.get(feed_url, timeout=15, headers={"User-Agent": "AuraOS/1.0"})
  feed = feedparser.parse(response.content)
  ```
- Remove both `socket.setdefaulttimeout()` calls after the above is in place

---

## Version Compatibility

| Package | Compatible With | Notes |
|---------|-----------------|-------|
| Gunicorn 25.2.0 | Python 3.10+ | Confirmed; Python 3.12-slim in Dockerfile is compatible |
| Flask 3.1.0 | Gunicorn 25.x | Flask 3.x is fully WSGI-compatible; no adapter needed |
| NeoPixelBus 2.8.4 | Arduino ESP32 (esp32dev) | I2S method requires GPIO pin capable of I2S data output; pin 26 is compatible |
| Adafruit NeoPixel 1.15.4 | ArduinoHA 2.1.0 | No known conflict; they operate on different peripherals |
| requests 2.32.x | feedparser 6.0.11 | `feedparser.parse(bytes_content)` accepts raw bytes from `response.content` — confirmed in feedparser docs |

---

## Sources

- PyPI gunicorn — confirmed version 25.2.0 (released 2026-03-24), Production/Stable classifier — HIGH confidence
- Flask official docs (deploying/gunicorn) — recommends Gunicorn, 2+ workers — MEDIUM confidence (403 on fetch, content from search snippets)
- feedparser GitHub Issue #76, #263, maintainer blog — confirmed no native timeout parameter, recommends requests-first pattern — HIGH confidence
- NeoPixelBus GitHub releases — confirmed version 2.8.4 (May 2025) — HIGH confidence
- NeoPixelBus Wiki (ESP32-NeoMethods) — RMT uses ISR-heavy interrupt filling vs. I2S-DMA hardware path — MEDIUM confidence (wiki content verified via WebFetch)
- Adafruit NeoPixel GitHub releases — confirmed version 1.15.4 (Feb 2025) — HIGH confidence
- ESP32 Forum + Adafruit NeoPixel Issue #139 — confirmed 5µs/ms interrupt delay from WiFi corrupts NeoPixel timing — MEDIUM confidence (community sources)
- Espressif docs (xTaskCreatePinnedToCore) + ESP32 Forum WiFi/Core0 threads — confirmed WiFi task runs on Core 0, user tasks default to Core 1 — MEDIUM confidence

---

*Stack research for: ESP32 firmware stabilization + Flask backend hardening*
*Researched: 2026-03-25*
