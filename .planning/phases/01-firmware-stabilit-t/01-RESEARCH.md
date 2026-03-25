# Phase 1: Firmware-Stabilität - Research

**Researched:** 2026-03-25
**Domain:** ESP32 Arduino firmware — Speichersicherheit, LED-Timing, FreeRTOS, Security-Hardening
**Confidence:** HIGH — basiert auf direkter Codeanalyse der aktuellen Quelldateien

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
Alle sechs Fixes inline im Monolith (kein File-Split). Konkrete Vorgaben:
- **MAX_LEDS**: Statisches Array mit Konstante (z.B. 30), KEIN dynamisches Heap-Alloc
- **RAII Guard**: Destruktor-basiert, `wasPooled`-Flag oder equivalent um Heap-Fallback korrekt freizugeben
- **Health-Check**: 5min-Intervall beibehalten, Counter-basierte Eskalation; vor Merge: `sysHealth.isRestartRecommended()`-Kriterien in MoodlightUtils prüfen
- **LED Timing**: Reconnect-State tracken, `processLEDUpdates()`-Bedingung vereinfachen
- **Debounce**: `disconnectStartMs`-Variable, Status-LED erst setzen wenn `millis() - disconnectStartMs > 30000`

### Claude's Discretion
Alle konkreten Implementierungsdetails (Namen, Werte, Struktur) — reine Infrastruktur-Phase mit technisch spezifizierten Fixes aus der Research-Phase.

### Deferred Ideas (OUT OF SCOPE)
Keine deferreds — Diskussion blieb im Phase-Scope.
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| LED-01 | `ledColors` verwendet `MAX_LEDS` Konstante, `numLeds` wird beim Laden validiert | Buffer-Overflow-Analyse: Zeile 148 zeigt `volatile uint32_t ledColors[DEFAULT_NUM_LEDS]`; Zeilen 443-449 und 580 loopen über `numLeds` — klar out-of-bounds wenn >12 |
| LED-02 | Keine `pixels.show()` während aktiver Reconnect-Phase | Zeile 592: Bedingung `!WiFi.isConnected()` erlaubt `show()` wenn WiFi weg ist (invertierte Logik); Reconnect-State-Flag fehlt |
| LED-03 | Status-LED blinkt nicht bei kurzen Unterbrechungen (<30s) | Zeile 3748: `setStatusLED(1)` wird sofort bei `wifiWasConnected = false` aufgerufen, kein Debounce |
| MEM-01 | JSON Buffer Pool hat kein Memory Leak | Zeilen 353-355: Heap-Fallback mit `new char[]`; Zeilen 359-371: `release()` löscht Heap-Buffer NUR wenn Mutex erfolgreich — Mutex-Timeout = Leak |
| MEM-02 | Health-Checks in einer einzigen Routine konsolidiert | Zeilen 4364-4472 (1h-Block) + 4489-4499 (5min-Block): beide rufen `sysHealth.isRestartRecommended()` auf, der 5min-Block setzt `rebootNeeded = true` sofort ohne Nacht-Logik |
| SEC-01 | API-Responses enthalten keine Klartext-Passwörter | Zeile 2089: `doc["wifiPass"] = wifiPassword`; Zeile 2139: `doc["pass"] = mqttPassword` — beide im Klartext |
</phase_requirements>

---

## Summary

Phase 1 umfasst sechs isolierte Bugfixes an `firmware/src/moodlight.cpp` und `firmware/src/config.h`. Alle Probleme sind durch direkte Codeanalyse eindeutig lokalisiert und lassen sich ohne strukturelle Umbauten beheben. Die Phase berührt weder das Backend noch die Web-UI-Dateien.

Der kritischste Fix ist LED-01 (Buffer-Overflow): `ledColors[DEFAULT_NUM_LEDS]` ist mit 12 Elementen deklariert, der Schleifenbound ist aber `numLeds` — bei jeder Konfiguration über 12 LEDs werden globale Variablen korrumpiert. Dieser Fix ist eine einzeilige Deklarationsänderung, muss aber als erstes umgesetzt werden, weil er das Array schützt, das alle anderen LED-Fixes verwenden.

Der zweitgrößte Stabilitätsfaktor ist MEM-01 (RAII für JsonBufferPool): Der Mutex-Timeout-Pfad in `release()` lässt Heap-Allokationen unbefreit. Der Fix ist ein 10-Zeilen RAII-Guard der direkt in `moodlight.cpp` nach der `JsonBufferPool`-Struct-Definition platziert wird.

**Primäre Empfehlung:** Fixes in Reihenfolge LED-01 → MEM-01 → MEM-02 → LED-02 → LED-03 → SEC-01. Alle sind unabhängig, aber LED-01 ist Vorbedingung für fehlerfreies Testen der anderen LED-Fixes.

---

## Standard Stack

### Core — keine neuen Libraries nötig

Alle Phase-1-Fixes verwenden ausschließlich bereits eingebundene Bibliotheken.

| Library | Version | Purpose | Status |
|---------|---------|---------|--------|
| Adafruit NeoPixel | 1.15.4 (aktuell) | LED strip control | Bestehend — kein Wechsel in Phase 1 |
| FreeRTOS (built-in) | ESP-IDF bundled | `xSemaphoreCreateMutex`, `xSemaphoreTake/Give` | Bestehend — RAII-Guard baut darauf auf |
| ArduinoHA | 2.1.0 | MQTT — `mqtt.isConnected()` | Bestehend — Referenz in LED-02 Fix |
| Preferences | ESP32 built-in | Persistenz für Health-Check-Flags | Bestehend — unverändert |

**Installation:** Keine neuen Pakete. `platformio.ini` unverändert.

### Kritischer Hinweis: ESP32 Arduino Core Version

Der aktuelle `platformio.ini` sollte eine explizit gepinnte Arduino-ESP32-Core-Version haben. Bekannte Regressionen in Core >= 3.0.x betreffen NeoPixel-Timing und können mit 12+ LEDs zu reproduzierbaren Crashes führen.

```ini
; platformio.ini — empfohlen wenn noch nicht gesetzt:
platform = espressif32@6.x.x  ; Entspricht Arduino Core 2.0.17
```

**Prüfen:** `platformio.ini` auf explizite Versionspinning kontrollieren.

---

## Architecture Patterns

### Recommended Project Structure (unverändert)

```
firmware/src/
├── config.h              # MAX_LEDS Konstante hinzufügen (LED-01)
├── moodlight.cpp         # Alle 6 Fixes inline — kein Splitting
└── MoodlightUtils.h/.cpp # Nur lesen (isRestartRecommended-Kriterien prüfen)
```

### Pattern 1: RAII JsonBufferGuard (MEM-01)

**Was:** Scope-gebundenes Acquire/Release — Destruktor ruft `jsonPool.release()` garantiert auf, auch bei frühem `return` und auch wenn der Mutex in `acquire()` vorübergehend nicht verfügbar war.

**Wo einsetzen:** Direkt nach der `JsonBufferPool`-Struct-Definition (aktuell Zeile 373) in `moodlight.cpp`.

**Warum diese Stelle:** Alle `jsonPool.acquire()`-Aufrufe in den Webserver-Handlern folgen danach — keine Änderungen an den Handlern selbst nötig, wenn der Guard die gleiche Schnittstelle (`buf`-Pointer) bietet.

```cpp
// Nach Zeile 373 (Ende von struct JsonBufferPool) einfügen:
class JsonBufferGuard {
public:
    char* buf;
    JsonBufferGuard() : buf(jsonPool.acquire()) {}
    ~JsonBufferGuard() { if (buf) jsonPool.release(buf); }
    // Nicht kopierbar — vermeidet doppeltes release()
    JsonBufferGuard(const JsonBufferGuard&) = delete;
    JsonBufferGuard& operator=(const JsonBufferGuard&) = delete;
};
```

**Wichtig — Fallback korrekt freigeben:** Die bestehende `release()`-Methode (Zeilen 359-371) behandelt Heap-Fallback bereits korrekt: wenn `buffer` nicht im Pool-Array liegt, wird `delete[] buffer` aufgerufen. Der RAII-Guard nutzt einfach `jsonPool.release(buf)` und profitiert davon. Kein separates `wasPooled`-Flag im Guard nötig — die Pool-Logik unterscheidet selbst.

**Aber:** Das aktuelle `release()` hat den Mutex-Timeout-Bug: `delete[] buffer` wird nur dann ausgeführt, wenn der Mutex erfolgreich akquiriert wurde. Wenn `xSemaphoreTake` nach 100ms fehlschlägt, wird weder `inUse[i] = false` noch `delete[] buffer` ausgeführt — der Heap-Buffer läuft weg. Der RAII-Guard allein löst das nicht, wenn `release()` selbst den Bug hat.

**Vollständige Lösung für MEM-01:** Entweder `release()` patchen ODER im Guard vor dem `pool.release()`-Aufruf prüfen ob der Buffer ein Heap-Fallback war. Da die Pool-Struct kein Public-Flag dafür hat, ist der einfachste Fix, `release()` direkt zu korrigieren:

```cpp
void release(char* buffer) {
    if (xSemaphoreTake(mutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
        for (int i = 0; i < JSON_BUFFER_COUNT; i++) {
            if (buffer == buffers[i]) {
                inUse[i] = false;
                xSemaphoreGive(mutex);
                return;
            }
        }
        xSemaphoreGive(mutex);
    }
    // KRITISCHER FIX: delete[] IMMER wenn nicht im Pool —
    // unabhängig ob Mutex erfolgreich war
    // (Wenn Mutex fehlschlug: Pool-Slot bleibt belegt, aber Heap wird befreit)
    delete[] buffer;
}
```

### Pattern 2: MAX_LEDS Buffer-Overflow Fix (LED-01)

**Was:** Statische Array-Größe auf `MAX_LEDS` anheben, `numLeds` beim Laden clampen.

**Wichtig — STATUS_LED_INDEX:** `STATUS_LED_INDEX = DEFAULT_NUM_LEDS - 1` (Zeile 207) ist konstant auf 11 festgelegt. Nach dem Fix sollte dieser dynamisch auf `numLeds - 1` zeigen. Sonst schreibt `setStatusLED()` auf Index 11, auch wenn `numLeds = 20` konfiguriert ist (dann ist Index 11 keine letzte LED mehr).

```cpp
// config.h:
#define MAX_LEDS 64

// moodlight.cpp Zeile 148:
volatile uint32_t ledColors[MAX_LEDS];  // statt DEFAULT_NUM_LEDS

// moodlight.cpp Zeile 207 — STATUS_LED_INDEX auf dynamischen Wert ändern:
// const int STATUS_LED_INDEX = DEFAULT_NUM_LEDS - 1;  // ALT — entfernen
// stattdessen: numLeds - 1 direkt verwenden, oder separate Variable die nach numLeds-Load gesetzt wird

// Beim Laden der Settings (Preferences-Load oder JSON-Import):
numLeds = constrain(loadedNumLeds, 1, MAX_LEDS);
```

**Scope-Implikation:** `STATUS_LED_INDEX` als Konstante muss entfernt oder dynamisch werden. Alle Verwendungsstellen (Zeilen 207, 504, 547, 550, 3858) sind betroffen.

### Pattern 3: LED-02 WiFi/MQTT-Reconnect-Bedingung fixen

**Aktueller Bug (Zeile 592):**
```cpp
if (!WiFi.isConnected() || (WiFi.isConnected() && mqtt.isConnected()) || !mqttEnabled) {
```
Diese Logik erlaubt `pixels.show()` wenn WiFi getrennt ist (`!WiFi.isConnected()` ist `true`). Das ist das Gegenteil der Absicht: LEDs sollten während Reconnect NICHT aktualisiert werden.

**Richtige Logik:** LEDs nur aktualisieren wenn weder WiFi-Reconnect noch MQTT-Reconnect aktiv ist.

```cpp
// Reconnect-State-Flag (Global/Static):
static bool wifiReconnectActive = false;

// In der WiFi-Reconnect-Logik (um Zeile 3744):
// Bei Disconnect:
wifiReconnectActive = true;
// Bei erfolgreichem Reconnect:
wifiReconnectActive = false;

// processLEDUpdates() Zeile 590-606 — vereinfachte Bedingung:
if (needsUpdate && !wifiReconnectActive) {
    yield();
    delay(1);
    pixels.show();
    lastLedUpdateTime = currentTime;
}
```

**Alternativ ohne neues Flag:** `wifiWasConnected` prüfen — wenn `false`, dann ist Reconnect aktiv. Aber `wifiWasConnected` wird direkt bei Disconnect auf `false` gesetzt, also ist das de facto schon ein Reconnect-State-Flag.

### Pattern 4: LED-03 Status-LED Debounce

**Aktueller Code (Zeilen 3742-3748):**
```cpp
if (wifiWasConnected) {
    wifiWasConnected = false;
    wifiReconnectAttempts = 0;
    wifiReconnectDelay = 5000;
    setStatusLED(1);  // Sofort bei Disconnect
}
```

**Fix — 30s Grace Period:**
```cpp
// Globale Variable (bei den WiFi-Reconnect-Variablen, um Zeile 186):
unsigned long disconnectStartMs = 0;
const unsigned long STATUS_LED_GRACE_MS = 30000;  // Anforderung: <30s ruhig

// Im WiFi-Disconnect-Handler:
if (wifiWasConnected) {
    wifiWasConnected = false;
    wifiReconnectAttempts = 0;
    wifiReconnectDelay = 5000;
    disconnectStartMs = millis();  // Grace-Timer starten, KEIN setStatusLED(1) hier
}

// Separater Prüfpunkt im loop() oder im WiFi-Check-Block:
if (disconnectStartMs > 0 && !WiFi.isConnected()) {
    if (millis() - disconnectStartMs > STATUS_LED_GRACE_MS) {
        setStatusLED(1);
        disconnectStartMs = 0;  // Einmalig setzen
    }
}

// Bei Reconnect:
disconnectStartMs = 0;
setStatusLED(0);
```

### Pattern 5: MEM-02 Health-Check Konsolidierung

**Aktueller Zustand (Zeilen 4364-4500):**
- Block 1 (Zeile 4366): Prüft alle `HEALTH_CHECK_INTERVAL` (1h), schreibt Stats, plant Neustart via Nacht-Logik
- Block 2 (Zeile 4490): Prüft alle 300.000ms (5min), ruft `sysHealth.update()` auf, setzt `rebootNeeded = true` sofort

**Problem:** Block 2 setzt `rebootNeeded` sofort ohne Nacht-Logik. Das kann mitten am Tag zu einem ungeplanten Neustart führen, wenn `sysHealth.isRestartRecommended()` wahr wird.

**`isRestartRecommended()`-Kriterien (verifiziert in MoodlightUtils.cpp Zeilen 1166-1198):**
- Heap-Fragmentierung > 85% bei Laufzeit > 48h → Neustart
- Freier Heap < 10.000 Bytes bei Laufzeit > 24h → Neustart
- Dateisystem > 95% belegt → Neustart
- Laufzeit > 720h (30 Tage) → Neustart

**Fix — konsolidierter Block:**
```cpp
// Variablen am Anfang von loop() (oder als static in loop):
static unsigned long lastFastHealthMs = 0;
// lastSystemHealthCheckTime bereits vorhanden (Zeile 324)

// 5-Minuten-Check: NUR sysHealth.update() — kein Neustart-Trigger
if (millis() - lastFastHealthMs > 300000) {
    sysHealth.update();
    memMonitor.update();
    lastFastHealthMs = millis();
}

// 1-Stunden-Check: Stats schreiben + Neustart-Entscheidung (Nacht-Logik bleibt exakt wie bisher)
if (millis() - lastSystemHealthCheckTime >= HEALTH_CHECK_INTERVAL) {
    // ... (bestehender Block Zeilen 4367-4471 unverändert)
    // sysHealth.isRestartRecommended() bleibt nur hier
    lastSystemHealthCheckTime = millis();
}
```

Der 5min-Block aus Zeilen 4489-4499 wird komplett entfernt.

### Pattern 6: SEC-01 Credentials maskieren

**Stellen:**

1. `/api/export/settings` Handler (Zeilen 2088-2096):
   ```cpp
   doc["wifiPass"] = "****";  // statt wifiPassword
   doc["mqttPass"] = "****";  // statt mqttPassword
   ```

2. `/api/settings/mqtt` Handler (Zeile 2139):
   ```cpp
   doc["pass"] = "****";  // statt mqttPassword
   ```

**Import-Verhalten:** Wenn der Nutzer ein exportiertes JSON reimportiert und `"****"` als Passwort-Wert bekommt, darf das Passwort NICHT mit `"****"` überschrieben werden. Im Import-Handler muss geprüft werden:
```cpp
if (imported_pass != "****") {
    wifiPassword = imported_pass;
}
```
Prüfen ob der Import-Handler (`/importSettings` o.ä.) diese Logik bereits hat oder ob sie ergänzt werden muss.

### Anti-Patterns to Avoid

- **`constrain()` nur im Loop-Bound, nicht beim Laden:** Wenn `numLeds` zu 20 in Preferences geladen wird aber erst in `processLEDUpdates()` gekürzt wird, greift `setStatusLED()` auf `ledColors[STATUS_LED_INDEX]` = Index 11 zu, während der Rest des Codes `numLeds = 20` nutzt. Inkonsistenz. → `numLeds` beim Laden clampen.
- **Grace-Period-Timer nicht zurücksetzen:** `disconnectStartMs = 0` muss bei erfolgreichem Reconnect gesetzt werden, sonst triggert die Grace-Period-Prüfung nie wieder korrekt.
- **RAII Guard mit `jsonPool` global:** `jsonPool` ist eine globale Variable in `moodlight.cpp` — der Guard kann direkt darauf zugreifen, keine Parameter nötig.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Scope-gebundene Ressourcenfreigabe | Eigenes Ref-Counting / Flag-System | C++ RAII Destruktor | Destruktoren werden garantiert aufgerufen — auch bei Exceptions und frühem return |
| Thread-safe LED-State-Sharing | Globales Polling ohne Synchronisation | Bestehender `ledMutex` mit `xSemaphoreTake` | Bereits vorhanden, bereits korrekt — nicht duplizieren |
| Reconnect-State | Neue Zustandsmaschine | Bestehende `wifiWasConnected`-Variable | Semantisch identisch: `false` = Reconnect aktiv |
| Nacht-basierter Reboot-Scheduler | Eigener Cron-ähnlicher Timer | Bestehende `tm timeinfo` + `localtime_r()`-Logik (Zeile 4419) | Bereits implementiert und getestet |

**Key insight:** Alle sechs Fixes sind Korrekturen an Stellen, wo bestehendes, korrektes Code-Muster (Mutex, RAII, Clamping) entweder fehlt oder falsch angewendet wurde.

---

## Runtime State Inventory

Step 2.5: SKIPPED — Phase 1 ist ein reiner Firmware-Fix, kein Rename/Refactor/Migration. Keine Runtime-State-Änderungen an Datenbanken, Registrierungen oder gespeicherten Identifikatoren.

---

## Environment Availability

Step 2.6: SKIPPED — Phase 1 ändert nur C++-Quellcode in bestehenden Dateien. PlatformIO ist bereits als Build-Tool konfiguriert. Keine neuen externen Dependencies.

**Hinweis für Test/Deploy:** Nach dem Fix muss die Firmware per OTA oder USB auf das ESP32-Gerät eingespielt werden. PlatformIO-Kommando: `pio run -t upload`.

---

## Common Pitfalls

### Pitfall 1: STATUS_LED_INDEX bleibt Konstante nach LED-01-Fix

**What goes wrong:** `const int STATUS_LED_INDEX = DEFAULT_NUM_LEDS - 1` (= 11) zeigt auf die falsche LED wenn `numLeds` > 12 konfiguriert ist. Die Status-LED blinkt dann an Position 11, obwohl der Nutzer 20 LEDs hat und die letzte bei Index 19 liegt.

**Why it happens:** Die Konstante wurde nie an `numLeds` geknüpft — war unproblematisch solange der Buffer-Overflow alle größeren Werte verhinderte.

**How to avoid:** Nach LED-01-Fix `STATUS_LED_INDEX` als dynamische Variable definieren, die nach dem `numLeds`-Load (Settings) aktualisiert wird: `statusLedIndex = numLeds - 1`.

**Warning signs:** Status-LED blinkt nicht sichtbar bei 20+ LEDs, oder blinkt auf falscher Position.

---

### Pitfall 2: Doppelter `rebootNeeded = true` in Verbindung mit `rebootTime`

**What goes wrong:** Der 5min-Block (Zeile 4497) setzt `rebootNeeded = true` und `rebootTime = millis() + 60000`. Der 1h-Block (Zeile 4425) setzt `rebootTime = currentMillis + 60000` ebenfalls. Falls `isRestartRecommended()` in beiden Blocks ausgewertet wird und beide in kurzer Zeit laufen, kann `rebootTime` überschrieben werden.

**Why it happens:** Zwei Codepfade schreiben auf dieselbe globale Variable ohne gegenseitige Ausschluss-Logik.

**How to avoid:** Nach MEM-02-Fix gibt es nur noch einen `rebootNeeded`-Setzer. Vor dem Fix: sicherstellen dass die 5min-Block-Entfernung vollständig ist (keine partielle Lösung).

---

### Pitfall 3: `release()` Bug überlebt den RAII-Fix

**What goes wrong:** RAII-Guard ruft `jsonPool.release(buf)`. Wenn `release()` den Mutex nicht bekommt (100ms Timeout), wird `delete[] buf` nicht aufgerufen. Der Guard selbst hat kein `delete[]`.

**Why it happens:** Der Guard delegiert vollständig an `release()` — der Bug in `release()` überlebt.

**How to avoid:** `release()` direkt patchen (siehe Pattern 1 Codebeispiel) — `delete[] buffer` muss ausserhalb des Mutex-Blocks stehen, nach der Pool-Iteration.

---

### Pitfall 4: WiFi-Reconnect-Flag und processLEDUpdates Race Condition

**What goes wrong:** `wifiReconnectActive`-Flag wird im WiFi-Check-Callback gesetzt, `processLEDUpdates()` liest es im Loop. Auf einem Single-Core-Loop ist das unproblematisch — aber wenn LED-Updates auf einem anderen FreeRTOS-Task laufen würden, wäre das eine Race Condition.

**Why it happens:** Der aktuelle Code hat LED-Updates im Loop-Task (kein separater Task mit `xTaskCreate`). Solange das so bleibt, ist das Flag-Pattern sicher.

**How to avoid:** Wenn in Zukunft LED-Updates in einen eigenen Task ausgelagert werden (Phase 2 oder später): Flag mit `ledMutex` schützen. Für Phase 1: keine Maßnahme nötig.

---

### Pitfall 5: Import-Handler für maskierte Passwörter nicht vergessen

**What goes wrong:** `/api/export/settings` gibt `"****"` aus. Beim Reimport wird `"****"` als neues Passwort in Preferences geschrieben. WiFi-Verbindung bricht dauerhaft ab.

**Why it happens:** Import- und Export-Handler sind getrennte Endpoints. Export-Fix ohne Import-Guard-Check ist ein halber Fix.

**How to avoid:** Import-Handler prüfen (Grep nach `/importSettings` oder `/savesettings`) — falls `"****"` nicht explizit abgefangen wird: Guard hinzufügen.

---

## Code Examples

### Verifikation: Vollständige Zeilennummern der Änderungsstellen

```
LED-01 (Buffer-Overflow):
  - config.h:12        → #define DEFAULT_NUM_LEDS 12 → MAX_LEDS 64 (neues Define), DEFAULT_NUM_LEDS bleibt für Initialisierungswert
  - moodlight.cpp:148  → volatile uint32_t ledColors[DEFAULT_NUM_LEDS] → [MAX_LEDS]
  - moodlight.cpp:207  → const int STATUS_LED_INDEX = DEFAULT_NUM_LEDS - 1 → dynamisch
  - Settings-Loader    → numLeds = constrain(loaded, 1, MAX_LEDS) — Stelle im JSON-Load suchen

LED-02 (WiFi/MQTT Timing):
  - moodlight.cpp:590-606 → if (needsUpdate) Bedingung fixen
  - WiFi-Disconnect-Handler ~3744 → Flag setzen

LED-03 (Status-LED Debounce):
  - Globale Variable ~Zeile 186-204 → disconnectStartMs + Konstante
  - WiFi-Disconnect-Handler ~3748 → setStatusLED(1) entfernen, Timer setzen
  - Loop oder WiFi-Check → Grace-Period prüfen + setStatusLED wenn überschritten
  - WiFi-Reconnect-Handler → disconnectStartMs = 0 + setStatusLED(0)

MEM-01 (RAII Guard):
  - moodlight.cpp:359-371 → release() Bug patchen
  - moodlight.cpp:373+    → JsonBufferGuard Klasse einfügen

MEM-02 (Health-Check):
  - moodlight.cpp:4489-4500 → 5min-Block entfernen
  - moodlight.cpp:4486      → memMonitor.update() bleibt (separat von sysHealth)
  - moodlight.cpp:4364-4472 → 1h-Block: sysHealth.update() aus 5min-Block hierher verschieben

SEC-01 (Credentials masken):
  - moodlight.cpp:2089 → "****" statt wifiPassword
  - moodlight.cpp:2096 → "****" statt mqttPassword
  - moodlight.cpp:2139 → "****" statt mqttPassword
  - Import-Handler     → "****"-Guard prüfen/ergänzen
```

### isRestartRecommended() — verifizierte Schwellenwerte

Aus `MoodlightUtils.cpp` Zeilen 1166-1198 (verifiziert):

```cpp
// Neustart empfohlen wenn:
// 1. Fragmentierungsindex > 0.85 UND Uptime > 48h
// 2. FreeHeap < 10.000 Bytes UND Uptime > 24h
// 3. LittleFS-Belegung > 95%
// 4. Uptime > 720h (30 Tage)
// Formel: fragmentationIndex = 1.0 - (maxBlock / freeHeap)
```

Diese Schwellenwerte ändern sich durch MEM-02 nicht — der Fix verschiebt nur, WO und WANN `isRestartRecommended()` aufgerufen wird und WIE auf das Ergebnis reagiert wird.

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `ledColors[DEFAULT_NUM_LEDS]` = fixed 12 | `ledColors[MAX_LEDS]` = fixed 64 | Phase 1 | Buffer-safe für alle konfigurierbaren LED-Zahlen |
| Manuelles `acquire()`/`release()` mit Mutex-Timeout-Bug | RAII Guard + gepatchtes `release()` | Phase 1 | Heap-Allokationen werden immer freigegeben |
| Zwei unabhängige Health-Check-Timer | Ein 5min-Update-Timer + ein 1h-Restart-Entscheider | Phase 1 | Kein unerwarteter Tages-Neustart |
| `setStatusLED(1)` sofort bei Disconnect | Grace Period von 30s vor Status-LED-Aktivierung | Phase 1 | Kein visuelles Blinken bei kurzen Verbindungsunterbrechungen |
| Klartext-Passwörter in API-Responses | `"****"` in allen Passwort-Feldern | Phase 1 | Passwörter nicht mehr über Netzwerk exponiert |

---

## Open Questions

1. **`STATUS_LED_INDEX` als Konstante vs. Variable**
   - Was wir wissen: `const int STATUS_LED_INDEX = DEFAULT_NUM_LEDS - 1` — ist statisch auf 11 festgelegt
   - Was unklar ist: Ob alle Verwendungsstellen (4 Stellen) auf eine Variable umgestellt werden können ohne Regressionen (z.B. in der AP-Mode-Initialisierung)
   - Empfehlung: Nach LED-01-Fix alle 4 Stellen prüfen und auf `numLeds - 1` umstellen

2. **Import-Handler für `"****"`-Passwörter**
   - Was wir wissen: Export gibt `"****"` aus nach SEC-01-Fix
   - Was unklar ist: Ob der Import-Handler (`/api/settings` POST oder `/savesettings`) bereits `"****"` ignoriert
   - Empfehlung: Vor SEC-01-Abschluss Grep nach dem Import-Endpoint und Guard prüfen/ergänzen

3. **`wifiWasConnected` als Reconnect-Flag**
   - Was wir wissen: `wifiWasConnected = false` wird bei Disconnect gesetzt (Zeile 3745)
   - Was unklar ist: Ob `wifiWasConnected` an anderen Stellen als Reconnect-State-Indikator verlässlich ist
   - Empfehlung: Ein dediziertes `wifiReconnectActive`-Flag ist klarer — vermeidet semantische Doppelnutzung

4. **Settings-Loader-Stelle für `numLeds`-Clamping**
   - Was wir wissen: Preferences-API wird verwendet, `numLeds` wird beim Start aus `preferences.getInt("numLeds", DEFAULT_NUM_LEDS)` geladen
   - Was unklar ist: Exakte Zeilennummer ohne vollständiges Lesen des Setup-Blocks (>500 Zeilen)
   - Empfehlung: Grep nach `getInt.*numLeds` oder `numLeds.*getInt` zur Lokalisierung

---

## Validation Architecture

`nyquist_validation` ist in `.planning/config.json` auf `false` gesetzt. Dieser Abschnitt wird übersprungen.

**Manuelle Verifikationsstrategie (aus Phase Success Criteria):**
1. LED-01: `numLeds = 20` setzen, 1h Dauerbetrieb — kein Crash, keine korrumpierten Settings
2. LED-02: WiFi-Router kurz neu starten — LEDs blinken nicht während Reconnect
3. LED-03: WiFi-Verbindung für < 30s unterbrechen — Status-LED bleibt ruhig
4. MEM-01: `ESP.getFreeHeap()` nach 24h Dauerbetrieb — < 5% Drift
5. MEM-02: Logs nach 1h+ Betrieb — genau ein `sysHealth`-Restart-Log, keine spontanen Neustarts
6. SEC-01: `curl http://<esp32-ip>/api/export/settings | grep -i pass` — zeigt `"****"`

---

## Sources

### Primary (HIGH confidence — direkte Codeanalyse)
- `firmware/src/moodlight.cpp` — Zeilen 148, 207, 327-373, 443-449, 469-556, 559-607, 2088-2096, 2133-2145, 3730-3800, 4364-4500 — direkt gelesen
- `firmware/src/config.h` — Zeilen 1-29 — direkt gelesen
- `firmware/src/MoodlightUtils.h` — Zeilen 208-237 — direkt gelesen
- `firmware/src/MoodlightUtils.cpp` — Zeilen 1166-1200 (`isRestartRecommended`-Implementierung) — direkt gelesen

### Secondary (HIGH confidence — vorherige Research-Dokumente)
- `.planning/research/STACK.md` — Stack-Empfehlungen, JsonBufferGuard-Pattern, MAX_LEDS-Empfehlung
- `.planning/research/PITFALLS.md` — Pitfall-Analysen mit verifizierten Community-Quellen
- `.planning/research/ARCHITECTURE.md` — Build-Order, Pattern-Details, Anti-Pattern-Begründungen
- `.planning/codebase/CONCERNS.md` — Vollständige Problemliste mit Zeilennummern

### Tertiary (MEDIUM confidence — aus vorherigen Research-Dokumenten übernommen)
- Adafruit NeoPixel GitHub Issue #429, #139 — NeoPixel/WiFi-Timing-Konflikte
- ESP32 Forum + Arduino Forum — Core 3.0.x NeoPixel-Regression
- FreeRTOS Docs — Mutex-Semantik, `xSemaphoreTake`-Timeout-Verhalten

---

## Metadata

**Confidence breakdown:**
- Bugfix-Lokalisierung: HIGH — alle 6 Fixes mit exakten Zeilennummern verifiziert
- Implementierungsmuster: HIGH — Standard-C++-RAII, bestehende FreeRTOS-Patterns
- Seiteneffekte (STATUS_LED_INDEX, Import-Handler): MEDIUM — als Open Questions dokumentiert, erfordern kurze Vorab-Prüfung
- ESP32 Core-Version-Empfehlung: MEDIUM — community-verifiziert, aber keine offizielle Espressif-Quelle für genaue Versionsnummer

**Research date:** 2026-03-25
**Valid until:** 2026-06-25 (stabile ESP32-Firmware-Domain, keine schnellen Breaking Changes erwartet)
