# Feature Research

**Domain:** ESP32 IoT Firmware Stabilization + Flask API Hardening
**Researched:** 2026-03-25
**Confidence:** HIGH (derived from direct codebase analysis + known defects in CONCERNS.md)

---

## Context

This is a stabilization milestone for an existing, running system — not a greenfield build.
"Features" here are stability and correctness behaviors, not new user-facing capabilities.
The question is: what does a production-stable embedded device and backend API actually need?

---

## Feature Landscape

### Table Stakes (Must Have for Stability)

Features that production IoT firmware and Flask APIs are expected to have. Their absence causes
the specific symptoms already observed: unexplained blinking, system hangs, inconsistent data.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| **[FW] LED updates never flicker due to WiFi/MQTT reconnects** | NeoPixel timing (800kHz bit-bang or RMT) is disrupted by WiFi interrupt latency. This is the #1 reported symptom. | MEDIUM | Root fix: ensure LED writes happen with interrupts in a clean state, or use RMT peripheral with DMA. The current `ledMutex` helps but doesn't fully solve WiFi interrupt interference. Needs event-driven suppression: no LED transition during active reconnect. |
| **[FW] LED array sized to `numLeds`, not compile-time constant** | Buffer overflow when user configures >12 LEDs is undefined behavior, can corrupt memory and cause random crashes. | LOW | Fix: introduce `MAX_LEDS` constant (e.g. 30), validate `numLeds` against it at write time, or use static array at MAX_LEDS. Dynamic allocation risks heap fragmentation on ESP32. |
| **[FW] JSON buffer pool RAII — no heap leak on mutex timeout** | Mutex timeout during `release()` currently orphans a 16KB heap allocation. Accumulated leaks trigger watchdog resets. | LOW | Fix: `wasPooled` flag on the buffer wrapper; `release()` calls `delete[]` unconditionally when buffer was heap-allocated, regardless of mutex outcome. |
| **[FW] Single consolidated health-check routine** | Two overlapping health-check timers (1h + 5min) with independent restart logic can trigger conflicting resets. One clear interval with explicit severity thresholds is the standard pattern. | LOW | Merge into one routine. Recommended: 5min interval, with counters to escalate from warn → restart at defined thresholds. Remove 1h duplicate. |
| **[FW] Status LED blink debounced — no blink on transient (<30s) reconnects** | A brief WiFi dropout that self-heals within seconds should not light up the living room LED strip in error red. Expected behavior: blink only if disconnected for sustained period. | LOW | Fix: track `disconnectStartMs`, only set status color if `millis() - disconnectStartMs > BLINK_DEBOUNCE_MS` (e.g. 30000). Clear on reconnect. |
| **[FW] Credentials not returned via API** | Returning WiFi password and MQTT password over HTTP GET is a security regression. Even in a home network, this is not acceptable in a stable production device. | LOW | Fix: replace password fields in `/api/export/settings` and `/api/settings/mqtt` responses with `"****"`. Passwords should only flow inward (POST) never outward (GET). |
| **[BE] Flask behind Gunicorn, not dev server** | Flask dev server is single-threaded, prints warnings on every request, is not signal-safe, and is explicitly documented as not for production. The background worker + Flask request handler already share a process — Gunicorn worker model provides proper isolation. | LOW | Replace `CMD ["python", "app.py"]` with `CMD ["gunicorn", "-w", "2", "-b", "0.0.0.0:6237", "app:app"]`. Add `gunicorn` to `requirements.txt`. Keep worker count at 2 — one for requests, one headroom. |
| **[BE] Per-connection socket timeouts, not global** | `socket.setdefaulttimeout()` is process-global. In a multi-threaded environment (Flask + background worker), this is a race condition: one thread's timeout setting silently affects another's connection. Per-connection timeouts via `feedparser`'s `agent` parameter or `requests(timeout=)` are the correct fix. | LOW | Remove all `socket.setdefaulttimeout()` calls. Pass `timeout=10` directly to each feed fetch call. |
| **[BE] Single source-of-truth for RSS feed list** | Two identical feed dicts in `app.py` and `background_worker.py` will drift. The background worker (which actually runs sentiment updates) currently has its own hardcoded copy that `feedconfig` POST never touches — making the config endpoint effectively a no-op. | LOW | Extract to `feeds.py` or a top-level constant in `app.py`. Background worker imports from there. |
| **[BE] Single source-of-truth for sentiment category thresholds** | Three different threshold definitions (`app.py`: 0.85/0.2, `background_worker.py` and `moodlight_extensions.py`: 0.30/0.10/-0.20/-0.50) produce inconsistent category labels depending on which code path executes. The ESP32 displays the category string — so a "neutral" from one path and "positiv" from another for the same score is a visible defect. | LOW | Consolidate into one function in `database.py` or a shared `sentiment_utils.py`. Use the `background_worker`/`extensions` thresholds (0.30/0.10/-0.20/-0.50) as they are consistent with each other. The `app.py` thresholds appear to be a legacy mistake. |
| **[BE] Dead endpoints removed** | `/api/dashboard` and `/api/logs` return literal `{...}` Python ellipsis syntax. Calling them raises a `500`. These endpoints do not exist functionally and must not exist in a production API. | LOW | Delete the two route handlers. |
| **[BE] `.env` in `.gitignore`** | `OPENAI_API_KEY` and `POSTGRES_PASSWORD` must not accidentally land in git. The absence of `.env` from `.gitignore` is an active risk on every commit. | LOW | Add `.env` and `sentiment-api/.env` to `.gitignore`. |
| **[REPO] Temp files and binary releases out of git** | `setup.html.tmp.html` is an editing artifact. Binary `.bin`/`.tgz` files in `releases/` bloat the repository and should live in GitHub Releases. | LOW | Delete the tmp file. Add `releases/`, `*.tmp.html`, `*.bin`, `*.tgz` to `.gitignore`. |

### Differentiators (Nice to Have — Not This Milestone)

Improvements that are valuable but do not address the current core stability problems.
They belong in a subsequent milestone after the system is stable.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| **[FW] Firmware modularization** | Splitting `moodlight.cpp` (4500 lines) into `wifi_manager.cpp`, `led_controller.cpp`, etc. would make the codebase maintainable and testable. | HIGH | Explicitly deferred in PROJECT.md as a separate milestone. Risk of regression during stabilization is too high. |
| **[FW] HTTPS for backend communication** | Encrypts ESP32 → API traffic; prevents passive interception on local network. | HIGH | Deferred in PROJECT.md due to prior `WiFiClientSecure` issues. Requires cert pinning or CA bundle management on device. |
| **[FW] GPIO input validation** | Prevents hardware damage from invalid pin values set via web UI. | LOW | Valuable but not causing the current instability symptoms. Add a `valid_gpio_pins[]` whitelist. |
| **[BE] Backend health endpoint** | A `GET /health` that returns Postgres connectivity, Redis connectivity, last sentiment update timestamp, and worker liveness. Enables uptime monitoring integration. | LOW | Useful for Uptime Kuma monitoring. Not blocking stability of the current device. |
| **[BE] OpenAI API call counter / budget guard** | Prevents runaway cost if the background worker loop misbehaves. | LOW | Nice to have for cost safety. The 30-minute interval makes runaway unlikely in practice. |
| **[FW] Setup AP with random password on first boot** | Prevents anyone nearby from connecting to the open `Moodlight-Setup` AP. | MEDIUM | Relevant for physical security, but the device is on a private home network. Out of scope per PROJECT.md (no auth milestone). |

### Anti-Features (Things to Deliberately NOT Build in This Milestone)

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| **Automated test suite** | CONCERNS.md flags the absence of tests | Adding a test suite during a stabilization push introduces scope risk and delays shipping the actual fixes. Tests for embedded firmware (non-host-executable) are a significant infrastructure effort. | Fix the known bugs first. Add tests in a dedicated quality milestone once the codebase is stable. PROJECT.md explicitly defers this. |
| **Firmware modularization** | 4500-line monolith is hard to maintain | Refactoring during bug-fixing is how regressions get introduced. The current structure, while messy, is understood and working. | Complete all bug fixes in the current structure, then split in a dedicated milestone. |
| **Authentication / authorization** | Security-conscious instinct | PROJECT.md explicitly out-of-scopes auth. This is a private home network device. Adding auth mid-stabilization adds scope and can break the existing web UI flows. | Mask credentials in API responses (table stakes fix, low effort). Full auth is a future milestone decision. |
| **HTTPS on ESP32** | Security-conscious instinct | Previous attempt had problems per PROJECT.md. `WiFiClientSecure` with cert management on 320KB RAM device requires careful memory handling. Wrong milestone. | Fix the stability bugs under HTTP first. HTTPS is its own milestone. |
| **Multi-device support** | Generalization instinct | One device is deployed. Designing for N devices adds DB schema changes, API routing complexity, and frontend work that is out of scope. | The existing device tracking (`X-Device-ID` headers) is sufficient for the single-device use case. |
| **feedconfig persistence** | The `/api/feedconfig` POST endpoint suggests runtime feed reconfiguration | The endpoint is currently a no-op (modifies in-memory state lost on restart, background worker ignores it). Making it persistent requires DB schema changes and is a new feature, not a stability fix. | Remove the endpoint in this milestone (dead weight) or leave it broken but documented. Do not implement persistence for it now. |

---

## Feature Dependencies

```
[BE] Single source-of-truth for category thresholds
    └──enables──> consistent category labels on ESP32 web UI and HA

[BE] Gunicorn replaces dev server
    └──requires──> gunicorn in requirements.txt
    └──enables──> safe [BE] per-connection timeouts (thread isolation now reliable)

[FW] LED array sized to numLeds
    └──blocks-if-absent──> [FW] LED update stability
    (buffer overflow can corrupt ledMutex state itself)

[FW] JSON buffer pool RAII fix
    └──prevents──> heap fragmentation over time
    └──which prevents──> random watchdog resets during normal operation

[FW] Status LED debounce
    └──requires──> [FW] LED updates stable (no point debouncing if LED itself flickers)

[FW] Credentials masked in API
    └──independent──> all other fixes (safe to do in any order)

[REPO] .env in .gitignore
    └──independent──> all other fixes (must be done before any commit with .env present)
```

### Dependency Notes

- **LED buffer fix must precede LED debounce work:** A buffer overflow corrupting `ledMutex` or adjacent memory makes any higher-level LED behavior fix unreliable.
- **Gunicorn before per-connection timeout fix:** The global `socket.setdefaulttimeout()` is only truly dangerous in a multi-threaded server context. With Gunicorn, worker isolation reduces (but does not eliminate) the risk — fix both together.
- **Category threshold consolidation is independent:** Can be done in any order, but should be done before any LED color behavior testing to avoid confusion about what score maps to what category.

---

## MVP Definition (This Milestone)

### Ship With (Stabilization Complete)

These are the minimum changes that make the device "stable in production" as defined by PROJECT.md's core value.

- [ ] **[FW] LED array bounds fix** — eliminates undefined behavior that can corrupt anything
- [ ] **[FW] JSON buffer pool RAII** — eliminates slow memory leak causing watchdog resets
- [ ] **[FW] Health check consolidation** — eliminates conflicting restart decisions
- [ ] **[FW] Status LED debounce** — eliminates the most visible symptom (blink on transient reconnects)
- [ ] **[FW] LED timing during reconnects** — eliminates NeoPixel flicker caused by WiFi interrupt interference
- [ ] **[FW] Credentials masked in API responses** — eliminates credential exposure, low effort
- [ ] **[BE] Gunicorn replaces dev server** — eliminates single-threaded bottleneck and dev warnings
- [ ] **[BE] Per-connection socket timeouts** — eliminates race condition in multi-threaded backend
- [ ] **[BE] RSS feed list deduplicated** — eliminates drift risk
- [ ] **[BE] Category thresholds unified** — eliminates inconsistent category labels
- [ ] **[BE] Dead endpoints removed** — eliminates 500 errors on `/api/dashboard`, `/api/logs`
- [ ] **[REPO] `.env` in `.gitignore`** — eliminates secret exposure risk
- [ ] **[REPO] Temp files + binary releases removed from git** — eliminates repo clutter

### Defer to Next Milestone

- [ ] **Firmware modularization** — only after stabilization is confirmed stable in production
- [ ] **HTTPS** — dedicated milestone with proper cert management strategy
- [ ] **Test suite** — dedicated quality milestone
- [ ] **Auth** — future decision, explicitly out of scope

---

## Feature Prioritization Matrix

| Feature | Stability Impact | Implementation Cost | Priority |
|---------|-----------------|---------------------|----------|
| LED array bounds fix | HIGH (UB/crash risk) | LOW | P1 |
| JSON buffer pool RAII | HIGH (slow leak → crash) | LOW | P1 |
| Health check consolidation | HIGH (conflicting restarts) | LOW | P1 |
| Gunicorn replaces dev server | HIGH (production correctness) | LOW | P1 |
| Category thresholds unified | MEDIUM (visible data inconsistency) | LOW | P1 |
| LED flicker during reconnects | HIGH (primary user symptom) | MEDIUM | P1 |
| Status LED debounce | MEDIUM (visible annoyance) | LOW | P1 |
| Per-connection socket timeouts | MEDIUM (thread race condition) | LOW | P1 |
| RSS feed list deduplicated | MEDIUM (drift risk) | LOW | P1 |
| Dead endpoints removed | LOW (latent 500 errors) | LOW | P2 |
| Credentials masked | MEDIUM (security hygiene) | LOW | P1 |
| `.env` in `.gitignore` | HIGH (secret leak prevention) | LOW | P1 |
| Temp/binary files from git | LOW (repo hygiene) | LOW | P2 |
| Firmware modularization | LOW for stability | HIGH | P3 (next milestone) |
| HTTPS | MEDIUM for security | HIGH | P3 (own milestone) |
| Test suite | LOW for immediate stability | HIGH | P3 (own milestone) |

---

## Sources

- Direct codebase analysis: `/Users/simonluthe/Documents/auraos-moodlight/.planning/codebase/CONCERNS.md`
- Direct codebase analysis: `/Users/simonluthe/Documents/auraos-moodlight/.planning/codebase/ARCHITECTURE.md`
- Project requirements: `/Users/simonluthe/Documents/auraos-moodlight/.planning/PROJECT.md`
- [ESP32 NeoPixel + WiFi RMT interference (esp32.com forum)](https://esp32.com/viewtopic.php?t=3980)
- [FastLED issue: random colors with WiFi enabled](https://github.com/FastLED/FastLED/issues/507)
- [ESP32 Watchdog Timer production guidance (Zbotic)](https://zbotic.in/esp32-watchdog-timer-prevent-system-freeze-in-production/)
- [Gunicorn production deployment guide (Better Stack)](https://betterstack.com/community/guides/scaling-python/gunicorn-explained/)
- [Flask performance optimization (DigitalOcean)](https://www.digitalocean.com/community/tutorials/how-to-optimize-flask-performance)

---

*Feature research for: ESP32 IoT firmware stabilization + Flask API hardening*
*Researched: 2026-03-25*
