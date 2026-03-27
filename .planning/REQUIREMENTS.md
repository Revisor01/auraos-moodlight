# Requirements: AuraOS Moodlight

**Defined:** 2026-03-27
**Core Value:** Die Firmware ist modular aufgebaut — jedes Modul hat eine klare Verantwortung, ist einzeln lesbar und änderbar, ohne den Rest des Systems zu verstehen.

## v6.0 Requirements

Requirements für Milestone v6.0: Dynamische Bewertungsskala.

### API-Migration

- [x] **API-01**: Backend nutzt Claude API (Anthropic SDK) statt OpenAI für Sentiment-Analyse
- [x] **API-02**: Anthropic API Key konfigurierbar über Umgebungsvariable (ANTHROPIC_API_KEY)
- [x] **API-03**: Bestehende Analyse-Qualität bleibt erhalten oder verbessert sich

### Dynamische Skalierung

- [ ] **SCALE-01**: Score-Mapping nutzt historische Perzentile statt fester Schwellwerte
- [ ] **SCALE-02**: Backend berechnet dynamische Schwellwerte basierend auf den letzten 7 Tagen
- [ ] **SCALE-03**: API liefert Skalierungs-Kontext (Rohwert, Perzentil, historischer Min/Max/Median)

### Prompt-Optimierung

- [ ] **PROMPT-01**: Sentiment-Prompt erzeugt ausgewogenere Scores über den vollen Bereich -1.0 bis +1.0
- [ ] **PROMPT-02**: Positive Nachrichten werden als solche erkannt (nicht nur "weniger negativ")

### ESP32 Integration

- [ ] **ESP-01**: ESP32 bezieht Schwellwerte dynamisch vom Backend statt hardcoded
- [ ] **ESP-02**: LED-Farbverteilung nutzt die volle Skala bei typischen Nachrichtenlagen

### Dashboard

- [ ] **VIS-01**: Dashboard zeigt Skalierungs-Transparenz (Perzentil-Position, historischer Bereich)

## Future Requirements

### Erweiterte Analyse (v7.0+)

- **ANAL-01**: Sentiment-Trend pro Feed
- **ANAL-02**: Historische Headline-Suche

## Out of Scope

| Feature | Reason |
|---------|--------|
| Mehrere LLM-Provider gleichzeitig | Ein Provider reicht, konfigurierbar per Env-Variable |
| Eigenes ML-Modell trainieren | LLM-API ist kosteneffizienter und einfacher |
| Echtzeit-Skalierung (pro Minute) | 7-Tage-Fenster reicht für stabile Skala |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| API-01 | Phase 16 | Complete |
| API-02 | Phase 16 | Complete |
| API-03 | Phase 16 | Complete |
| PROMPT-01 | Phase 16 | Pending |
| PROMPT-02 | Phase 16 | Pending |
| SCALE-01 | Phase 17 | Pending |
| SCALE-02 | Phase 17 | Pending |
| SCALE-03 | Phase 17 | Pending |
| ESP-01 | Phase 18 | Pending |
| ESP-02 | Phase 18 | Pending |
| VIS-01 | Phase 18 | Pending |

**Coverage:**
- v6.0 requirements: 11 total
- Mapped to phases: 11
- Unmapped: 0 ✓

---
*Requirements defined: 2026-03-27*
*Last updated: 2026-03-27 — Traceability nach Roadmap v6.0 ergänzt*
