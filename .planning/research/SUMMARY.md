# Project Research Summary

**Project:** AuraOS Moodlight — Stabilization Milestone
**Domain:** ESP32 IoT Firmware Stabilization + Flask Backend Hardening
**Researched:** 2026-03-25
**Confidence:** HIGH

## Executive Summary

AuraOS Moodlight ist ein laufendes, produktives System — kein Greenfield-Projekt. Das Ziel dieser Milestone-Runde ist ausschliesslich Stabilisierung: bekannte Fehler beheben, technische Schulden der kritischen Art abbauen, ohne die Architektur zu veraendern. Die Research-Ergebnisse basieren auf direkter Codeanalyse und sind entsprechend praezise: Alle identifizierten Probleme haben konkrete Zeilennummern, reproduzierbare Symptome und eindeutige Loesungspfade. Es gibt kein exploratives Element — die Ursachen sind bekannt, die Loesungen dokumentiert.

Der empfohlene Ansatz teilt sich in zwei parallele Arbeitstraenge: Firmware-Fixes (ESP32, `moodlight.cpp`) und Backend-Hardening (Flask, `sentiment-api/`). Beide Traenge sind weitgehend unabhaengig voneinander und koennen parallel bearbeitet werden. Die einzige kritische Abhaengigkeit ist die Gunicorn-Migration im Backend, bei der der Background-Worker genau einmal (`-w 1`) laufen muss — ein Fehler hier verdoppelt OpenAI-Kosten und erzeugt Race Conditions in PostgreSQL. Alle anderen Fixes sind isolierte In-Place-Korrekturen ohne Refactoring-Risiko.

Das primaere Risiko dieser Milestone ist nicht technischer Natur, sondern organisatorisch: Der Monolith (`moodlight.cpp`, ~4500 Zeilen) verleitet dazu, Refactoring mit Bug-Fixing zu vermischen. Research und Projektplan sind darin einig — kein Splitting in diesem Milestone. Alle Fixes sollen inline vorgenommen werden. Firmware-Modularisierung, HTTPS und Test-Suite sind explizit auf nachfolgende Milestones verschoben.

## Key Findings

### Recommended Stack

Der bestehende Stack bleibt nahezu unveraendert. Adafruit NeoPixel, ArduinoHA, ArduinoJson und FreeRTOS bleiben in Verwendung. Die zwei relevanten Stack-Aenderungen sind: (1) Gunicorn 25.2.0 ersetzt den Flask Dev-Server als WSGI-Layer — eine Konfigurationsaenderung in Dockerfile und requirements.txt. (2) Das Fetch-Pattern fuer RSS-Feeds wechselt von `feedparser.parse(url)` zu `requests.get(url, timeout=15)` + `feedparser.parse(response.content)`, um per-Connection-Timeouts statt des prozessglobalen `socket.setdefaulttimeout()` zu verwenden. NeoPixelBus als Adafruit-Ersatz ist als Fallback dokumentiert, aber erst relevant wenn Core-Pinning + Mutex die Flicker-Probleme nicht loesen.

**Core technologies:**
- FreeRTOS (built-in): Task-Management, Mutexes — RAII-Wrapper fuer JsonBufferPool loest Heap-Leak
- Adafruit NeoPixel 1.15.4: LED-Strip — Core-Pinning (Core 1) und konsequenter Mutex-Schutz beheben WiFi-Interferenz
- Gunicorn 25.2.0: WSGI-Server — ersetzt Flask Dev-Server, `-w 1 --threads 4` vermeidet Background-Worker-Duplikation
- requests 2.32.3: HTTP-Client fuer RSS-Fetch — loest globalen Socket-Timeout-Problem
- NeoPixelBus 2.8.4 (Fallback): I2S-DMA LED-Ausgabe — nur migrieren wenn Adafruit NeoPixel nach Fixes weiterhin Probleme macht

### Expected Features

Dieses Milestone liefert keine neuen Nutzer-Features — es behebt Stabilitaetsprobleme, die als Fehler wahrgenommen werden. Alle Items in der "Must Have"-Liste sind Korrektheitsprobleme (Undefined Behavior, Memory Leaks, Race Conditions) oder sichtbare Fehler (unerklaerliches Blinken, falsche Kategorie-Labels).

**Must have (table stakes — Stabilisierung):**
- [FW] LED-Array auf MAX_LEDS begrenzt — verhindert Buffer-Overflow bei numLeds > 12
- [FW] JSON Buffer Pool RAII — schliesst Heap-Leak bei Mutex-Timeout in `release()`
- [FW] Health-Check-Konsolidierung — beseitigt konfliktierendes Restart-Verhalten zweier Timer
- [FW] Status-LED Debounce — unterdrueckt Blinken bei kurzen (<5s) WiFi-Unterbrechungen
- [FW] LED-Task auf Core 1 pinnen — verhindert WiFi-Interrupt-Interferenz mit NeoPixel-Timing
- [FW] Credentials in API-Responses maskieren — entfernt WiFi/MQTT-Passwort aus GET-Responses
- [BE] Gunicorn ersetzt Flask Dev-Server — produktionstauglicher WSGI-Layer
- [BE] Per-Connection Socket-Timeouts — entfernt prozessglobales `socket.setdefaulttimeout()`
- [BE] RSS-Feed-Liste dedupliziert (`shared_config.py`) — eine Source of Truth fuer app.py und background_worker.py
- [BE] Sentiment-Thresholds konsolidiert — ein `get_sentiment_category()` fuer alle drei Dateien
- [BE] Tote Endpoints entfernt (`/api/dashboard`, `/api/logs`)
- [REPO] `.env` in `.gitignore` — verhindert versehentliches Committen von Secrets
- [REPO] Temp-Dateien und Binary-Releases aus Git entfernen

**Should have (differentiators — naechste Milestone):**
- Firmware-Modularisierung (`moodlight.cpp` aufsplitten)
- Backend Health-Endpoint (`GET /health`) fuer Uptime-Kuma-Integration
- GPIO-Input-Validierung im Web-UI

**Defer (v2+):**
- HTTPS fuer ESP32-Backend-Kommunikation
- Test-Suite (embedded firmware + backend)
- Authentifizierung / Authorization
- Multi-Device-Support

### Architecture Approach

Die Architektur bleibt unveraendert — Monolith auf dem ESP32, Flask-Monolith im Backend. Die einzige neue Datei ist `sentiment-api/shared_config.py` fuer RSS-Feed-Dict und Sentiment-Thresholds. Alle anderen Aenderungen sind In-Place-Korrekturen. Gunicorn laeuft mit `-w 1 --threads 4`, da der Background-Worker als Daemon-Thread innerhalb des Flask-Prozesses laeuft und mit mehreren Workers mehrfach gestartet wuerde.

**Major components:**
1. `firmware/src/moodlight.cpp` — ESP32-Monolith; alle Firmware-Fixes inline (LED-Core-Pinning, RAII, Health-Check, Debounce)
2. `firmware/src/config.h` — erhaelt `MAX_LEDS`-Konstante
3. `sentiment-api/shared_config.py` (NEU) — RSS_FEEDS-Dict + `get_sentiment_category()`-Funktion; von app.py, background_worker.py und moodlight_extensions.py importiert
4. `sentiment-api/Dockerfile` — CMD auf Gunicorn umgestellt
5. `sentiment-api/requirements.txt` — gunicorn 25.2.0 hinzugefuegt

### Critical Pitfalls

1. **Gunicorn `-w 2+` verdoppelt Background-Worker** — immer `-w 1 --threads 4` verwenden; Verifikation: Docker-Logs zeigen genau einen "Sentiment update" alle 30 Minuten, nie zwei parallel
2. **NeoPixel `show()` crasht unter WiFi-Last** — LED-Task auf Core 1 pinnen (`xTaskCreatePinnedToCore(..., 1)`); niemals `strip.show()` aus WiFi-Event-Callbacks oder MQTT-Callbacks aufrufen
3. **JSON Buffer Pool Heap-Leak** — RAII-Guard implementieren; `release()` muss Buffer auch bei Mutex-Timeout freigeben; Verifikation: `ESP.getFreeHeap()` bleibt nach 24h stabil
4. **Buffer-Overflow bei numLeds > 12** — `numLeds` beim Laden cappen: `numLeds = constrain(loaded, 1, MAX_LEDS)`; nicht erst beim Schreiben pruefen, da andere Codestellen `numLeds` direkt verwenden
5. **Inkonsistente Sentiment-Thresholds** — alle drei Threshold-Definitionen durch eine `shared_config.py`-Funktion ersetzen; Verifikation: Score 0.35 ergibt "positiv" ueberall, Score 0.80 ergibt "sehr positiv" ueberall

## Implications for Roadmap

Basierend auf der Research-Synthese ergeben sich zwei klar getrennte Arbeitsphasen mit parallelen Untergruppen:

### Phase 1: Firmware-Stabilitaet
**Rationale:** Die Firmware-Fixes sind vollstaendig unabhaengig vom Backend und verhindern aktive Crashes sowie Undefined Behavior. Der Buffer-Overflow (Pitfall 4) kann die `ledMutex`-Variable korrumpieren — er muss vor allen anderen Firmware-Fixes behoben werden.
**Delivers:** Stabiler ESP32-Betrieb ohne Watchdog-Resets, ohne Heap-Leaks, ohne NeoPixel-Flicker durch WiFi-Interrupts
**Addresses:** LED-Array-Bounds-Fix, RAII JsonBufferGuard, Health-Check-Konsolidierung, Status-LED-Debounce, LED-Core-Pinning, Credential-Masking in Firmware
**Avoids:** Pitfall 2 (NeoPixel-Crash), Pitfall 3 (Heap-Leak), Pitfall 4 (Buffer-Overflow)
**Build-Reihenfolge:** MAX_LEDS-Fix (1) → RAII JsonBufferGuard (2) → LED-Core-Pinning (3) → Health-Check-Konsolidierung (4) → Status-LED-Debounce (5) → Credential-Masking (6)

### Phase 2: Backend-Hardening
**Rationale:** Backend-Fixes sind untereinander ebenfalls weitgehend unabhaengig, aber Gunicorn-Migration muss vor dem Socket-Timeout-Fix getestet werden (Thread-Isolation begruendet den Fix). `shared_config.py` muss vor Threshold-Konsolidierung existieren.
**Delivers:** Produktionstauglicher Flask-Stack, keine Race Conditions, konsistente Sentiment-Daten, keine Secret-Leaks im Repository
**Uses:** Gunicorn 25.2.0, requests 2.32.3, feedparser 6.0.11
**Implements:** `shared_config.py` als neue Architekturkomponente
**Avoids:** Pitfall 1 (doppelter Background-Worker), Pitfall 5 (inkonsistente Thresholds)
**Build-Reihenfolge:** Housekeeping (.gitignore, tmp-Dateien) (1) → shared_config.py (2) → Threshold-Konsolidierung (3) → Tote Endpoints entfernen (4) → Gunicorn requirements + Dockerfile (5) → Socket-Timeout entfernen (6) → Deploy + Logs pruefen (7)

### Phase 3: Deferred (Eigenstaendige Folge-Milestones)
**Rationale:** Firmware-Modularisierung und HTTPS erfordern eigene Milestones mit dedizierter Planung. Test-Suite benoetigt Infrastructure-Entscheidungen fuer Embedded-Tests. Keines dieser Items behebt aktuelle Stabilitaetsprobleme.
**Delivers:** Maintainability (Modularisierung), Security (HTTPS), Quality-Assurance (Test-Suite)

### Phase Ordering Rationale

- Phase 1 und 2 koennen parallel entwickelt werden — keine Abhaengigkeiten zwischen Firmware und Backend in diesem Milestone
- Innerhalb Phase 1: MAX_LEDS-Fix zuerst, da Buffer-Overflow benachbarte Variablen korrumpieren kann und andere Fixes unzuverlaessig macht
- Innerhalb Phase 2: `shared_config.py` als erste Aktion, da Threshold-Konsolidierung davon abhaengt
- Gunicorn-Migration (-w 1) muss als Entscheidung vor dem Deployment feststehen — Pitfall 1 (Worker-Duplikation) hat sofortige und sichtbare Konsequenzen (OpenAI-Kosten, DB-Race-Conditions)

### Research Flags

Phasen, die waehrend der Planung keine zusaetzliche Research benoetigen:
- **Phase 1 (Firmware):** Alle Fixes sind durch direkte Codeanalyse vollstaendig spezifiziert. Implementierungsdetails liegen in ARCHITECTURE.md.
- **Phase 2 (Backend):** Alle Fixes sind durch direkte Codeanalyse vollstaendig spezifiziert. Gunicorn-Konfiguration ist dokumentiert.

Phasen, die bei der Planung Aufmerksamkeit benoetigen:
- **Phase 2 — Gunicorn-Deployment:** Die `-w 1`-Entscheidung schliesst horizontales Skalieren aus. Falls der Load jemals steigt (mehr Geraete), muss Background-Worker in separaten Container ausgelagert werden. Fuer den aktuellen Single-Device-Use-Case ist `-w 1` korrekt.
- **Phase 1 — NeoPixel-Fallback:** Wenn Core-Pinning + Mutex-Fix den Flicker nicht beseitigen, ist NeoPixelBus-Migration der naechste Schritt. Dies ist kein eigenstaendiger Roadmap-Punkt, sondern ein Fallback innerhalb Phase 1.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Gunicorn, requests, feedparser aus verifizierten Sources; NeoPixelBus aus GitHub-Releases bestaetigt |
| Features | HIGH | Direkte Codeanalyse mit Zeilennummern; keine Spekulationen ueber Nutzererwartungen noetig |
| Architecture | HIGH | Vollstaendig auf Codeanalyse basierend; jeder Fix hat konkrete Implementierungsanleitung |
| Pitfalls | HIGH | Community-Quellen fuer ESP32/NeoPixel-Timing; direkte Codeanalyse fuer alle anderen Pitfalls |

**Overall confidence:** HIGH

### Gaps to Address

- **`sysHealth.isRestartRecommended()`-Kriterien:** CONCERNS.md markiert die Restart-Kriterien als unklar. Vor der Health-Check-Konsolidierung sollte der Code in `MoodlightUtils.cpp` gelesen werden, um sicherzustellen, dass die Konsolidierung keine Restart-Schwellenwerte veraendert.
- **Arduino ESP32 Core-Version:** STACK.md empfiehlt eine explizit gepinnte Core-Version in platformio.ini (z.B. 2.0.17) fuer NeoPixel-Stabilitaet. Die aktuell verwendete Version und ob sie mit den bekannten Regressions-Versionen (3.0.x) kollidiert, sollte geprueft werden.
- **feedparser Timeout-Parameter:** STACK.md und ARCHITECTURE.md empfehlen leicht unterschiedliche Ansaetze (requests-first vs. urllib). Bei der Implementierung sollte der requests-Ansatz bevorzugt werden (`feedparser.parse(requests.get(url, timeout=15).content)`), da er im feedparser-Maintainer-Blog explizit empfohlen wird.

## Sources

### Primary (HIGH confidence)
- `.planning/codebase/CONCERNS.md` — vollstaendige Problemliste mit Zeilennummern
- `.planning/codebase/ARCHITECTURE.md` — bestehendes Architektur-Dokument
- `.planning/PROJECT.md` — Projektkontext und explizite Constraints
- PyPI gunicorn 25.2.0 (released 2026-03-24, Production/Stable) — Stack-Entscheidung
- feedparser GitHub Issues #76, #263 — bestaetigen kein nativen Timeout-Parameter
- NeoPixelBus GitHub Releases (v2.8.4, Mai 2025) — Version bestaetigt

### Secondary (MEDIUM confidence)
- Flask Official Docs (deploying/gunicorn) — Gunicorn-Empfehlung (403 beim Fetch, via Search-Snippets)
- NeoPixelBus Wiki (ESP32-NeoMethods) — RMT vs. I2S-DMA Vergleich
- ESP32 Forum + Adafruit NeoPixel Issue #139 — 5µs/ms Interrupt-Delay bestaetigt
- Espressif Docs (xTaskCreatePinnedToCore) — WiFi-Task auf Core 0 bestaetigt

### Tertiary (MEDIUM-LOW confidence)
- ESP32-S3 NeoPixel/WiFi Crash (GitHub Issue #429) — Crash-Pattern bestaetigt
- Gunicorn pre-fork global state (Medium) — Background-Worker-Duplikation erklaert
- Gunicorn Issue #2800 — Background Thread Zuverlaessigkeit
- Miguel Grinberg Blog — Flask Background Worker Best Practices

---
*Research completed: 2026-03-25*
*Ready for roadmap: yes*
