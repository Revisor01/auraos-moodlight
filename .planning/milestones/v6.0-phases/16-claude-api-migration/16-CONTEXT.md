# Phase 16: Claude API Migration - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

OpenAI SDK durch Anthropic SDK ersetzen. Sentiment-Analyse läuft über Claude Haiku statt GPT-4o-mini. Prompt wird optimiert für ausgewogenere Score-Verteilung über den vollen Bereich -1.0 bis +1.0. Positive Nachrichten sollen als positiv erkannt werden, nicht nur als "weniger negativ".

</domain>

<decisions>
## Implementation Decisions

### API-Migration
- Anthropic SDK (anthropic Python-Paket) statt openai
- Claude Haiku (claude-haiku-4-5-20251001) für Kosteneffizienz (~$0.25/1M Input-Tokens)
- ANTHROPIC_API_KEY statt OPENAI_API_KEY als Umgebungsvariable
- OpenAI SDK kann aus requirements.txt entfernt werden
- Bestehende Analyse-Funktion analyze_headlines_openai_batch() umbenennen/refactoren

### Prompt-Optimierung
- Aktueller Prompt erzeugt Scores die bei -0.15 bis -0.35 clustern
- Ziel: Voller Bereich -1.0 bis +1.0 nutzen
- Nachrichten über Erfolge, Fortschritte, gute Wirtschaftsdaten sollen deutlich positiv sein
- Kalibrierungs-Beispiele im Prompt: "Friedensverhandlungen beginnen" → +0.7, "Wirtschaftswachstum über Erwartungen" → +0.5
- Neutrale Nachrichten (Sportergebnisse, Wetter) → 0.0 bis +0.1

### Claude's Discretion
- Genaue Prompt-Formulierung (innerhalb der Kalibrierungs-Vorgaben)
- Batch-Strategie (alle Headlines in einem Request oder aufgeteilt)
- Error-Handling bei API-Ausfällen

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/app.py — analyze_headlines_openai_batch() (Zeile ~180-260)
- sentiment-api/background_worker.py — ruft die Analyse-Funktion auf
- sentiment-api/requirements.txt — openai==1.70.0 (wird zu anthropic)
- sentiment-api/docker-compose.yaml — OPENAI_API_KEY Umgebungsvariable

### Established Patterns
- Analyse gibt Dict mit results[] zurück: headline, source, sentiment, strength, feed_id
- tanh(raw * 2.0) als Verstärkungsfunktion
- Logging der Roh-Werte und Endergebnisse

### Integration Points
- app.py: Import openai → anthropic
- app.py: analyze_headlines_openai_batch() → analyze_headlines_batch() (umbenennen)
- background_worker.py: Referenz auf Analyse-Funktion
- docker-compose.yaml: OPENAI_API_KEY → ANTHROPIC_API_KEY
- requirements.txt: openai → anthropic

</code_context>

<specifics>
## Specific Ideas

- ANTHROPIC_API_KEY wird vom User auf dem Server in docker-compose.yaml gesetzt

</specifics>

<deferred>
## Deferred Ideas

Keine.

</deferred>
