# Project Research Summary

**Project:** AuraOS Moodlight — Combined OTA Update + Build Automation Milestone
**Domain:** ESP32 Embedded Firmware, OTA Update Architecture, Build Automation
**Researched:** 2026-03-26
**Confidence:** HIGH

## Executive Summary

Dieses Milestone fuegt dem laufenden AuraOS-Moodlight-Projekt zwei eng gekoppelte Faehigkeiten hinzu: eine kombinierte OTA-Update-Datei (eine TGZ enthaelt sowohl UI-Dateien als auch Firmware) und ein automatisiertes Build-Release-Skript mit Version-Bump. Die kritische Erkenntnis aus der Forschung ist, dass die `min_spiffs`-Partitionstabelle dem ESP32 nur 128 KB LittleFS-Speicher gibt — waehrend die kompilierte Firmware bereits 1,2 MB gross ist. Das macht jeden Ansatz, der firmware.bin in LittleFS zwischenspeichert, physisch unmoeglich. Die einzig tragfaehige Architektur ist Streaming: die kombinierte TGZ wird entweder ueber `tarGzStreamUpdater` (naming-convention-basiert) oder einen verschachtelten TGZ-Ansatz mit direktem `Update.write()`-Aufruf verarbeitet.

Der empfohlene Implementierungsweg nutzt ausschliesslich bereits installierte Bibliotheken (ESP32-targz 1.2.7, Arduino `Update` API, LittleFS) ohne neue Dependencies. Der Handler empfaengt die kombinierte TGZ und verarbeitet sie ueber zwei separate HTTP-Routen: `/update/combined-ui` nutzt `tarGzStreamExpander` mit Exclude-Filter fuer `*.bin`, `/update/combined-firmware` nutzt `tarGzStreamUpdater` mit `.ino.bin`-Naming-Convention. Ein "Full Update"-Button im Browser postet die Datei sequentiell an beide Endpunkte. Das ist die sauberste Loesung ohne Library-Modifikationen und ohne LittleFS-Staging der Firmware.

Das wichtigste Risiko ist der `esp_task_wdt_init`-Build-Blocker: die aktuelle Codebasis kompiliert nicht mit Arduino ESP32 Core 3.x. Dieser Bug muss als allererster Schritt behoben werden, da ohne funktionierendes `pio run` kein Build-Skript und kein Combined-TGZ erstellt werden kann. Darueber hinaus erfordert die LittleFS-Raumbegrenzung exzellentes Ressourcenmanagement: Cleanup-Funktionen muessen rekursiv arbeiten, Heap-Checks muessen vor Upload-Annahme stattfinden, und das Build-Skript muss Build-Fehler explizit erkennen und abbrechen statt stilles Failure zu schlucken.

---

## Key Findings

### Recommended Stack

Das Projekt benoetigt keine neuen Bibliotheken. Alle benoetigen Komponenten sind bereits installiert und funktionieren. Die zwei Kernmethoden aus ESP32-targz 1.2.7 sind: `tarGzStreamExpander` (UI-Dateien aus TGZ-Stream in LittleFS) und `tarGzStreamUpdater` (firmware.ino.bin aus TGZ-Stream direkt in U_FLASH-Partition). Die Naming Convention ist unveraenderlich: Eintraege muessen auf `.ino.bin` enden, damit `tarGzStreamUpdater` sie als Flash-Ziel erkennt.

**Core technologies:**
- `tarGzStreamExpander` (ESP32-targz 1.2.7): UI-Extraktion aus TGZ-Stream — Streaming avoids LittleFS staging for firmware
- `tarGzStreamUpdater` (ESP32-targz 1.2.7): Firmware-Flashing aus TGZ-Stream — Purpose-built for OTA, routes `.ino.bin` entries directly to U_FLASH
- Arduino `Update` API: Firmware-Flash-Endpunkt — Already used in existing firmware upload handler
- LittleFS (built-in): Dateisystem fuer UI-Dateien — Already configured, 128 KB hard limit drives all architecture decisions
- ESP8266WebServer (built-in): HTTP-Upload in Chunks — Already used, chunk-based `UPLOAD_FILE_WRITE` compatible

**Kritische Version-Constraint:**
`esp_task_wdt_init()` ist mit Arduino ESP32 Core 3.x inkompatibel — Build schlaegt fehl. Fix: Conditional compile mit `#if ESP_ARDUINO_VERSION_MAJOR >= 3` auf `esp_task_wdt_config_t`-Struct-API.

### Expected Features

Die Features dieses Milestones sind klar eingegrenzt: kein Greenfield-Design, sondern gezielte Erweiterung eines laufenden Systems.

**Must have (table stakes):**
- Fix `esp_task_wdt_init` Build-Blocker — Voraussetzung fuer alles andere
- `build-release.sh [major|minor]` bumpt Version in `config.h` und baut Combined TGZ
- ESP32-Handler fuer Combined Upload: zwei Routen (`/update/combined-ui`, `/update/combined-firmware`)
- Single Upload Form in `diagnostics.html` mit "Full Update"-Button und Version-Anzeige
- Build-Script committet Version-Bump nach verifiziertem Build (Artifact-Existenz-Pruefung)
- Unified Version: eine Versionsnummer, eine Datei auf LittleFS (`/firmware-version.txt`)

**Should have (differentiator, low effort):**
- Backward-compat: Legacy-Routen `/ui-upload` und `/update` bleiben registriert als Recovery-Pfad
- Pre-flight LittleFS-Space-Check vor Upload-Annahme
- Separate UI-TGZ und Firmware-BIN als zusaetzliche Build-Outputs fuer Fallback-Faelle
- Version-Anzeige im `diagnostics.html`-Header (bereits vorhandener `<span>` wird nur noch bevoelkert)

**Defer (v2+):**
- Pull-based OTA (falsch fuer diesen Use Case: ein Geraet im LAN)
- Signed/verified firmware images (kein Sicherheitsrisiko bei privatem Home-Device)
- GitHub Actions CI fuer Firmware (lokales Build-Skript genuegt)
- Rollback via Web-UI (erfordert Dual-Partition-Schema, separates Infrastruktur-Milestone)
- Delta/inkrementelle Firmware-Updates (RAM-Constraints auf 4 MB ESP32)

### Architecture Approach

Der Combined-Update-Handler ergaenzt das bestehende Two-Route-Muster (ein Lambda fuer Response, ein Lambda fuer Upload) um zwei neue Routen. Beide Routes nutzen denselben TGZ-Stream, aber unterschiedliche Unpacker-Methoden. Der Browser-Client postet die gleiche Datei zweimal — der Nutzer waehlt die Datei einmal; ein "Full Update"-Button triggert beide Uploads sequentiell per JS. Das ist aequivalent zum bisherigen Two-Step-Workflow, aber mit einer einzigen Datei statt zweier.

**Major components:**
1. `/update/combined-ui` Handler — `tarGzStreamExpander` mit `*.bin`-Exclude-Filter; schreibt UI-Dateien nach LittleFS
2. `/update/combined-firmware` Handler — `tarGzStreamUpdater`; erkennt `firmware.ino.bin` und streamt in U_FLASH
3. `build-release.sh` (erweitert) — Version-Bump in `config.h`, Firmware-Build, Combined-TGZ-Packaging, Git-Commit
4. `diagnostics.html` (erweitert) — Single-File-Input, "Full Update"-Button, Version-Anzeige
5. `config.h` — Single Source of Truth fuer Version zur Compile-Zeit
6. `/firmware-version.txt` auf LittleFS — Runtime-Readback (loest `/ui-version.txt` ab)

**Drei Aenderungsstellen in `moodlight.cpp`:**
- Nach `handleUiUpload()` (~Line 1530): neue `handleCombinedUi()` Funktion
- Nach `handleCombinedUi()`: neue `handleCombinedFirmware()` Funktion
- Nach bestehender `/ui-upload`-Registrierung (~Line 2075): zwei neue `server.on()` Aufrufe

### Critical Pitfalls

1. **LittleFS 128 KB Limit macht firmware.bin-Staging unmoeglich (OTA-1, KRITISCH)** — firmware.bin (1,2 MB) darf niemals in LittleFS geschrieben werden. Loesung: `tarGzStreamUpdater` mit `.ino.bin`-Entry streamt direkt in OTA-Flash ohne LittleFS-Kontakt.

2. **`esp_task_wdt_init` Build-Blocker (OTA-5)** — Kein Build ohne diesen Fix. Als allererstes umsetzen: `#if ESP_ARDUINO_VERSION_MAJOR >= 3` mit `esp_task_wdt_config_t`-Struct.

3. **Build-Script schluckt `pio run`-Fehler still (OTA-7)** — `|| true` nach dem `pio`-Call verhindert dass `set -e` bei Build-Fehler abbricht; altes firmware.bin wird als neues deployed. Loesung: `if ! pio run; then exit 1; fi` und Artifact-Existenz-Pruefung vor Git-Commit.

4. **Static-State-Bug bei Upload-Abbruch (OTA-6)** — Static-Variablen im Handler behalten Zustand zwischen Requests. Loesung: Alle State-Variablen explizit bei `UPLOAD_FILE_START` resetten; Error-Flag `combinedUpdateError = false` am Request-Start.

5. **Nicht-atomares Update bei Single-Route-Ansatz (OTA-4)** — Beim Two-Route-Ansatz kein Problem, da jede Route unabhaengig ist. Bei moeglicher kuenftiger Single-Route-Implementierung: Firmware zuerst flashen, dann UI installieren.

---

## Implications for Roadmap

Basierend auf der Forschung ergibt sich eine klare lineare Abhaengigkeitskette mit drei Phasen:

### Phase 1: Build-Blocker und Script-Grundlage

**Rationale:** `pio run` schlaegt mit aktuellem Code fehl. Kein anderer Schritt ist moeglich bevor der Compiler laeuft. Das Build-Skript muss vor der ESP32-Implementierung fehlerfrei und vertrauenswuerdig sein, damit ein verifiziertes Combined-TGZ zum Testen vorliegt.
**Delivers:** Kompilierbares Projekt + `build-release.sh` das ein verifiziertes `Combined-X.X-AuraOS.tgz` mit `firmware.ino.bin` und `ui.tgz` produziert
**Addresses:**
- Fix `esp_task_wdt_init` (OTA-5, Blocker)
- Build-Script: `pio run`-Fehlerbehandlung reparieren (OTA-7, Integrity)
- Build-Script: Version-Bump-Logik (`major|minor`) hinzufuegen
- Build-Script: Combined TGZ erzeugen (UI-Dateien + `firmware.ino.bin`)
- Build-Script: Artifact-Pruefung + Git-Commit
**Avoids:** OTA-5 (Build-Blocker), OTA-7 (stilles Build-Failure)

### Phase 2: ESP32 Combined-Update-Handler

**Rationale:** Erfordert ein real existierendes Combined-TGZ aus Phase 1 zum Testen. Die Two-Route-Architektur ist die sicherste Loesung fuer die LittleFS-Constraints ohne Library-Modifikationen.
**Delivers:** Zwei neue HTTP-Routen die zusammen ein Combined-TGZ vollstaendig verarbeiten; Legacy-Routen bleiben erhalten
**Uses:** `tarGzStreamExpander` (mit `*.bin`-Exclude-Filter), `tarGzStreamUpdater` (mit `.ino.bin`-Entry)
**Implements:** State-Reset bei `UPLOAD_FILE_START`, Heap-Check-Guard, Error-Flag-Pattern, Cleanup via `removeDirRecursive()`
**Avoids:** OTA-1 (LittleFS-Limit), OTA-2 (Heap-Minimum), OTA-6 (Static-State), OTA-8 (Cleanup-Bug)

### Phase 3: Diagnostics-UI und Unified Version

**Rationale:** UI-Aenderungen sind unabhaengig von Handler-Implementierung, koennen aber erst sinnvoll getestet werden wenn die Routen aus Phase 2 existieren. Version-Unification ist der finale Aufraeum-Schritt.
**Delivers:** `diagnostics.html` mit Single-File-Input, "Full Update"-Button (sequentielle AJAX-Posts an beide Endpunkte), Version-Anzeige; `/firmware-version.txt` als unified Version-Source
**Implements:** JS-Sequential-Upload-Logic, Version-Anzeige via `/api/firmware-version`-Endpunkt
**Avoids:** OTA-3 (Versions-Parsing-Fragilitaet — Dateiname-Parse bleibt minimal, Version kommt aus Firmware-Binary)

### Phase Ordering Rationale

- Phase 1 vor Phase 2: Kein testbares TGZ ohne funktionierenden Build. Der wdt-Fix ist der einzige Weg um `pio run` zu ermoeglichen.
- Phase 2 vor Phase 3: Die Route-URLs muessen feststehen bevor der JS-Upload-Code geschrieben wird. UI-Buttons muessen gegen echte Endpunkte testen koennen.
- Innerhalb Phase 1: wdt-Fix zuerst (absoluter Blocker), dann Build-Script-Fehlerbehandlung (Integrity-Gate), dann Version-Bump (Feature), dann TGZ-Packaging (Artefakt), dann Git-Commit-Logik.
- Innerhalb Phase 2: Route-Registrierung und State-Machine-Geruest zuerst, dann UI-Extraktion validieren (kein Firmware-Teil), dann Firmware-Flash hinzufuegen. Legacy-Routen bleiben erhalten.

### Research Flags

Phasen die keine weitere Forschung benoetigen (Standard-Patterns gut dokumentiert):
- **Phase 1:** Alle Aenderungen sind One-Liner-Fixes oder direkte Shell-Script-Erweiterungen. Kein neues Terrain.
- **Phase 3:** HTML/JS-Upload-Pattern ist Standard; Version-Anzeige via bestehendem API-Endpunkt.

Phasen die Vorsicht erfordern (kein externes Research noetig, aber Test-Iteration):
- **Phase 2:** Die genaue Interaktion von `tarGzStreamUpdater` mit dem Chunk-Upload-Pattern des ESP8266WebServer muss bei der Implementierung verifiziert werden. Die Bibliothek erwartet einen kontinuierlichen `Stream*`; der Upload-Handler liefert Chunks. Ein minimaler `ChunkStream`-Wrapper ist notwendig. Empfehlung: zuerst mit einem Test-TGZ ohne `firmware.ino.bin` validieren, dann schrittweise erweitern.

---

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Alle API-Signaturen aus lokaler Library-Installation verifiziert; Partition-Groesse aus tatsaechlicher CSV-Datei gemessen; Firmware-Groesse aus realem Release-Binary |
| Features | HIGH | Direkte Codebase-Analyse; alle Gaps aus bestehendem Code abgeleitet; keine Vermutungen |
| Architecture | HIGH | Vollstaendig aus Live-Codebase; Handler-Pattern in `moodlight.cpp` direkt inspiziert; Build-Script-Logik direkt gelesen |
| Pitfalls | HIGH | Mix aus Code-Analyse und verifizierten Community-Quellen; Partition-Constraint ist physikalisch, nicht spekulativ |

**Overall confidence:** HIGH

### Gaps to Address

- **ChunkStream-Wrapper fuer tarGzStreamUpdater:** Die Bibliothek erwartet einen kontinuierlichen `Stream*`. Die Implementierung eines minimalen Wrappers der `UPLOAD_FILE_WRITE`-Chunks als Stream prasentiert ist notwendig aber nicht dokumentiert. In Phase 2 durch Prototyp validieren bevor der volle Handler implementiert wird.

- **LittleFS Ist-Nutzung vor Upload:** Die UI-Dateien belegen laut Forschung ca. 35 KB (komprimierte TGZ). Unkomprimiert auf LittleFS sind es mehr. Der tatsaechliche `LittleFS.usedBytes()`-Wert vor dem ersten Combined-Upload sollte im Serial-Log ausgegeben werden, um den verfuegbaren Puffer fuer `/temp/combined.tgz` (~35 KB) zu kennen.

- **`.ino.bin`-Naming-Convention muss im Build-Skript dokumentiert sein:** Die Entscheidung fuer `.ino.bin` statt `.bin` (wegen `tarGzStreamUpdater`-Routing) muss als Kommentar im Build-Skript erklaert werden. Andernfalls wird das Suffix bei zukuenftigen Aenderungen versehentlich entfernt, was den Firmware-Flash-Pass stilll brechen wuerde.

---

## Sources

### Primary (HIGH confidence)
- `firmware/.pio/libdeps/esp32dev/ESP32-targz/src/libunpacker/LibUnpacker.hpp` (lokal) — Alle API-Signaturen fuer `tarGzStreamExpander`, `tarGzStreamUpdater`, Filter-Callbacks
- `/Users/simonluthe/.platformio/packages/framework-arduinoespressif32/tools/partitions/min_spiffs.csv` (lokal) — 128 KB LittleFS-Partition-Bestaetigung (0x20000)
- `releases/v9.0/Firmware-9.0-AuraOS.bin` (lokal) — 1,2 MB Firmware-Groesse (bestaetigtes physisches Limit)
- `firmware/src/moodlight.cpp` (lokal) — Bestehende Handler-Implementierung (Lines 1312-1530, 2914-2977)
- `build-release.sh` (lokal) — Bestehender Build-Flow mit `|| true`-Bug
- `.planning/PROJECT.md` (lokal) — Aktive Anforderungen und Constraints

### Secondary (MEDIUM confidence)
- [ESP32-targz GitHub](https://github.com/tobozo/ESP32-targz) — `tarGzStreamUpdater` Naming Convention (`.ino.bin` / `.spiffs.bin`)
- [ESP32-targz Update_from_gz_stream Example](https://github.com/tobozo/ESP32-targz/blob/master/examples/ESP32/Update_from_gz_stream/Update_from_gz_stream.ino) — `.ino.bin`-Suffix-Anforderung bestaetigt

---
*Research completed: 2026-03-26*
*Ready for roadmap: yes*
