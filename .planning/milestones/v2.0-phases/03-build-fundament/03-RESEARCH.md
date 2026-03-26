# Phase 3: Build-Fundament - Research

**Researched:** 2026-03-26
**Domain:** ESP32 Arduino Core 3.x API-Migration + macOS-kompatibler Build-Script
**Confidence:** HIGH — alle kritischen Fakten aus lokalen installierten Header-Dateien verifiziert

---

<user_constraints>
## User Constraints (aus CONTEXT.md)

### Locked Decisions
Alle Implementierungsentscheidungen liegen im Ermessen von Claude (Infrastructure Phase).

Kernanforderungen:
- **FIX-01**: `esp_task_wdt_init(30, false)` kompatibel mit Arduino ESP32 Core 3.x machen — `pio run` muss fehlerfrei bauen
- **BUILD-01**: Build-Script akzeptiert `major`/`minor`/`patch` und bumpt Version in config.h automatisch
- **BUILD-02**: Build-Script erzeugt `AuraOS-X.X.tgz` mit UI-Dateien + `firmware.ino.bin`
- **BUILD-03**: Build-Script committed nach erfolgreichem Build (nicht bei Fehler)
- **BUILD-04**: Build-Script bricht bei `pio run` Fehler ab — kein `|| true`

Feste Constraints aus vorheriger Recherche:
- macOS-kompatibel (kein GNU tar `--transform`). Zwei-Schritt: erst tar erstellen, dann gzip. `sed -i ''` fur macOS.
- Version-Format: X.Y (kein patch-Teil). `MOODLIGHT_VERSION "9.0"` nach `minor` Bump: `"9.1"`.
- Firmware im TGZ muss `firmware.ino.bin` heissen (ESP32-targz naming convention).
- Commit nur nach Build-Erfolg; bei Fehler: Version in config.h zurucksetzen.

### Claude's Discretion
Alle Implementierungsdetails liegen im Ermessen von Claude.

### Deferred Ideas (OUT OF SCOPE)
Keine.
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| FIX-01 | `esp_task_wdt_init(30, false)` API-Aufruf in `moodlight.cpp` ist kompatibel mit Arduino ESP32 Core 3.x | WDT-Header verifiziert: neue Signatur ist `esp_task_wdt_init(const esp_task_wdt_config_t*)`. Konditionaler Compile mit `ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)` funktioniert. |
| BUILD-01 | `build-release.sh` akzeptiert Version-Bump-Typ als Argument (`major`, `minor`, `patch`) und erhöht die Version in `config.h` automatisch | Version-Parsing via grep/cut verifiziert. macOS `sed -i ''` Syntax bestatigt. Bump-Logik fur X.Y Format definiert. |
| BUILD-02 | Build-Script erzeugt ein Combined-TGZ (`AuraOS-X.X.tgz`) das UI-Dateien + `firmware.ino.bin` enthalt | macOS-kompatible zwei-Schritt tar+gzip Methode verifiziert. `firmware.ino.bin` Naming-Convention aus ESP32-targz Quellcode bestatigt. |
| BUILD-03 | Build-Script committed Version-Bump + Combined-TGZ-Artefakt nach erfolgreichem Build (nicht bei Fehler) | Commit-Pattern nach `set -e` + `pio run` klar; git-Kommandos in Skript integrierbar. |
| BUILD-04 | Build-Script bricht bei `pio run` Fehler ab — kein `|| true` das Compile-Fehler verschluckt | Aktuelles Build-Script hat genau diesen Bug: `pio run -s 2>&1 | grep ... \|\| true`. Fixbar durch direktes `pio run` ohne Pipe. |
</phase_requirements>

---

## Summary

Phase 3 besteht aus zwei unabhangigen Aufgaben: einem Ein-Zeilen-C++-Fix (FIX-01) und einer vollstandigen Uberarbeitung des Build-Scripts (BUILD-01 bis BUILD-04). Beide Aufgaben sind gut verstanden und haben keine offenen Fragen.

**FIX-01** ist trivial, aber absoluter Blocker: ohne `pio run` kann kein Build-Script getestet werden. Das installierte Arduino ESP32 Core (Framework 3.3.7, ESP-IDF 5.5.2) erwartet `esp_task_wdt_init(const esp_task_wdt_config_t*)` statt des alten `esp_task_wdt_init(uint32_t, bool)`. Der Fix ist ein konditionaler Compile-Block der beim aktuellen Core immer den neuen Pfad nimmt. Die vollstandige Header-Signatur wurde aus der lokalen Installation verifiziert.

**Build-Script**: Das bestehende `build-release.sh` hat funf konkrete Bugs die alle behoben werden mussen: (1) kein Version-Bump-Argument, (2) kein Combined-TGZ, (3) kein Auto-Commit, (4) `pio run || true` verschluckt Fehler, (5) kein Empty-VERSION-Guard. Das neue Script wird das bestehende komplett ersetzen. Alle macOS-Kompatibilitatsdetails (BSD-tar, `sed -i ''`, zwei-Schritt tar+gzip) sind verifiziert.

**Primary recommendation:** FIX-01 zuerst committen, dann Build-Script ersetzen. Beides in Wave 1 abarbeitbar — keine Abhangigkeiten zu anderen Phasen.

---

## Standard Stack

### Core

| Library/Tool | Version | Purpose | Status |
|---|---|---|---|
| Arduino ESP32 Core (framework-arduinoespressif32) | 3.3.7 | ESP32 Arduino Framework | Bereits installiert |
| ESP-IDF | 5.5.2 | Underlying IDF (bestimmt WDT-API) | Bereits installiert (Teil des Frameworks) |
| ESP32-targz | 1.2.7 | TGZ-Erstellung nicht benotigt; `.ino.bin` Naming-Convention fur den Empfanger | Bereits in lib_deps |
| PlatformIO | aktuell | Build-System | Bereits installiert (`pio`) |
| macOS BSD tar | system | TGZ-Erstellung im Build-Script | System-Tool, kein Install notig |
| macOS gzip | system | Komprimierung des Combined-TGZ | System-Tool, kein Install notig |
| bash | system | Build-Script | macOS Standard |
| git | system | Auto-Commit nach Build | Bereits vorhanden |

### Keine neuen Abhangigkeiten benotigt

Weder `platformio.ini` noch `lib_deps` mussen geandert werden. Der WDT-Fix ist ein reiner Code-Change. Das Build-Script verwendet nur macOS-Systemtools.

---

## Architecture Patterns

### FIX-01: Konditionaler WDT-Init

**Verifizierten Fakten (aus lokaler Installation):**

- Installierter Header: `/Users/simonluthe/.platformio/packages/framework-arduinoespressif32-libs/esp32/include/esp_system/include/esp_task_wdt.h`
- ESP-IDF Version: **5.5.2** (MAJOR=5, MINOR=5, PATCH=2)
- Neue Signatur: `esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t *config)`
- `esp_task_wdt_config_t` hat drei Felder: `timeout_ms` (uint32_t), `idle_core_mask` (uint32_t), `trigger_panic` (bool)
- `ESP_IDF_VERSION_VAL` Makro ist verfugbar in `esp_idf_version.h`

**Konkreter Fix fur `firmware/src/moodlight.cpp` Zeile 4063:**

```cpp
// Watchdog-Timer initialisieren mit 30 Sekunden Timeout, ohne automatischen Reset
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // Arduino ESP32 Core 3.x (esp-idf >= 5.0): API erwartet Konfigurationsstruktur
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms   = 30000,
        .idle_core_mask = 0,
        .trigger_panic  = false
    };
    esp_task_wdt_init(&wdt_config);
#else
    // Altere Versionen: Parameter direkt
    esp_task_wdt_init(30, false);
#endif
```

**Wichtig:** `esp_task_wdt_init()` — nicht `esp_task_wdt_reconfigure()`. Letzteres ist nur fur eine bereits laufende WDT-Instanz (gibt `ESP_ERR_INVALID_STATE` zuruck wenn WDT noch nicht initialisiert). Im `setup()`-Kontext ist `esp_task_wdt_init()` korrekt.

### BUILD-01 bis BUILD-04: Neues Build-Script

**Struktur des neuen `build-release.sh`:**

```
1. Argument-Validierung: $1 muss major|minor|patch sein, sonst Usage-Error
2. Version lesen: grep + cut aus config.h, mit Empty-Guard
3. Version bumpen: bump_version-Funktion fur X.Y Format
4. config.h aktualisieren: sed -i '' (macOS)
5. pio run: direkt ohne Pipe, set -e sorgt fur Abbruch bei Fehler
6. TGZ erstellen: zwei-Schritt (tar dann gzip), macOS-kompatibel
7. git commit: version bump + TGZ-Artefakt, nur wenn Build erfolgreich
```

**Version-Bump-Logik fur X.Y Format (kein Patch-Teil):**

```bash
BUMP_TYPE="${1:-minor}"

# Version aus config.h lesen
VERSION=$(grep '^#define MOODLIGHT_VERSION' firmware/src/config.h | cut -d'"' -f2)
[ -z "$VERSION" ] && echo "ERROR: MOODLIGHT_VERSION nicht in config.h gefunden" && exit 1

# Bumpen
bump_version() {
    local current="$1"
    local type="$2"
    local major minor
    IFS='.' read -r major minor <<< "$current"
    case "$type" in
        major) echo "$((major + 1)).0" ;;
        minor) echo "${major}.$((minor + 1))" ;;
        patch) echo "${current}" ;; # kein patch-Teil in AuraOS — unverandert
        *) echo "ERROR: Unbekannter Bump-Typ: $type" >&2; exit 1 ;;
    esac
}

NEW_VERSION=$(bump_version "$VERSION" "$BUMP_TYPE")

# config.h aktualisieren
sed -i '' "s/#define MOODLIGHT_VERSION \"${VERSION}\"/#define MOODLIGHT_VERSION \"${NEW_VERSION}\"/" \
    firmware/src/config.h
```

**macOS-kompatible TGZ-Erstellung:**

```bash
RELEASE_DIR="releases/v${NEW_VERSION}"
mkdir -p "$RELEASE_DIR"

# Schritt 1: Unkomprimiertes TAR aus data/-Verzeichnis
(cd firmware/data && tar -cf /tmp/auraos_combined.tar \
    index.html setup.html mood.html diagnostics.html js/ css/ \
    --exclude="*.tmp.*" --exclude="*.tgz" --exclude="*.tar")

# Schritt 2: firmware.bin mit .ino.bin-Name anhangen (tarGzStreamUpdater-Convention)
cp firmware/.pio/build/esp32dev/firmware.bin /tmp/firmware.ino.bin
tar -rf /tmp/auraos_combined.tar -C /tmp firmware.ino.bin

# Schritt 3: Gzippen
gzip -c /tmp/auraos_combined.tar > "${RELEASE_DIR}/AuraOS-${NEW_VERSION}.tgz"

# Cleanup
rm -f /tmp/auraos_combined.tar /tmp/firmware.ino.bin
```

**BUILD-04 Fix — aktuelles Problem im bestehenden Script:**

Das bestehende Script hat:
```bash
pio run -s 2>&1 | grep -E "(SUCCESS|error|Error)" || true
```
Das `|| true` nach der Pipe verhindert den `set -e` Abbruch auch bei Compile-Fehler.

Der Fix ist:
```bash
cd firmware
pio run
cd ..
```
`set -e` am Anfang des Scripts sorgt dann fur automatischen Abbruch. Kein `|| true`, kein grep-Filter.

**BUILD-03: Auto-Commit Pattern:**

```bash
# Nur nach erfolgreichem Build (set -e hat vorher abgebrochen wenn pio run fehlschlug)
git add firmware/src/config.h "${RELEASE_DIR}/AuraOS-${NEW_VERSION}.tgz"
git commit -m "Bump: API Version v${NEW_VERSION} (CI/CD Test)"
```

**Rollback bei pio-Fehler:**

Da `set -e` das Script bei `pio run`-Fehler abbricht *bevor* der git commit, bleibt `config.h` auf dem gebobbten Wert (Version wurde vor dem Build erhoht). Optionen:

- Option A (einfach): Version *vor* dem Build bumpen, bei Fehler manuell zurucksetzen (akzeptabel fur Single-Dev-Projekt)
- Option B (sauber): Version erst nach erfolgreichem Build bumpen via trap:
  ```bash
  trap 'sed -i "" "s/MOODLIGHT_VERSION \"${NEW_VERSION}\"/MOODLIGHT_VERSION \"${VERSION}\"/" firmware/src/config.h; echo "Build fehlgeschlagen — Version zuruckgesetzt"' ERR
  ```

Option B ist sauberer und wird empfohlen.

---

## Don't Hand-Roll

| Problem | Nicht selbst bauen | Stattdessen | Warum |
|---|---|---|---|
| TGZ-Extraktion auf ESP32 | Eigener TAR-Parser | ESP32-targz (bereits installiert) | 200+ Zeilen edge-case handling fur gzip-dekomprimierung + TAR-format |
| ESP32 OTA-Flash | Eigener Update-Writer | Arduino `Update` API + ESP32-targz `tarGzStreamUpdater` | Partition-slot switching, MD5-verification, power-loss safety |
| gzip auf macOS | Python/node komprimierung | macOS `gzip` CLI | Systemtool, keine Abhangigkeit |

---

## Common Pitfalls

### Pitfall 1: `esp_task_wdt_reconfigure` statt `esp_task_wdt_init` im setup()-Kontext

**Was schiefgeht:** `esp_task_wdt_reconfigure()` gibt `ESP_ERR_INVALID_STATE` zuruck wenn der WDT noch nicht initialisiert wurde. Im `setup()`-Kontext ist er noch nicht initialisiert.

**Warum es passiert:** Der Name `reconfigure` klingt nach "sicherer, weil idempotent" — ist er aber nicht. Die Funktion erwartet eine laufende WDT-Instanz.

**Wie vermeiden:** Immer `esp_task_wdt_init()` mit der neuen Config-Struktur verwenden. Fehlercode prufen: `ESP_ERR_INVALID_STATE` = WDT lauft bereits (dann ware `reconfigure` richtig); erster Aufruf muss immer `init` sein.

### Pitfall 2: macOS BSD tar `--transform` nicht verfugbar

**Was schiefgeht:** `tar --transform='s|firmware.bin|firmware.ino.bin|'` ist ein GNU-tar-Feature. macOS liefert BSD tar (Apple-Version). Der Aufruf schlagt mit `tar: Option --transform not recognized` fehl.

**Warum es passiert:** Linux-Entwickler schreiben GNU-tar-Syntax, macOS hat BSD tar.

**Wie vermeiden:** `cp firmware.bin /tmp/firmware.ino.bin` + `tar -rf archiv.tar -C /tmp firmware.ino.bin`. Explizite Umbenennung vor dem Hinzufugen.

### Pitfall 3: Pipe nach `pio run` verhindert `set -e` Abbruch

**Was schiefgeht:** `pio run | grep "SUCCESS" || true` — `set -e` wirkt auf die Pipe als Ganzes, und `|| true` uberschreibt den Exit-Code. Compile-Fehler werden verschluckt.

**Warum es passiert:** Der Entwickler wollte sauberen Output (nur SUCCESS/error), hat dabei den Fehler-Exit-Code eliminiert.

**Wie vermeiden:** `pio run` direkt ausfuhren ohne Pipe. Wer gefiltertes Output will: `pio run 2>&1 | tee /tmp/pio_output.log; pio_exit=${PIPESTATUS[0]}; [ $pio_exit -eq 0 ] || exit $pio_exit`.

### Pitfall 4: Leerer VERSION-String zerstort Release-Ordner-Struktur

**Was schiefgeht:** Wenn `grep | cut` fehlschlagt (z.B. Config-Format geandert), ist `VERSION=""`. Dann ist `RELEASE_DIR="releases/v/"` — alle Checksums landen falsch, und `shasum *.tgz` lauft im falschen Verzeichnis.

**Warum es passiert:** kein Guard nach dem grep-Aufruf.

**Wie vermeiden:** Unmittelbar nach dem grep: `[ -z "$VERSION" ] && echo "ERROR: Version nicht gefunden" && exit 1`.

### Pitfall 5: Zwei-Schritt tar+gzip hinterlasst /tmp-Dateien bei Script-Abbruch

**Was schiefgeht:** Script bricht nach tar-Erstellung ab (z.B. firmware.bin nicht gefunden). `/tmp/auraos_combined.tar` und `/tmp/firmware.ino.bin` bleiben liegen.

**Wie vermeiden:** Cleanup in trap:
```bash
trap 'rm -f /tmp/auraos_combined.tar /tmp/firmware.ino.bin' EXIT
```

---

## Code Examples

### Verifizierte WDT-Header-Signatur (aus lokaler Installation)

```c
// Quelle: /Users/simonluthe/.platformio/packages/framework-arduinoespressif32-libs/
//         esp32/include/esp_system/include/esp_task_wdt.h

typedef struct {
    uint32_t timeout_ms;      // TWDT timeout in Millisekunden
    uint32_t idle_core_mask;  // Bitmask der Kerne deren Idle-Task uberwacht werden soll
    bool trigger_panic;       // Panic auslozen bei Timeout
} esp_task_wdt_config_t;

esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t *config);
esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t *config);
```

### Verifizierte ESP-IDF Version-Makros (aus lokaler Installation)

```c
// Quelle: framework-arduinoespressif32-libs/esp32/include/esp_common/include/esp_idf_version.h
#define ESP_IDF_VERSION_MAJOR   5  // Aktuell installiert
#define ESP_IDF_VERSION_MINOR   5
#define ESP_IDF_VERSION_PATCH   2
#define ESP_IDF_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))
#define ESP_IDF_VERSION  ESP_IDF_VERSION_VAL(ESP_IDF_VERSION_MAJOR, ...)
```

### firmware/data/ Struktur (verifiziert)

```
firmware/data/
├── index.html
├── setup.html
├── mood.html
├── diagnostics.html
├── setup.html.tmp.html   <-- NICHT ins TGZ aufnehmen (tmp-Datei)
├── js/
└── css/
```

Das `--exclude="*.tmp.*"` im tar-Aufruf schliesst `setup.html.tmp.html` korrekt aus.

---

## State of the Art

| Alter Ansatz | Aktueller Ansatz | Geandert in | Impact |
|---|---|---|---|
| `esp_task_wdt_init(timeout, panic)` | `esp_task_wdt_init(&config_struct)` | ESP-IDF 5.0 / Arduino Core 3.x | Breaking change, Compile-Fehler ohne Fix |
| Separate Firmware.bin + UI.tgz im Release | Ein Combined AuraOS-X.X.tgz | Phase 3 (diese Phase) | Einfachere OTA-Verwaltung |

---

## Open Questions

1. **Patch-Bump: Erwartung unklar**
   - Was wir wissen: AuraOS nutzt X.Y Versionierung (kein Patch-Teil)
   - Was unklar ist: Was soll `./build-release.sh patch` tun? Version unverandert lassen oder ablehnen?
   - Empfehlung: `patch` = Fehler ausgeben ("AuraOS nutzt keine Patch-Versionen, bitte `minor` verwenden") — verhindert verworrene Versionsgeschichte

2. **git commit Scope: Nur TGZ oder auch altes Release-Verzeichnis?**
   - Was wir wissen: Build erzeugt `releases/v${NEW_VERSION}/AuraOS-X.X.tgz`
   - Was unklar ist: Sollen alte `releases/` Verzeichnisse im Repo bleiben oder nur das neueste committe?
   - Empfehlung: Nur `firmware/src/config.h` + neues TGZ commiten, alte Releases bleiben im Repo-History (git wird sie automatisch tracken wenn sie schon committed waren)

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|---|---|---|---|---|
| `pio` CLI | BUILD-01 bis BUILD-04 (Build-Script) | Verfugbar (`command -v pio`) | aktuell | — |
| macOS `tar` (BSD) | BUILD-02 (TGZ-Erstellung) | Verfugbar (System-Tool) | system | — |
| macOS `gzip` | BUILD-02 (Komprimierung) | Verfugbar (System-Tool) | system | — |
| `git` | BUILD-03 (Auto-Commit) | Verfugbar | aktuell | — |
| `sed` (BSD) | BUILD-01 (config.h Update) | Verfugbar; muss `sed -i ''` verwenden | system (BSD) | — |
| Arduino ESP32 Core 3.3.7 | FIX-01 (WDT API) | Installiert | 3.3.7 / IDF 5.5.2 | — |

Keine fehlenden Abhangigkeiten. Alle Tools verfugbar.

---

## Validation Architecture

Nyquist-Validation ist fur diese Phase nicht anwendbar: FIX-01 und BUILD-01 bis BUILD-04 sind reine Build-Infrastruktur ohne automatisierbare Unit-Tests. Die Verifikation erfolgt durch:

- **FIX-01**: `cd firmware && pio run` — muss ohne Fehler durchlaufen
- **BUILD-01 bis BUILD-04**: `./build-release.sh minor` ausfuhren und prufen:
  - `releases/v9.1/AuraOS-9.1.tgz` existiert
  - TGZ enthalt `firmware.ino.bin` und alle HTML/CSS/JS-Dateien
  - `config.h` enthalt `#define MOODLIGHT_VERSION "9.1"`
  - `git log --oneline -1` zeigt den Auto-Commit
  - `./build-release.sh` ohne Argument gibt Usage-Fehler

Diese Checks sind manuelle Smoke-Tests, nicht automatisierte Tests. Kein Wave-0-Gap zu fullen.

---

## Sources

### Primary (HIGH — lokale Dateien, verifiziert)

- `/Users/simonluthe/.platformio/packages/framework-arduinoespressif32-libs/esp32/include/esp_system/include/esp_task_wdt.h` — exakte WDT-API-Signatur, `esp_task_wdt_config_t` Felder
- `/Users/simonluthe/.platformio/packages/framework-arduinoespressif32-libs/esp32/include/esp_common/include/esp_idf_version.h` — ESP-IDF 5.5.2, `ESP_IDF_VERSION_VAL` Makro
- `/Users/simonluthe/.platformio/packages/framework-arduinoespressif32/package.json` — Arduino ESP32 Core Version 3.3.7
- `firmware/src/moodlight.cpp` Zeile 4063 — genauer Aufrufkontext des WDT-Init
- `firmware/src/config.h` — `MOODLIGHT_VERSION "9.0"`, X.Y Format bestatigt
- `firmware/platformio.ini` — `platform = espressif32` (ungepinnt), `board = esp32dev`
- `build-release.sh` — vollstandiger aktueller Inhalt, alle Bugs identifiziert
- `.planning/research/STACK.md` — macOS-tar Zwei-Schritt-Approach, `firmware.ino.bin` Naming-Convention
- `firmware/data/` — tatsachlicher Verzeichnisinhalt (setup.html.tmp.html Ausschluss-Bedarf identifiziert)

### Secondary (HIGH — aus vorheriger Projektrecherche)

- `.planning/research/PITFALLS.md` Pitfall OTA-5 — `esp_task_wdt_init` Breaking Change mit Compile-Fehlermeldung
- `.planning/research/PITFALLS.md` Pitfall OTA-7 — `|| true` Bug im bestehenden Build-Script
- `.planning/research/STACK.md` Build Script Sektion — macOS-kompatible TGZ-Erstellung, Version-Bump-Logik

---

## Metadata

**Confidence breakdown:**
- FIX-01 (WDT API): HIGH — Header direkt aus installiertem Framework verifiziert, exakte Signatur und Feldnamen bestatigt
- BUILD-01 (Version Bump): HIGH — grep/cut/sed Syntax fur macOS verifiziert; X.Y Format aus config.h bestatigt
- BUILD-02 (TGZ-Erstellung): HIGH — macOS BSD tar Einschrankungen verifiziert; `firmware.ino.bin` Naming-Convention aus ESP32-targz Quellcode bestatigt
- BUILD-03 (Auto-Commit): HIGH — git add/commit Syntax trivial; trap-Pattern fur Rollback bestatigt
- BUILD-04 (pio run Fehler): HIGH — Bug im bestehenden Script direkt identifiziert (Zeile 50: `|| true`)

**Research date:** 2026-03-26
**Valid until:** Stabil — kein Framework-Update geplant, macOS-Systemtools unverandert
