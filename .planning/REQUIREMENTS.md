# Requirements: AuraOS Moodlight

**Defined:** 2026-03-27
**Core Value:** Die Firmware ist modular aufgebaut — jedes Modul hat eine klare Verantwortung, ist einzeln lesbar und änderbar, ohne den Rest des Systems zu verstehen.

## v8.0 Requirements

Requirements für Milestone v8.0: ESP32 UI-Redesign.

### CSS-Fundament

- [x] **CSS-01**: style.css nutzt CSS-Variablen konsistent mit dem Backend-Dashboard (Farben, Abstände, border-radius, Schriften)
- [x] **CSS-02**: Dark/Light Mode funktioniert weiterhin über CSS-Variablen

### Hauptseite (index.html)

- [ ] **IDX-01**: Score-Anzeige nutzt Farbkodierung wie im Dashboard (5 Farbstufen)
- [ ] **IDX-02**: Karten-Layout (border-radius 8px, kompaktes Padding) wie Dashboard
- [ ] **IDX-03**: LED-Steuerung und Helligkeit behalten volle Funktionalität

### Einstellungen (setup.html)

- [ ] **SET-01**: Formular-Design konsistent mit Dashboard-Einstellungs-Tab (Input-Felder, Labels, Buttons)
- [ ] **SET-02**: Alle bestehenden Einstellungen (WiFi, MQTT, Hardware, Farben) bleiben funktional

### Stimmung (mood.html)

- [ ] **MOOD-01**: Headlines-Darstellung mit Farbkodierung wie im Dashboard
- [ ] **MOOD-02**: Score-Verlauf und Statistiken im Dashboard-Karten-Stil

### Diagnose (diagnostics.html)

- [ ] **DIAG-01**: System-Info-Karten im Dashboard-Stil (Speicher, WiFi, Uptime)
- [ ] **DIAG-02**: Update-Bereich behält volle Funktionalität (Firmware + UI Upload)

## Out of Scope

| Feature | Reason |
|---------|--------|
| Neue Funktionen hinzufügen | Nur visuelles Redesign, keine neuen Features |
| JavaScript-Refactoring | Nur CSS/HTML, bestehende JS-Logik bleibt |
| Font Awesome entfernen | Wird weiterhin für Icons genutzt |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| CSS-01 | Phase 22 | Complete |
| CSS-02 | Phase 22 | Complete |
| IDX-01 | Phase 23 | Pending |
| IDX-02 | Phase 23 | Pending |
| IDX-03 | Phase 23 | Pending |
| SET-01 | Phase 23 | Pending |
| SET-02 | Phase 23 | Pending |
| MOOD-01 | Phase 23 | Pending |
| MOOD-02 | Phase 23 | Pending |
| DIAG-01 | Phase 23 | Pending |
| DIAG-02 | Phase 23 | Pending |

**Coverage:**
- v8.0 requirements: 11 total
- Mapped to phases: 11
- Unmapped: 0 ✓

---
*Requirements defined: 2026-03-27*
*Last updated: 2026-03-27 after roadmap creation (v8.0)*
