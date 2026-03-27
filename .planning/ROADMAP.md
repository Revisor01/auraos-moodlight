# Roadmap: AuraOS Moodlight

## Completed Milestones

- ✅ **v1.0: Stabilisierung** — 2 phases, 6 plans, 13 requirements (shipped 2026-03-25) → [archive](milestones/v1.0-ROADMAP.md)
- ✅ **v2.0: Combined Update + Build Automation** — 3 phases, 4 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v2.0-ROADMAP.md)
- ✅ **v3.0: Firmware-Modularisierung** — 3 phases, 11 plans, 12 requirements (shipped 2026-03-26) → [archive](milestones/v3.0-ROADMAP.md)
- ✅ **v4.0: Konfigurierbare RSS-Feeds** — 3 phases, 4 plans, 9 requirements (shipped 2026-03-26) → [archive](milestones/v4.0-ROADMAP.md)
- ✅ **v5.0: Schlagzeilen-Transparenz & Dashboard** — 4 phases, 7 plans, 12 requirements (shipped 2026-03-27) → [archive](milestones/v5.0-ROADMAP.md)

## Current Milestone

### 🚧 v6.0 Dynamische Bewertungsskala (In Progress)

**Milestone Goal:** Scoring-Pipeline von OpenAI auf Claude API umstellen, Bewertungsskala dynamisch auf historische Perzentile stützen, volle LED-Farbpalette ausreizen.

## Phases

- [ ] **Phase 16: Claude API Migration** - OpenAI durch Anthropic SDK ersetzen, Prompt optimieren
- [ ] **Phase 17: Dynamische Skalierung** - Perzentil-basiertes Score-Mapping und Backend-Transparenz
- [ ] **Phase 18: ESP32 + Dashboard Integration** - Dynamische Schwellwerte auf dem Gerät, Skalierungs-Anzeige im Dashboard

## Phase Details

### Phase 16: Claude API Migration
**Goal**: Backend analysiert Sentiment ausschließlich über Claude API (Anthropic SDK), mit optimiertem Prompt für ausgewogenere Scores
**Depends on**: Phase 15
**Requirements**: API-01, API-02, API-03, PROMPT-01, PROMPT-02
**Success Criteria** (what must be TRUE):
  1. Backend startet ohne OpenAI-Key und analysiert erfolgreich mit Anthropic SDK
  2. ANTHROPIC_API_KEY ist per Umgebungsvariable konfigurierbar und wird beim Start validiert
  3. Sentiment-Scores verteilen sich erkennbar über den vollen Bereich -1.0 bis +1.0 (nicht nur -0.3 bis +0.1)
  4. Positive Nachrichtenlagen erzeugen Scores > +0.3, nicht nur "weniger negativ"
**Plans**: 2 plans
Plans:
- [ ] 16-01-PLAN.md — OpenAI SDK durch Anthropic SDK ersetzen (requirements.txt, docker-compose.yaml, app.py)
- [ ] 16-02-PLAN.md — Optimierten Sentiment-Prompt mit Kalibrierungsbeispielen einbauen

### Phase 17: Dynamische Skalierung
**Goal**: Score-Mapping basiert auf historischen Perzentilen der letzten 7 Tage, API liefert vollständigen Skalierungs-Kontext
**Depends on**: Phase 16
**Requirements**: SCALE-01, SCALE-02, SCALE-03
**Success Criteria** (what must be TRUE):
  1. `/api/moodlight/current` enthält Felder für Rohwert, Perzentil-Position und historischen Min/Max/Median
  2. Bei gleichem Rohwert ändert sich der skalierte LED-Index, wenn sich der historische Bereich der letzten 7 Tage verschiebt
  3. Backend berechnet dynamische Schwellwerte aus realen DB-Daten, nicht aus festen Konstanten
**Plans**: TBD

### Phase 18: ESP32 + Dashboard Integration
**Goal**: ESP32 bezieht Schwellwerte dynamisch vom Backend und nutzt die volle Farbpalette; Dashboard zeigt Skalierungs-Kontext transparent an
**Depends on**: Phase 17
**Requirements**: ESP-01, ESP-02, VIS-01
**Success Criteria** (what must be TRUE):
  1. ESP32 liest Schwellwerte aus der API-Antwort und mappt LED-Farben ohne hartcodierte Grenzen
  2. Bei typischen Nachrichtenlagen leuchten alle 5 Farbstufen im Wochenverlauf sichtbar auf
  3. Dashboard zeigt für den aktuellen Score: Rohwert, Perzentil-Rang und historischen Bereich
**UI hint**: yes

## Progress

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 16. Claude API Migration | v6.0 | 0/2 | Planning done | - |
| 17. Dynamische Skalierung | v6.0 | 0/? | Not started | - |
| 18. ESP32 + Dashboard Integration | v6.0 | 0/? | Not started | - |
