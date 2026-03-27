---
phase: 16-claude-api-migration
plan: "01"
subsystem: backend
tags: [anthropic, api-migration, sentiment-analysis, python]
dependency_graph:
  requires: []
  provides: [anthropic-sentiment-backend]
  affects: [background_worker.py, moodlight_extensions.py]
tech_stack:
  added: [anthropic==0.86.0]
  removed: [openai==1.70.0]
  patterns: [Anthropic SDK messages.create(), APIConnectionError/RateLimitError/APIStatusError exception handling]
key_files:
  created: []
  modified:
    - sentiment-api/requirements.txt
    - sentiment-api/docker-compose.yaml
    - sentiment-api/app.py
decisions:
  - "Claude Haiku (claude-haiku-4-5-20251001) als Ersatz für GPT-4o-mini gewählt"
  - "temperature=1.0 für Claude (statt 0.1 bei OpenAI) — Empfehlung laut Anthropic-Docs"
  - "Prompt vereinfacht (Kalibrierungs-Ankerpunkte entfernt) — Plan 02 optimiert weiter"
  - "analyze_headlines_batch ohne _openai_ im Namen — sauberes Interface für background_worker.py"
metrics:
  duration: "~2 Minuten"
  completed: "2026-03-26"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 3
---

# Phase 16 Plan 01: OpenAI → Anthropic SDK Migration Summary

**One-liner:** Anthropic SDK (claude-haiku-4-5-20251001) ersetzt OpenAI SDK (gpt-4o-mini) vollständig — 8 chirurgische Änderungen in app.py, requirements.txt, docker-compose.yaml.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | requirements.txt und docker-compose.yaml migrieren | cb05e8d | sentiment-api/requirements.txt, sentiment-api/docker-compose.yaml |
| 2 | app.py — OpenAI durch Anthropic SDK ersetzen | 59052a9 | sentiment-api/app.py |

## What Was Built

### Task 1 — Dependency Migration
- `requirements.txt`: `openai==1.70.0` → `anthropic==0.86.0`
- `docker-compose.yaml`: `OPENAI_API_KEY` → `ANTHROPIC_API_KEY` im `news-analyzer` Service

### Task 2 — app.py Migration (8 Änderungspunkte)
1. **Import:** `from openai import OpenAI, OpenAIError` → `import anthropic; from anthropic import Anthropic, APIConnectionError, RateLimitError, APIStatusError`
2. **Client-Init:** `openai_client = OpenAI(...)` → `anthropic_client = Anthropic(api_key=..., timeout=45.0)` mit ANTHROPIC_API_KEY Validierung
3. **Analysefunktion:** `analyze_sentiment_openai()` → `analyze_sentiment_claude()` mit `anthropic_client.messages.create()` und `response.content[0].text.strip()`
4. **Modell:** `gpt-4o-mini` → `claude-haiku-4-5-20251001`, `temperature=0.1` → `temperature=1.0`
5. **Batch-Funktion:** `analyze_headlines_openai_batch()` → `analyze_headlines_batch()` (Interface unverändert)
6. **Health-Check:** `health["checks"]["openai"]` → `health["checks"]["anthropic"]`
7. **News-Route Guard:** `openai_client` → `anthropic_client` im 503-Early-Return
8. **Background-Worker-Übergabe:** `analyze_function=analyze_headlines_batch`
9. **Startup-Warnung:** `openai_client` → `anthropic_client`

## Verification Results

```
1. Syntax-Check: OK
2. Anthropic-Referenzen in app.py: 14
3. openai in requirements.txt: 0
4. analyze_headlines_batch ohne _openai_: OK
```

## Interface Compatibility

`background_worker.py` bleibt unverändert — der Worker empfängt `analyze_function` als Referenz. Die umbenannte `analyze_headlines_batch()` liefert exakt dasselbe Dict-Format:
```python
{"results": [...], "total_sentiment": float, "statistics": {...}}
```

## Deviations from Plan

### Auto-angepasst

**[Cleanup] Prompt vereinfacht für initiale Migration**
- **Gefunden während:** Task 2
- **Sachverhalt:** Der alte OpenAI-Prompt enthielt ausführliche Kalibrierungs-Ankerpunkte. Der Plan gibt als neuen Prompt-Inhalt eine vereinfachte Version vor — explizit mit Hinweis "in Plan 02 weiter optimiert".
- **Entscheidung:** Vereinfachter Prompt gemäß Planvorgabe übernommen. Prompt-Optimierung ist Gegenstand von Plan 02.
- **Dateien:** sentiment-api/app.py

Keine sonstigen Abweichungen — Plan exakt umgesetzt.

## Known Stubs

Keine — alle Änderungen sind funktionale Migrationen ohne Platzhalter.

## Self-Check: PASSED

- [x] `sentiment-api/requirements.txt` enthält `anthropic==0.86.0`
- [x] `sentiment-api/docker-compose.yaml` enthält `ANTHROPIC_API_KEY`
- [x] `sentiment-api/app.py` Syntax-Check: OK
- [x] Commits cb05e8d und 59052a9 vorhanden
