# Roadmap: AuraOS Moodlight

## Completed Milestones

- ✅ **v1.0: Stabilisierung** — 2 phases, 6 plans, 13 requirements (shipped 2026-03-25) → [archive](milestones/v1.0-ROADMAP.md)
- ✅ **v2.0: Combined Update + Build Automation** — 3 phases, 4 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v2.0-ROADMAP.md)

## Current Milestone

**v3.0: Firmware-Modularisierung**

## Phases

- [x] **Phase 6: Shared State Fundament** — AppState-Struct definieren, alle globalen Variablen zentralisieren (completed 2026-03-26)
- [ ] **Phase 7: Modul-Extraktion** — Alle 6 Module aus moodlight.cpp extrahieren, Firmware baut nach jeder Extraktion
- [ ] **Phase 8: Konsolidierung & Qualität** — moodlight.cpp auf max 200 Zeilen reduzieren, Qualitätssicherung

## Phase Details

### Phase 6: Shared State Fundament
**Goal**: Alle globalen Variablen sind in einem zentralen AppState-Struct zusammengefasst — Module können ab jetzt auf ein definiertes Interface zugreifen statt auf kreuz-und-quer-extern-Deklarationen
**Depends on**: Nothing (first phase of milestone)
**Requirements**: ARCH-01
**Success Criteria** (what must be TRUE):
  1. Ein `AppState`-Struct (oder globale Header-Datei `app_state.h`) existiert und enthält alle bisher verstreuten globalen Variablen
  2. `moodlight.cpp` referenziert globale Variablen ausschliesslich ueber AppState — keine losen `extern`-Deklarationen mehr
  3. `pio run` baut fehlerfrei durch — Firmware-Verhalten identisch zum Stand vor der Phase
**Plans:** 2/2 plans complete
Plans:
- [x] 06-01-PLAN.md — AppState-Struct in app_state.h definieren
- [x] 06-02-PLAN.md — Globale Variablen in moodlight.cpp durch appState.member ersetzen

### Phase 7: Modul-Extraktion
**Goal**: Alle 6 funktionalen Bereiche (WiFi, LED, Web-Server, MQTT, Settings, Sensor) sind in eigene .h/.cpp-Dateien extrahiert — jedes Modul liest und aendert ausschliesslich seinen Teil des AppState
**Depends on**: Phase 6
**Requirements**: MOD-01, MOD-02, MOD-03, MOD-04, MOD-05, MOD-06
**Success Criteria** (what must be TRUE):
  1. Sechs neue Modul-Dateien existieren (`wifi_manager`, `led_controller`, `web_server`, `mqtt_handler`, `settings_manager`, `sensor_manager`) — je eine .h und eine .cpp
  2. Nach jeder einzelnen Modul-Extraktion baut `pio run` fehlerfrei — keine Phase-interne Regression
  3. Funktionstest nach allen Extraktionen: Gerät verbindet mit WiFi, LEDs reagieren auf Sentiment, Web-Interface liefert /api/status mit korrekten Werten
  4. MQTT-Heartbeat sendet weiterhin an Home Assistant (wenn mqttEnabled)
**Plans**: TBD

### Phase 8: Konsolidierung & Qualität
**Goal**: moodlight.cpp ist auf setup(), loop() und Modul-Initialisierung reduziert — Codebase ist bereinigt von totem Code, Magic Numbers und Duplikaten
**Depends on**: Phase 7
**Requirements**: ARCH-02, ARCH-03, QUAL-01, QUAL-02, QUAL-03
**Success Criteria** (what must be TRUE):
  1. `moodlight.cpp` hat maximal 200 Zeilen — nur noch setup(), loop() und Modul-Init-Aufrufe
  2. `pio run` baut fehlerfrei ohne Linking-Fehler oder zirkulaere Dependencies
  3. Alle Magic Numbers in `moodlight.cpp` und den extrahierten Modulen sind durch benannte Konstanten in `config.h` ersetzt
  4. Kein toter oder unreachable Code in den neuen Modulen — bei Bereinigung gefundene Redundanzen sind konsolidiert
**Plans**: TBD

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 6. Shared State Fundament | 2/2 | Complete   | 2026-03-26 |
| 7. Modul-Extraktion | 0/? | Not started | - |
| 8. Konsolidierung & Qualitaet | 0/? | Not started | - |
