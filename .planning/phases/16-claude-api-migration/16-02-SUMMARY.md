---
phase: 16-claude-api-migration
plan: "02"
subsystem: backend
tags: [anthropic, prompt-engineering, sentiment-analysis, kalibrierung]
dependency_graph:
  requires: [anthropic-sentiment-backend]
  provides: [calibrated-sentiment-prompt]
  affects: [sentiment-api/app.py]
tech_stack:
  added: []
  removed: []
  patterns: [Kalibrierungs-Ankerpunkte, Anti-Bias-Anweisung, Tonalitäts-Bewertung]
key_files:
  created: []
  modified:
    - sentiment-api/app.py
decisions:
  - "8 Ankerpunkte (4 negativ, 2 neutral, 2 positiv) als Kalibrierungsbeispiele in den Prompt eingebaut"
  - "Anti-Bias-Anweisung explizit formuliert: kein negativer Bias, voller Bereich nutzen"
  - "Tonalitäts-Anweisung: Bewertung der Schlagzeilen-Tonalität, nicht des allgemeinen Themas"
metrics:
  duration: "~2 Minuten"
  completed: "2026-03-27"
  tasks_completed: 1
  tasks_total: 2
  files_modified: 1
---

# Phase 16 Plan 02: Optimierter Sentiment-Prompt Summary

**One-liner:** Kalibrierter Prompt mit 8 Ankerpunkten und Anti-Bias-Anweisung in analyze_sentiment_claude() — Scores sollen jetzt den vollen Bereich -1.0 bis +1.0 ausschöpfen statt im negativen Bereich zu clustern.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Optimierten Prompt in analyze_sentiment_claude() einbauen | 45a3cf9 | sentiment-api/app.py |
| 2 | Deployment und manuelle Prompt-Verifikation | — | — |

## What Was Built

### Task 1 — Kalibrierter Prompt

Der `prompt_lines`-Block in `analyze_sentiment_claude()` wurde ersetzt. Statt des einfachen Platzhalter-Prompts aus Plan 01 enthält die Funktion jetzt:

**KALIBRIERUNG-Abschnitt (8 Ankerpunkte):**
- `'Flugzeugabsturz mit 200 Toten' → -0.9`
- `'Wirtschaftskrise verschärft sich' → -0.6`
- `'Regierung schließt neue Haushaltslücke' → -0.3`
- `'Wetterbericht für die Woche' → 0.0`
- `'Forschungsergebnisse veröffentlicht' → 0.0`
- `'Wirtschaftswachstum über Erwartungen' → +0.5`
- `'Friedensverhandlungen beginnen' → +0.7`
- `'Historischer Durchbruch bei Krebstherapie' → +0.9`

**WICHTIG-Abschnitt (Anti-Bias + Tonalität):**
- Bewerte die TONALITÄT der Schlagzeile, nicht das allgemeine Thema
- Sachliche Berichte über Probleme sind NICHT automatisch -0.5 oder schlechter
- Nutze den vollen Bereich: positive Nachrichten sollen positive Werte bekommen
- Im Zweifel: ausgewogene Einschätzung, kein negativer Bias

Der Rest der Funktion (API-Call, Response-Parsing, Error-Handling) blieb unverändert.

### Task 2 — Deployment (übersprungen)

Task 2 ist ein `checkpoint:human-verify` Gate — Deployment und Verifikation übernimmt der User. Gemäß Ausführungsanweisung als "übersprungen — User handles deployment" markiert.

**Deployment-Anleitung für den User** (aus dem Plan):
1. `ANTHROPIC_API_KEY=sk-ant-...` in `/opt/auraos-moodlight/sentiment-api/.env` setzen
2. `git push` → CI/CD baut Image → Portainer deployt automatisch
3. Container-Logs prüfen: `"Anthropic Client erfolgreich initialisiert."` muss erscheinen
4. Health-Check: `GET http://analyse.godsapp.de/api/health` → `"anthropic": {"status": "healthy", "available": true}`

## Verification Results

```
Syntax: OK
Prompt-Check: OK
- KALIBRIERUNG: vorhanden
- Friedensverhandlungen beginnen → +0.7: vorhanden
- negativer Bias (Anti-Bias-Anweisung): vorhanden
- +0.7 Ankerpunkt: vorhanden
```

## Deviations from Plan

**Task 2 übersprungen — User handles deployment**
- Laut Ausführungsanweisung wird Task 2 (checkpoint:human-verify) nicht durch den Executor ausgeführt
- Das Deployment erfolgt durch den User per GitHub Push + Portainer CI/CD
- Im SUMMARY als "skipped — user handles deployment" dokumentiert

Keine sonstigen Abweichungen — Task 1 exakt nach Plan umgesetzt.

## Known Stubs

Keine — der Prompt ist vollständig eingebaut. Das Deployment-Ergebnis (Score-Verteilung) kann erst nach dem ersten Produktions-Lauf bewertet werden.

## Self-Check: PASSED

- [x] `sentiment-api/app.py` enthält `KALIBRIERUNG`
- [x] `sentiment-api/app.py` enthält `Friedensverhandlungen beginnen`
- [x] `sentiment-api/app.py` enthält `negativer Bias`
- [x] Syntax-Check: OK
- [x] Commit 45a3cf9 vorhanden
