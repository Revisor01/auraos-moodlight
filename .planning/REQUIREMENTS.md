# Requirements: AuraOS Moodlight — Combined Update + Build Automation

**Defined:** 2026-03-26
**Core Value:** Ein einziger Klick baut ein Combined Update (UI + Firmware), bumpt die Version und committet.

## v1 Requirements

### Build & Versionierung

- [ ] **BUILD-01**: `build-release.sh` akzeptiert Version-Bump-Typ als Argument (`major`, `minor`, `patch`) und erhöht die Version in `config.h` automatisch
- [ ] **BUILD-02**: Build-Script erzeugt ein Combined-TGZ (`AuraOS-X.X.tgz`) das UI-Dateien + `firmware.ino.bin` enthält
- [ ] **BUILD-03**: Build-Script committed Version-Bump + Combined-TGZ-Artefakt nach erfolgreichem Build (nicht bei Fehler)
- [ ] **BUILD-04**: Build-Script bricht bei `pio run` Fehler ab — kein `|| true` das Compile-Fehler verschluckt

### ESP32 Combined Update Handler

- [ ] **OTA-01**: Neuer HTTP-Endpoint für Combined-UI-Upload: extrahiert UI-Dateien aus dem Combined-TGZ nach LittleFS, überspringt `*.bin` Dateien
- [ ] **OTA-02**: Neuer HTTP-Endpoint für Combined-Firmware-Flash: streamt `firmware.ino.bin` aus dem Combined-TGZ direkt via `Update.write()` ohne LittleFS-Staging
- [ ] **OTA-03**: Nach erfolgreichem Firmware-Flash wird die Version aus einer `VERSION.txt` im TGZ-Root gelesen und in `firmware-version.txt` + `ui-version.txt` gespeichert
- [ ] **OTA-04**: Upload-Handler hat sauberen State-Reset bei Abbruch — keine stale static-Variablen vom vorherigen Upload

### Diagnostics UI

- [ ] **UI-01**: Update-Sektion in `diagnostics.html` hat einen Datei-Picker und einen "Full Update"-Button — der Button führt sequentiell erst UI-Upload, dann Firmware-Flash aus
- [ ] **UI-02**: Fortschrittsanzeige zeigt welcher Schritt gerade läuft (1/2 UI-Installation, 2/2 Firmware-Flash)
- [ ] **UI-03**: Eine einzige Versionsanzeige (nicht getrennt Firmware/UI) zeigt die aktuelle Gesamtversion

### Build-Blocker

- [x] **FIX-01**: `esp_task_wdt_init(30, false)` API-Aufruf in `moodlight.cpp` ist kompatibel mit Arduino ESP32 Core 3.x — `pio run` baut erfolgreich

## v2 Requirements

Deferred to future milestones.

### Firmware Modularisierung (Milestone 3)

- **MOD-01**: `moodlight.cpp` ist in logische Module aufgeteilt
- **MOD-02**: Globale Variablen sind durch Klassen-Interfaces ersetzt

### HTTPS (Milestone 4)

- **HTTPS-01**: Backend-Kommunikation läuft über HTTPS
- **HTTPS-02**: ESP32 verwendet `WiFiClientSecure`

## Out of Scope

| Feature | Reason |
|---------|--------|
| Auto-OTA (Pull-basiert) | Kein Server-Push, manueller Upload reicht |
| Rollback-Mechanismus | ESP32 hat kein Dual-Partition-Rollback im min_spiffs Schema |
| Getrennte UI/Firmware-Versionen | Combined Update = eine Version für beides |
| GitHub Releases Integration | Overkill für ein Single-Device-Projekt |
| Authentifizierung für Upload | Privates Heimnetz, kein Bedarf |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| FIX-01 | Phase 3 | Complete |
| BUILD-01 | Phase 3 | Pending |
| BUILD-02 | Phase 3 | Pending |
| BUILD-03 | Phase 3 | Pending |
| BUILD-04 | Phase 3 | Pending |
| OTA-01 | Phase 4 | Pending |
| OTA-02 | Phase 4 | Pending |
| OTA-03 | Phase 4 | Pending |
| OTA-04 | Phase 4 | Pending |
| UI-01 | Phase 5 | Pending |
| UI-02 | Phase 5 | Pending |
| UI-03 | Phase 5 | Pending |

**Coverage:**
- v1 requirements: 12 total
- Mapped to phases: 12
- Unmapped: 0 ✓

---
*Requirements defined: 2026-03-26*
*Last updated: 2026-03-26 after roadmap creation*
