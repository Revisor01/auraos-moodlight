# Architecture Research

**Domain:** ESP32 IoT firmware stabilization + Flask backend hardening
**Researched:** 2026-03-25
**Confidence:** HIGH (basiert auf direkter Codeanalyse)

## Standard Architecture

### System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    ESP32 Firmware (Monolith)                     │
│                   firmware/src/moodlight.cpp                     │
├──────────────┬──────────────┬──────────────┬────────────────────┤
│  LED Control │  WiFi/MQTT   │  Web Server  │  Sentiment Fetch   │
│  (mutex-     │  (Reconnect  │  (HTTP:80,   │  (HTTP GET alle    │
│  protected)  │  Backoff)    │  LittleFS)   │  30 Min)           │
├──────────────┴──────────────┴──────────────┴────────────────────┤
│              MoodlightUtils (Watchdog, Memory, File I/O)         │
│              firmware/src/MoodlightUtils.h/.cpp                  │
├──────────────────────────────────────────────────────────────────┤
│              Hardware: NeoPixel (Pin 26), DHT (Pin 17), LittleFS │
└──────────────────────────────────────────────────────────────────┘
                               │ HTTP GET (analyse.godsapp.de)
                               ↓
┌─────────────────────────────────────────────────────────────────┐
│                    Flask Backend (Docker)                         │
├──────────────┬──────────────┬──────────────────────────────────┤
│  app.py      │  moodlight_  │  background_worker.py             │
│  (API Layer) │  extensions  │  (30 Min RSS + OpenAI)            │
│              │  .py         │                                    │
├──────────────┴──────────────┴──────────────────────────────────┤
│              database.py (PostgreSQL + Redis Singletons)         │
├─────────────────────┬───────────────────────────────────────────┤
│  PostgreSQL 16      │  Redis 7 (256MB LRU, AOF, 5 Min TTL)      │
└─────────────────────┴───────────────────────────────────────────┘
```

### Component Boundaries (Stabilisierungsfokus)

| Komponente | Verantwortung | Berührt von Fixes |
|------------|--------------|-------------------|
| `ledColors[]` Array | Shared State zwischen Sentiment-Update und processLEDUpdates() | Buffer-Overflow Fix, RAII-Fix |
| `JsonBufferPool` | Heap-schonendes JSON-Parsing | RAII Memory Leak Fix |
| `processLEDUpdates()` | Entkoppelter LED-Schreibzugriff via Mutex + 50ms-Rate-Limit | LED/WiFi Timing Fix |
| `setStatusLED()` | Status-Blink über die letzte LED | Blink-Unterdrückung bei Reconnects |
| Health-Check-Block (1h) | Systemstats, Restart-Empfehlung (Zeilen 4364-4472) | Health-Check-Konsolidierung |
| Health-Check-Block (5min) | sysHealth.update() + Restart (Zeilen 4489-4499) | Health-Check-Konsolidierung |
| `app.run()` in `app.py` | Flask Dev-Server-Einstiegspunkt | Gunicorn-Migration |
| `socket.setdefaulttimeout()` | Globaler Timeout in app.py + background_worker.py | Per-Connection Timeout Fix |
| `rss_feeds`-Dict | Dupliziert in app.py (Zeile 55) und background_worker.py (Zeile 137) | RSS-Deduplizierung |
| Kategorie-Thresholds | Dreifach definiert, inkonsistent (app.py vs. extensions vs. worker) | Threshold-Konsolidierung |
| `/api/export/settings` | Gibt `wifiPass`/`mqttPass` im Klartext zurück | Credential-Masking |

---

## Recommended Project Structure

Kein Refactoring des Monolithen in diesem Milestone (explizit Out of Scope). Alle Fixes sind
**in-place Korrekturen** innerhalb der bestehenden Dateistruktur.

```
firmware/src/
├── config.h              # MAX_LEDS-Konstante hinzufuegen (Buffer-Overflow Fix)
├── moodlight.cpp         # Alle Firmware-Fixes inline (kein Split)
└── MoodlightUtils.h/.cpp # Unveraendert (Hilfsfunktionen bleiben stabil)

sentiment-api/
├── app.py                # socket.setdefaulttimeout entfernen, tote Endpoints entfernen
│                         # rss_feeds nach shared_config.py auslagern
├── shared_config.py      # NEU: RSS_FEEDS dict + CATEGORY_THRESHOLDS (einzige neue Datei)
├── moodlight_extensions.py # Import von shared_config statt lokaler Definition
├── background_worker.py  # Import von shared_config statt lokaler Definition
├── requirements.txt      # gunicorn hinzufuegen
└── Dockerfile            # CMD auf gunicorn umstellen
```

### Structure Rationale

- `shared_config.py` ist die einzige neue Datei: minimale Invasivitaet, loest aber zwei der
  kritischsten Duplikationsprobleme (RSS-Feeds + Thresholds) auf einmal.
- Alle anderen Aenderungen sind direkte Korrekturen im bestehenden Code, kein struktureller Umbau.

---

## Architectural Patterns

### Pattern 1: RAII fuer JsonBufferPool (Heap-Leak Fix)

**Was:** Scope-gebundenes Acquire/Release statt manueller `release()`-Aufrufe.
**Wann:** Immer wenn `jsonPool.acquire()` verwendet wird — der Mutex-Timeout-Pfad in `release()`
  laesst Heap-Allokationen unbefreit, wenn der Mutex nach 100ms nicht verfuegbar ist.
**Trade-offs:** Minimale ROM-Kosten (~40 Bytes fuer die Wrapper-Klasse), eliminiert die
  Leckmoeglichkeit vollstaendig.

**Minimal-Implementation:**
```cpp
class JsonBufferGuard {
public:
    char* buf;
    JsonBufferGuard() : buf(jsonPool.acquire()) {}
    ~JsonBufferGuard() { if (buf) jsonPool.release(buf); }
    // Nicht kopierbar
    JsonBufferGuard(const JsonBufferGuard&) = delete;
    JsonBufferGuard& operator=(const JsonBufferGuard&) = delete;
};
// Verwendung: JsonBufferGuard guard; JsonDocument doc(guard.buf, JSON_BUFFER_SIZE);
// Freigabe automatisch am Ende des Scope, auch bei fruehzeitigem return.
```

### Pattern 2: MAX_LEDS-Begrenzung fuer ledColors[] (Buffer-Overflow Fix)

**Was:** Statisch allokiertes `ledColors`-Array hat Groesse 12 (`DEFAULT_NUM_LEDS`), aber
  `numLeds` ist zur Laufzeit konfigurierbar. Schreibzugriff in `processLEDUpdates()` und
  `updateLEDs()` mit `numLeds` als Schleifengrenze = Out-of-bounds-Schreiben moeglich.
**Wann:** `numLeds > 12` via Web-Interface gesetzt.
**Loesung:** `MAX_LEDS`-Konstante in `config.h`, `numLeds` beim Laden per `constrain()` kappen.

```cpp
// config.h
#define MAX_LEDS 64  // Absolutes Maximum, Array-Groesse

// moodlight.cpp (Global)
volatile uint32_t ledColors[MAX_LEDS];  // Statt DEFAULT_NUM_LEDS

// beim Laden der Settings:
numLeds = constrain(loadedNumLeds, 1, MAX_LEDS);
```

### Pattern 3: Konsolidierter Health-Check (Doppelte Timer Fix)

**Was:** Zwei separate Timer-Bloecke in `loop()` mit ueberlappender Funktionalitaet:
- Block 1 (1h): Stats schreiben, Restart-Planung, Filesystem-Check
- Block 2 (5min): `sysHealth.update()`, sofortiger Restart bei kritischen Problemen

Der 5-Minuten-Block kann einen sofortigen Restart ausloesen, der 1-Stunden-Block plant ihn nur
fuer die Nacht — inkonsistentes Verhalten, moegliche Race-Condition.

**Loesung:** Einen einzigen Block mit zwei Intervallen, explizit priorisiert:

```cpp
// Einzige sysHealth-Variable:
static unsigned long lastFastHealthCheck = 0;
static unsigned long lastFullHealthCheck = 0;

// Fast Check (5 Min): Nur kritische Erkennung, KEIN sofortiger Restart
if (millis() - lastFastHealthCheck > 300000) {
    sysHealth.update();
    lastFastHealthCheck = millis();
    // Kein direktes rebootNeeded=true hier
}

// Full Check (1h): Entscheidet ueber Restart (Nacht-Logik bleibt)
if (millis() - lastFullHealthCheck > HEALTH_CHECK_INTERVAL) {
    // Stats schreiben, restart-Planung wie bisher...
    if (sysHealth.isRestartRecommended()) { /* Nacht-Logik */ }
    lastFullHealthCheck = millis();
}
```

### Pattern 4: Status-LED Blink-Unterdrückung (Reconnect-Blinken Fix)

**Was:** `setStatusLED(1)` (blau blinkend) wird bei jedem WiFi-Reconnect sofort aufgerufen,
  auch bei kurzen Verbindungsunterbruechen < 5 Sekunden. Das Blinken stoert den normalen
  LED-Betrieb.
**Loesung:** Grace-Period vor dem Status-LED-Aktivieren:

```cpp
// Nur Status setzen wenn Disconnect laenger als GRACE (z.B. 5s)
unsigned long wifiLostTime = 0;
const unsigned long STATUS_LED_GRACE = 5000; // ms

// Im WiFi-Disconnect-Handler:
if (wifiLostTime == 0) wifiLostTime = millis();
if (millis() - wifiLostTime > STATUS_LED_GRACE) {
    setStatusLED(1); // Nur dann blinken
}
// Bei Reconnect:
wifiLostTime = 0;
setStatusLED(0);
```

### Pattern 5: Gunicorn fuer Flask (WSGI-Migration)

**Was:** `CMD ["python", "app.py"]` startet Flask Dev-Server (single-threaded, nicht
  produktionstauglich). Der Background Worker laeuft als Daemon-Thread innerhalb dieses Prozesses.
**Wichtig:** `start_background_worker()` wird am Ende von `app.py` aufgerufen, BEVOR `app.run()`.
  Bei Gunicorn mit mehreren Workers wuerde der Background Worker in JEDEM Worker-Prozess laufen.
  Loesung: 1 Worker (`-w 1`) oder Gunicorn `post_fork`-Hook der verhindert, dass Worker 2+ den
  Background-Thread starten.

**Empfehlung: `-w 1` (sicher, ausreichend fuer einen ESP32):**
```dockerfile
CMD ["gunicorn", "-w", "1", "--threads", "4", "-b", "0.0.0.0:6237", \
     "--timeout", "120", "--access-logfile", "-", "app:app"]
```

Hintergrund: Das Projekt hat einen einzelnen ESP32 als Client. Mehrere Workers wuerden den
Background-Worker duplizieren und zu Race-Conditions beim PostgreSQL-Schreiben fuehren.
`--threads 4` gibt Concurrency ohne Prozess-Duplikation.

### Pattern 6: Per-Connection Timeouts (Socket-Timeout Fix)

**Was:** `socket.setdefaulttimeout(10)` in `app.py` Zeile 373 und `background_worker.py`
  Zeile 164 aendert den GLOBALEN Prozess-Timeout. In Multi-Thread-Umgebungen (Gunicorn + Background
  Worker) kann das andere Verbindungen (PostgreSQL, Redis) beeinflussen.
**Loesung:** feedparser-eigenen Timeout nutzen oder urllib via feedparser-Parameter.

```python
# Statt socket.setdefaulttimeout(10):
feed = feedparser.parse(
    url,
    request_headers={'User-Agent': 'WorldMoodAnalyzer/1.0'},
    agent='WorldMoodAnalyzer/1.0',
    # feedparser unterstuetzt kein direktes timeout-Keyword,
    # aber requests-Handler kann verwendet werden:
)
# Alternativ: urllib.request mit timeout:
import urllib.request
req = urllib.request.Request(url, headers={'User-Agent': '...'})
with urllib.request.urlopen(req, timeout=10) as response:
    feed = feedparser.parse(response.read())
```

---

## Data Flow

### Primärer Flow: Sentiment -> LED (unveraendert, Fixes sind orthogonal)

```
Background Worker (30 Min)
    ↓ RSS fetch (12 Feeds, je 1 Headline)
    ↓ OpenAI GPT-4o-mini batch
    ↓ tanh(avg * 2.0)
    ↓ INSERT sentiment_history (PostgreSQL)
    ↓ Redis-Cache invalidieren

ESP32 loop() (alle 30 Min)
    ↓ GET /api/moodlight/current
    ↓ Redis-Cache-Hit (5 Min TTL) oder PostgreSQL-Fallback
    ↓ JSON parse (via JsonBufferPool -> nach Fix: RAII-gesichert)
    ↓ mapSentimentToLED() -> Index 0-4
    ↓ updateLEDs() -> ledColors[i] (nach Fix: i < constrain(numLeds, 1, MAX_LEDS))
    ↓ processLEDUpdates() -> pixels.show() (nach Fix: nur wenn kein WiFi-Reconnect aktiv)
```

### Wie Fixes den Data Flow veraendern

| Fix | Flow-Aenderung |
|-----|---------------|
| RAII JsonBufferPool | `acquire()` und `release()` werden scope-gebunden. Kein neuer Datenpfad. |
| MAX_LEDS Buffer Fix | `numLeds` wird beim Laden gecapped. Loop-Bound in `processLEDUpdates()` und `updateLEDs()` kann unveraendert bleiben (numLeds ist jetzt sicher). |
| Health-Check Konsolidierung | Zwei parallele Timer werden zu einem sequentiellen Block. Restart-Entscheidung geht nur noch durch die Nacht-Logik. |
| Status-LED Grace Period | Grace-Timer wird VOR `setStatusLED()` eingefuegt. LED-Mutex-Pfad unveraendert. |
| Gunicorn Migration | WSGI-Layer aendert sich, aber Flask-Routen und Background Worker bleiben identisch. |
| RSS-Feed Deduplizierung | `rss_feeds` wird aus `shared_config.py` importiert. Beide Codepfade (app.py + background_worker.py) nutzen dasselbe Objekt. |
| Threshold-Konsolidierung | `get_sentiment_category(score)` in `shared_config.py`, Imports ersetzen die drei lokalen Definitionen. |
| Credential Masking | `/api/export/settings` und `/api/settings/mqtt` geben `"****"` statt Passwort zurueck. Kein Einfluss auf interne Speicherung. |

---

## Build Order (Abhaengigkeiten zwischen Fixes)

### Gruppe 1: Unabhaengige Backend-Fixes (parallel ausfuehrbar)

Diese Fixes beruehren sich nicht gegenseitig und koennen in beliebiger Reihenfolge gemacht werden:

1. **`shared_config.py` erstellen + RSS-Feeds zentralisieren** — Voraussetzung fuer
   Threshold-Konsolidierung, aber selbst von nichts abhaengig.
2. **Threshold-Konsolidierung** — benoetigt `shared_config.py` (haengt von Fix 1 ab).
3. **Tote Endpoints entfernen** (`/api/dashboard`, `/api/logs`) — isoliert.
4. **Credential Masking** — isoliert, beruehrt nur `moodlight.cpp` und `moodlight_extensions.py`.
5. **`.env` in `.gitignore` + `setup.html.tmp.html` loeschen** — reine Housekeeping-Aufgabe.

### Gruppe 2: Gunicorn-Migration (nach Gruppe 1, wegen Background-Worker-Interaktion)

6. **`gunicorn` in `requirements.txt`** hinzufuegen.
7. **`Dockerfile` CMD aendern** auf `gunicorn -w 1 --threads 4 ...`.
8. **Global-Socket-Timeout entfernen** — nach Gunicorn-Migration, damit der Testlauf
   das neue Threading-Verhalten validiert.
9. **Docker-Stack neu deployen** und Logs pruefen.

### Gruppe 3: Firmware-Fixes (unabhaengig vom Backend, parallel zu Gruppe 1+2)

10. **`MAX_LEDS`-Konstante in `config.h`** + Array-Groesse anpassen — einfachster Fix, keine
    Logik-Aenderung.
11. **RAII `JsonBufferGuard`** — isolierter Fix, beruehrt nur `JsonBufferPool` und dessen
    Aufrufstellen. Empfohlen VOR anderen Firmware-Fixes, da er das Grundproblem der
    Heap-Fragmentierung adressiert.
12. **Health-Check Konsolidierung** — benoetigt kein anderes Fix, aber sollte nach gruendlicher
    Lektuere des `sysHealth.isRestartRecommended()`-Codes gemacht werden (Kriterien unklar laut
    CONCERNS.md).
13. **Status-LED Grace Period** — isoliert, kann nach Fix 12 gemacht werden (teilt `loop()`-Block).

### Abhaengigkeitsgraph

```
shared_config.py (1)
    └── Threshold-Konsolidierung (2)

gunicorn requirements (6)
    └── Dockerfile CMD (7)
        └── Socket-Timeout entfernen (8)
            └── Deploy + Test (9)

MAX_LEDS Fix (10) -- isoliert
RAII JsonBufferGuard (11) -- isoliert, empfohlen zuerst
Health-Check (12) -- isoliert
Status-LED Grace (13) -- nach (12) empfohlen
Housekeeping (3,4,5) -- jederzeit
```

---

## Anti-Patterns

### Anti-Pattern 1: Status-LED direkt aus WiFi-Event-Handler aktualisieren

**Was Leute tun:** `setStatusLED(1)` direkt im WiFi-Disconnect-Callback aufrufen.
**Warum es falsch ist:** WiFi-Events koennen auf einem anderen FreeRTOS-Task-Kontext laufen.
  Der Mutex-Zugriff von `setStatusLED()` auf `ledMutex` kann dort zu Deadlocks fuehren.
**Stattdessen:** Flag setzen (`statusLedUpdatePending = true`) und im `loop()` auswerten.
  Der bestehende Code nutzt bereits dieses Muster in Teilen — konsequent beibehalten.

### Anti-Pattern 2: Mehrere Gunicorn-Worker mit Background-Thread

**Was Leute tun:** `CMD ["gunicorn", "-w", "4", ...]` fuer bessere Performance.
**Warum es falsch ist:** `start_background_worker()` wird in `app.py` vor `app.run()` aufgerufen
  und wuerde in JEDEM der 4 Worker-Prozesse starten. 4 parallele Background-Worker schreiben
  gleichzeitig in PostgreSQL und rufen OpenAI 4x oefter auf.
**Stattdessen:** `-w 1 --threads 4` — ein Prozess, mehrere Threads, Background-Worker einmalig.
  Oder: Background-Worker in separaten Container auslagern (eigenstaendiger Milestone).

### Anti-Pattern 3: `constrain()` auf numLeds erst beim Lesen, nicht beim Speichern

**Was Leute tun:** In `processLEDUpdates()` `min(numLeds, MAX_LEDS)` als Loop-Bound verwenden,
  aber `numLeds` bleibt > MAX_LEDS im Speicher.
**Warum es falsch ist:** Andere Codestellen (z.B. `STATUS_LED_INDEX = numLeds - 1`) nutzen
  `numLeds` direkt und koennen weiterhin out-of-bounds sein.
**Stattdessen:** `numLeds` direkt beim Laden (Preferences/JSON) cappen:
  `numLeds = constrain(value, 1, MAX_LEDS)` — einmalig, sicher ueberall.

---

## Integration Points

### External Services

| Dienst | Integrationsmuster | Relevante Fixes |
|--------|-------------------|-----------------|
| OpenAI API | Batch-Request in `analyze_headlines_openai_batch()` | Kein Fix in diesem Milestone |
| PostgreSQL | ThreadedConnectionPool (1-5), Singleton via `get_database()` | Gunicorn w=1 schuetzt vor Pool-Erschoepfung |
| Redis | Singleton via `get_cache()`, 5-Min TTL | Unveraendert |
| MQTT Broker | ArduinoHA Library, exponentieller Backoff | Status-LED Grace Period reduziert sichtbare Artefakte |
| NTP | `configTime()` einmalig beim Boot | Unveraendert |

### Internal Boundaries (Firmware)

| Grenze | Kommunikation | Wichtig fuer Fixes |
|--------|--------------|-------------------|
| Sentiment-Fetch-Task <-> LED-Update | `ledUpdatePending` Flag + `ledMutex` | RAII Fix beruehrt diesen Pfad |
| WiFi-Events <-> Status-LED | Indirektes Flag-Muster (teilweise) | Grace-Period Fix vervollstaendigt dieses Muster |
| Health-Check (1h) <-> Health-Check (5min) | Getrennte static-Timer, teilen `sysHealth` Objekt | Konsolidierungs-Fix |
| JsonBufferPool <-> alle JSON-Verarbeitung | `acquire()`/`release()` manuell | RAII Fix |

### Internal Boundaries (Backend)

| Grenze | Kommunikation | Wichtig fuer Fixes |
|--------|--------------|-------------------|
| app.py <-> background_worker.py | Shared-Function-Reference + geteilt `rss_feeds` | RSS-Fix: Import statt Duplikat |
| app.py <-> moodlight_extensions.py | Flask Blueprint via `register_moodlight_endpoints()` | Threshold-Fix: Import aus shared_config |
| Flask-Prozess <-> Gunicorn | WSGI-Protokoll | Migration Fix |

---

## Confidence Assessment

| Bereich | Confidence | Begruendung |
|---------|-----------|-------------|
| Buffer-Overflow Fix | HIGH | Code direkt gelesen, Zeilen 148/443/580 eindeutig |
| RAII JsonBufferPool | HIGH | Code direkt gelesen, Zeilen 342-372 eindeutig |
| Health-Check Konsolidierung | HIGH | Code direkt gelesen, Zeilen 4364-4500 eindeutig |
| Status-LED Grace Period | HIGH | Code direkt gelesen, Blinkverhalten-Mechanismus klar |
| Gunicorn w=1 --threads 4 | HIGH | Background-Worker-Threading-Implikation verifiziert |
| Socket-Timeout Fix | HIGH | Code direkt gelesen, Zeile 373 und Worker-Zeile 164 |
| RSS/Threshold-Deduplizierung | HIGH | Code direkt gelesen, Zeilen 55+137 und 205/216/24 |
| Credential Masking | HIGH | Code direkt gelesen, Zeile 672 + 2088-2096 |

---

## Sources

- Direkte Codeanalyse: `firmware/src/moodlight.cpp` (Zeilen 148, 330-373, 440-454, 560-607, 4354-4500)
- Direkte Codeanalyse: `sentiment-api/app.py` (Zeilen 55, 205-209, 363-378, 559-574)
- Direkte Codeanalyse: `sentiment-api/background_worker.py` (Zeilen 137-150, 157-166, 216-227)
- Direkte Codeanalyse: `sentiment-api/moodlight_extensions.py` (Zeilen 24-35, 310-328)
- Direkte Codeanalyse: `sentiment-api/Dockerfile` (Zeile 18)
- `.planning/codebase/CONCERNS.md` — Vollstaendige Problemliste mit Zeilennummern
- `.planning/codebase/ARCHITECTURE.md` — Bestehendes Architektur-Dokument
- `.planning/PROJECT.md` — Projektkontext und Constraints

---
*Architecture research for: ESP32 IoT Firmware Stabilization + Flask Backend Hardening*
*Researched: 2026-03-25*
