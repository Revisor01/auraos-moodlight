# Roadmap: AuraOS Moodlight

## Completed Milestones

- ✅ **v1.0: Stabilisierung** — 2 phases, 6 plans, 13 requirements (shipped 2026-03-25) → [archive](milestones/v1.0-ROADMAP.md)

## Current Milestone

### v2.0: Combined Update + Build Automation

**Milestone Goal:** Ein einziger Klick baut ein Combined Update (UI + Firmware), bumpt die Version und committet — kein manuelles Versionieren, kein getrennter Upload.

## Phases

- [x] **Phase 3: Build-Fundament** - Build-Blocker fixen + Build-Script das ein verifiziertes Combined-TGZ erzeugt (completed 2026-03-26)
- [ ] **Phase 4: Combined-Update-Handler** - ESP32 verarbeitet Combined-TGZ über zwei neue HTTP-Routen
- [ ] **Phase 5: Diagnostics-UI** - Ein Upload-Feld, ein Button, eine Versionsanzeige

## Phase Details

### Phase 3: Build-Fundament
**Goal**: Das Projekt baut fehlerfrei und `./build-release.sh minor` erzeugt ein versioniertes `Combined-X.X-AuraOS.tgz` das UI-Dateien und `firmware.ino.bin` enthält
**Depends on**: Nothing (first phase of this milestone)
**Requirements**: FIX-01, BUILD-01, BUILD-02, BUILD-03, BUILD-04
**Success Criteria** (what must be TRUE):
  1. `pio run` bricht ohne Fehler ab — kein `esp_task_wdt_init`-Compilerfehler
  2. `./build-release.sh minor` bumpt die Version in `config.h` und bricht bei Build-Fehler explizit ab
  3. Das erzeugte `Combined-X.X-AuraOS.tgz` enthält sowohl UI-Dateien als auch `firmware.ino.bin`
  4. Build-Script committed Version-Bump + Artefakt nur nach verifiziertem Build
**Plans:** 2/2 plans complete
Plans:
- [x] 03-01-PLAN.md — WDT-API-Fix (esp_task_wdt_init kompatibel mit ESP-IDF 5.x)
- [x] 03-02-PLAN.md — Build-Release-Script komplett ersetzen (Version-Bump + Combined-TGZ + Auto-Commit)

### Phase 4: Combined-Update-Handler
**Goal**: Der ESP32 verarbeitet ein Combined-TGZ vollständig: UI-Dateien werden nach LittleFS extrahiert, `firmware.ino.bin` wird direkt in den Flash gestreamt — ohne LittleFS-Staging der Firmware
**Depends on**: Phase 3
**Requirements**: OTA-01, OTA-02, OTA-03, OTA-04
**Success Criteria** (what must be TRUE):
  1. POST an `/update/combined-ui` extrahiert alle UI-Dateien aus dem Combined-TGZ nach LittleFS und überspringt `*.bin`-Einträge
  2. POST an `/update/combined-firmware` streamt `firmware.ino.bin` direkt in den OTA-Flash ohne LittleFS-Kontakt
  3. Nach erfolgreichem Firmware-Flash enthält `/firmware-version.txt` auf LittleFS die neue Version
  4. Ein unterbrochener Upload hinterlässt keinen inkonsistenten State — erneuter Upload funktioniert
**Plans**: TBD

### Phase 5: Diagnostics-UI
**Goal**: Die Update-Sektion in `diagnostics.html` hat einen einzigen Datei-Picker und einen "Full Update"-Button der beide Uploads sequentiell ausführt, sowie eine einzige Versionsanzeige
**Depends on**: Phase 4
**Requirements**: UI-01, UI-02, UI-03
**Success Criteria** (what must be TRUE):
  1. Nutzer wählt eine Combined-TGZ-Datei und klickt "Full Update" — kein zweiter Upload nötig
  2. Fortschrittsanzeige zeigt klar welcher Schritt läuft (1/2 UI-Installation, 2/2 Firmware-Flash)
  3. Eine einzige Versionsanzeige zeigt die aktuelle Gesamtversion (nicht getrennt Firmware/UI)
**Plans**: TBD
**UI hint**: yes

## Progress

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 3. Build-Fundament | v2.0 | 2/2 | Complete   | 2026-03-26 |
| 4. Combined-Update-Handler | v2.0 | 0/? | Not started | - |
| 5. Diagnostics-UI | v2.0 | 0/? | Not started | - |
