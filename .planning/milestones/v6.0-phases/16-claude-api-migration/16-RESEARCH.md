# Phase 16: Claude API Migration - Research

**Researched:** 2026-03-26
**Domain:** Python SDK swap (openai → anthropic) + Prompt engineering for balanced sentiment scores
**Confidence:** HIGH

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- Anthropic SDK (`anthropic` Python-Paket) statt `openai`
- Claude Haiku (`claude-haiku-4-5-20251001`) für Kosteneffizienz (~$0.25/1M Input-Tokens)
- `ANTHROPIC_API_KEY` statt `OPENAI_API_KEY` als Umgebungsvariable
- OpenAI SDK kann aus `requirements.txt` entfernt werden
- Bestehende Analyse-Funktion `analyze_headlines_openai_batch()` umbenennen/refactoren
- Prompt-Kalibrierung: "Friedensverhandlungen beginnen" → +0.7, "Wirtschaftswachstum über Erwartungen" → +0.5
- Neutrale Nachrichten (Sportergebnisse, Wetter) → 0.0 bis +0.1

### Claude's Discretion
- Genaue Prompt-Formulierung (innerhalb der Kalibrierungs-Vorgaben)
- Batch-Strategie (alle Headlines in einem Request oder aufgeteilt)
- Error-Handling bei API-Ausfällen

### Deferred Ideas (OUT OF SCOPE)
Keine.
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| API-01 | Backend nutzt Claude API (Anthropic SDK) statt OpenAI für Sentiment-Analyse | SDK-Swap vollständig dokumentiert, API-Syntax bekannt |
| API-02 | Anthropic API Key konfigurierbar über Umgebungsvariable (ANTHROPIC_API_KEY) | docker-compose.yaml Änderung + app.py Client-Init dokumentiert |
| API-03 | Bestehende Analyse-Qualität bleibt erhalten oder verbessert sich | Gleiche Response-Struktur, verbesserter Prompt für ausgewogenere Scores |
| PROMPT-01 | Sentiment-Prompt erzeugt ausgewogenere Scores über den vollen Bereich -1.0 bis +1.0 | Kalibrierungs-Ankerpunkte + Gegenmaßnahmen gegen negativen Bias dokumentiert |
| PROMPT-02 | Positive Nachrichten werden als solche erkannt (nicht nur "weniger negativ") | Explizite Positivbeispiele im Prompt + Asymmetrie-Warnung an Modell |
</phase_requirements>

---

## Summary

Phase 16 tauscht den OpenAI SDK gegen den Anthropic SDK aus und optimiert gleichzeitig den Sentiment-Analyse-Prompt. Die technische Migration ist ein chirurgischer Eingriff in `app.py`: drei Stellen (Import, Client-Init, API-Call) plus `requirements.txt` und `docker-compose.yaml`. Der gesamte Rest des Systems — `background_worker.py`, die Rückgabestruktur, die Datenbank, Redis, das ESP32-Polling — bleibt unverändert.

Die eigentliche Komplexität liegt im Prompt. Der aktuelle Prompt produziert trotz expliziter Skala Scores, die bei -0.15 bis -0.35 clustern (97% negativ, 2% positiv). Das Problem ist strukturell: LLMs neigen bei Nachrichtenschlagzeilen zu negativem Bias, weil Nachrichten überwiegend Probleme thematisieren. Ein besserer Prompt braucht drei Elemente: (1) asymmetrische Kalibrierungs-Ankerpunkte die explizit Positivbeispiele zeigen, (2) eine Anweisung zur Bewertung der sprachlichen Tonalität statt des Themas, und (3) eine explizite Warnung gegen negativen Systematikfehler.

Die `tanh(raw * 2.0)` Verstärkungsfunktion bleibt unverändert — sie ist für Phase 17 (dynamische Skalierung) relevant, nicht für diese Phase.

**Primary recommendation:** Anthropic SDK 0.86.0 installieren, `analyze_sentiment_openai()` durch eine funktional äquivalente Funktion ersetzen, Prompt mit kalibrierten Ankerbeispielen ausstatten.

---

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| anthropic | 0.86.0 | Anthropic API Client (sync/async, Pydantic models) | Offizieller SDK, auto-retry, typed responses |

### Replacing
| Removed | Version | Reason |
|---------|---------|--------|
| openai | 1.70.0 | Wird durch anthropic ersetzt — kein Dual-Provider |

**Installation:**
```bash
pip install anthropic==0.86.0
```

**Version verification (2026-03-26):**
```
anthropic 0.86.0 — aktuellste Version auf PyPI, verifiziert.
```

---

## Architecture Patterns

### Existing Code Structure (relevant files)

```
sentiment-api/
├── app.py                  # Enthält analyze_sentiment_openai() + analyze_headlines_openai_batch()
├── background_worker.py    # Ruft analyze_headlines_openai_batch() als Referenz auf (Zeile 214)
├── shared_config.py        # get_sentiment_category() — unverändert
├── requirements.txt        # openai==1.70.0 → anthropic==0.86.0
└── docker-compose.yaml     # OPENAI_API_KEY → ANTHROPIC_API_KEY
```

### Pattern 1: Anthropic SDK Client Init
**What:** Client wird analog zu OpenAI global initialisiert, liest API Key aus Umgebungsvariable.
**When to use:** Einmaliger Init beim Modulimport — genau wie bisher mit OpenAI.

```python
# Source: https://platform.claude.com/docs/en/api/sdks/python
import anthropic
from anthropic import Anthropic, APIConnectionError, RateLimitError, APIStatusError

anthropic_api_key = os.environ.get("ANTHROPIC_API_KEY")
anthropic_client = None
if anthropic_api_key:
    try:
        anthropic_client = Anthropic(api_key=anthropic_api_key, timeout=45.0)
        logging.info("Anthropic Client erfolgreich initialisiert.")
    except Exception as e:
        logging.error(f"Fehler bei der Initialisierung des Anthropic Clients: {e}")
else:
    logging.error("ANTHROPIC_API_KEY Umgebungsvariable nicht gesetzt!")
```

### Pattern 2: messages.create() — Direkter Ersatz für chat.completions.create()

**OpenAI (bisherig):**
```python
response = openai_client.chat.completions.create(
    model="gpt-4o-mini",
    messages=[{"role": "user", "content": full_prompt}],
    temperature=0.1,
    max_tokens=len(headlines_batch) * 15,
    timeout=45.0
)
raw_response_content = response.choices[0].message.content.strip()
```

**Anthropic (neu):**
```python
# Source: https://platform.claude.com/docs/en/api/sdks/python
response = anthropic_client.messages.create(
    model="claude-haiku-4-5-20251001",
    max_tokens=len(headlines_batch) * 15,
    temperature=1.0,  # Haiku: 0.0 nicht erlaubt — Minimum ist 1.0 (oder extended_thinking)
    messages=[{"role": "user", "content": full_prompt}]
)
raw_response_content = response.content[0].text.strip()
```

**Wichtige Unterschiede:**
- `response.choices[0].message.content` → `response.content[0].text`
- `temperature=0.1` → `temperature=1.0` (Haiku erlaubt kein Temperature < 1.0 ohne extended_thinking)
- `timeout` gehört zum Client-Init, nicht zum einzelnen Call
- Kein `timeout` Parameter in `messages.create()` — wird über `Anthropic(timeout=45.0)` gesetzt

### Pattern 3: Error Handling

```python
# Source: https://platform.claude.com/docs/en/api/sdks/python
from anthropic import APIConnectionError, RateLimitError, APIStatusError

try:
    response = anthropic_client.messages.create(...)
    raw_response_content = response.content[0].text.strip()
except APIConnectionError as e:
    logging.error(f"Anthropic API nicht erreichbar: {e}")
    return [0.0] * len(headlines_batch)
except RateLimitError as e:
    logging.error(f"Anthropic Rate-Limit erreicht: {e}")
    return [0.0] * len(headlines_batch)
except APIStatusError as e:
    logging.error(f"Anthropic API Fehler {e.status_code}: {e.response}")
    return [0.0] * len(headlines_batch)
except Exception as e:
    logging.error(f"Unerwarteter Fehler bei der Anthropic Analyse: {e}")
    return [0.0] * len(headlines_batch)
```

### Pattern 4: Health-Check Endpoint anpassen

Der `/api/health` Endpoint in `app.py` prüft aktuell `openai_client is not None`. Diese Prüfung muss auf `anthropic_client` umgestellt werden. Auch der Label-Text "openai" → "anthropic".

### Pattern 5: Function Rename

`background_worker.py` referenziert die Funktion per Name (Zeile 214):
```python
start_background_worker(
    app=app,
    analyze_function=analyze_headlines_openai_batch,  # ← dieser Name muss mitgeändert werden
    interval_seconds=1800
)
```

Alle drei Stellen synchron umbenennen: Definition in `app.py`, Import-Aufruf in `app.py`, und die Übergabe als `analyze_function`.

### Anti-Patterns to Avoid
- **`temperature=0.1` bei Haiku:** Haiku unterstützt kein Temperature < 1.0 ohne `extended_thinking: true`. Immer `temperature=1.0` verwenden (bei deterministischem Bedarf: Anweisung im Prompt, nicht per Temperature).
- **`timeout` in `messages.create()`:** Nicht möglich — Timeout wird beim Client-Objekt konfiguriert.
- **`response.choices[0].message.content`:** OpenAI-Syntax. Bei Anthropic ist es `response.content[0].text`.
- **Log-Meldungen mit "OpenAI" belassen:** Alle Strings in `app.py` die "OpenAI" oder "gpt-4o-mini" enthalten müssen auf "Anthropic"/"Claude Haiku" geändert werden.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Retry-Logik bei Rate-Limits | Eigene Retry-Schleife | Anthropic SDK built-in (max_retries=2 default) | SDK retried automatisch 429, 408, 5xx mit exponential backoff |
| Timeout-Handling | try/except mit threading.Timer | `Anthropic(timeout=45.0)` | SDK raises `APITimeoutError` automatisch |
| Response-Parsing | Eigenes Pydantic-Modell | `response.content[0].text` | SDK liefert bereits typisierte Pydantic-Objekte |

---

## Common Pitfalls

### Pitfall 1: Temperature-Einschränkung bei Haiku
**What goes wrong:** `anthropic.BadRequestError: 400 temperature: 0.1 is not supported for this model`
**Why it happens:** Haiku (und neuere Claude-Modelle) erlauben nur `temperature=1.0` im Standard-Modus. Extended thinking erlaubt `temperature=0.0-1.0`, aber das wäre Overkill hier.
**How to avoid:** Immer `temperature=1.0` setzen. Determinismus über Prompt-Anweisungen steuern ("Antworte nur mit dem Format X").
**Warning signs:** BadRequestError beim ersten API-Call nach Migration.

### Pitfall 2: Response-Struktur verwechseln
**What goes wrong:** `AttributeError: 'Message' object has no attribute 'choices'`
**Why it happens:** OpenAI-Syntax `response.choices[0].message.content` funktioniert bei Anthropic nicht.
**How to avoid:** Anthropic: `response.content[0].text`
**Warning signs:** AttributeError direkt nach dem API-Call.

### Pitfall 3: negativer Prompt-Bias trotz Anpassung
**What goes wrong:** Scores bleiben negativ geclustert auch mit neuem Prompt.
**Why it happens:** LLMs lernten aus Nachrichtendaten dass "Nachrichten = meistens negativ". Skalen-Definitionen allein reichen nicht — das Modell braucht explizite Kalibrierungsbeispiele die zeigen: "Diese Headline bekommt +0.6".
**How to avoid:** Kalibrierungs-Ankerpunkte direkt als nummerierte Beispiele im Prompt, nicht nur als Beschreibung der Skala. Zudem eine explizite Anweisung: "Wenn du dir nicht sicher bist, tendiere zu einer ausgewogenen Einschätzung, nicht zu negativen Werten."
**Warning signs:** Nach Deployment weiterhin >80% negative Scores im Logging.

### Pitfall 4: docker-compose.yaml halb-migriert
**What goes wrong:** Container startet, aber OPENAI_API_KEY ist gesetzt, ANTHROPIC_API_KEY fehlt → Client-Init schlägt still fehl.
**Why it happens:** Auf dem Server muss die `.env` Datei ebenfalls angepasst werden (OPENAI_API_KEY → ANTHROPIC_API_KEY). Die docker-compose.yaml ist ein Template; der eigentliche Wert kommt aus der `.env` Datei.
**How to avoid:** Beide Dateien synchron anpassen. `.env` auf dem Server muss manuell gesetzt werden (User-Aufgabe laut CONTEXT.md).
**Warning signs:** `ANTHROPIC_API_KEY Umgebungsvariable nicht gesetzt!` im Container-Log.

### Pitfall 5: Health-Check meldet degraded nach Migration
**What goes wrong:** `/api/health` gibt `"status": "degraded"` zurück, obwohl alles funktioniert.
**Why it happens:** Health-Check prüft `openai_client is not None` — nach Migration ist `openai_client` nicht mehr definiert.
**How to avoid:** Health-Check auf `anthropic_client is not None` umstellen, Label von "openai" auf "anthropic" ändern.

---

## Code Examples

### Vollständige neue analyze_sentiment() Funktion (Skelett)

```python
# Source: https://platform.claude.com/docs/en/api/sdks/python
def analyze_sentiment_claude(headlines_batch: list) -> list:
    if not anthropic_client:
        logging.error("Anthropic Client nicht verfügbar.")
        return [0.0] * len(headlines_batch)
    if not headlines_batch:
        return []

    # Prompt-Aufbau (Kalibrierungs-Ankerpunkte sind entscheidend)
    prompt_lines = [
        "Analysiere das Sentiment jeder deutschen Nachrichtenschlagzeile.",
        "Gib für jede Schlagzeile eine Zahl zwischen -1.0 und +1.0 zurück.",
        "",
        "KALIBRIERUNG - Diese Beispiele zeigen die erwartete Skala:",
        "- 'Flugzeugabsturz mit 200 Toten' → -0.9",
        "- 'Wirtschaftskrise verschärft sich' → -0.6",
        "- 'Regierung schließt neue Haushaltslücke' → -0.3",
        "- 'Wetterbericht für die Woche' → 0.0",
        "- 'Forschungsergebnisse veröffentlicht' → 0.0",
        "- 'Wirtschaftswachstum über Erwartungen' → +0.5",
        "- 'Friedensverhandlungen beginnen' → +0.7",
        "- 'Historischer Durchbruch bei Krebstherapie' → +0.9",
        "",
        "WICHTIG:",
        "- Bewerte die TONALITÄT der Schlagzeile, nicht das allgemeine Thema.",
        "- Sachliche Berichte über Probleme sind NICHT automatisch -0.5 oder schlechter.",
        "- Nutze den vollen Bereich: positive Nachrichten sollen positive Werte bekommen.",
        "- Im Zweifel: ausgewogene Einschätzung, kein negativer Bias.",
        "",
        "Format: Nur 'Nummer: Score' pro Zeile (z.B. '1: -0.3'). Keine Erklärungen.",
        "",
        "Schlagzeilen:"
    ]
    for i, headline in enumerate(headlines_batch):
        prompt_lines.append(f"{i+1}: {headline}")
    full_prompt = "\n".join(prompt_lines)

    scores = [0.0] * len(headlines_batch)

    try:
        logging.info(f"Sende {len(headlines_batch)} Headlines an Anthropic API (claude-haiku-4-5-20251001)...")
        response = anthropic_client.messages.create(
            model="claude-haiku-4-5-20251001",
            max_tokens=len(headlines_batch) * 15,
            temperature=1.0,
            messages=[{"role": "user", "content": full_prompt}]
        )
        raw_response_content = response.content[0].text.strip()
        logging.info("Antwort von Anthropic API erhalten.")

        # Parsing-Logik identisch zu bisheriger Implementierung
        lines = raw_response_content.split('\n')
        score_pattern = re.compile(r"^\s*(\d+):\s*([-+]?\d*\.?\d+)\s*$")
        parsed_count = 0
        for line in lines:
            match = score_pattern.match(line)
            if match:
                try:
                    index = int(match.group(1)) - 1
                    score = float(match.group(2))
                    if 0 <= index < len(scores):
                        scores[index] = max(-1.0, min(1.0, score))
                        parsed_count += 1
                except (ValueError, IndexError) as parse_error:
                    logging.warning(f"Parse-Fehler '{line}': {parse_error}")

        logging.info(f"Erfolgreich {parsed_count} Scores geparsed.")
        return scores

    except APIConnectionError as e:
        logging.error(f"Anthropic API nicht erreichbar: {e}")
        return [0.0] * len(headlines_batch)
    except RateLimitError as e:
        logging.error(f"Anthropic Rate-Limit: {e}")
        return [0.0] * len(headlines_batch)
    except APIStatusError as e:
        logging.error(f"Anthropic API Fehler {e.status_code}: {e.response}")
        return [0.0] * len(headlines_batch)
    except Exception as e:
        logging.error(f"Unerwarteter Fehler bei Anthropic Analyse: {e}")
        return [0.0] * len(headlines_batch)
```

---

## Runtime State Inventory

> Kein Rename-/Refactor-Szenario für Infrastruktur-State. Dennoch: Der API-Key wechselt, was Server-seitigen State betrifft.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | Keine — PostgreSQL-Daten sind API-agnostisch | Keine |
| Live service config | docker-compose.yaml auf Server: `OPENAI_API_KEY=${OPENAI_API_KEY}` | Zeile ersetzen durch `ANTHROPIC_API_KEY=${ANTHROPIC_API_KEY}` |
| OS-registered state | Keine Task-Scheduler-Einträge | Keine |
| Secrets/env vars | `.env` Datei auf Server enthält `OPENAI_API_KEY=sk-...` | User muss `ANTHROPIC_API_KEY=sk-ant-...` in `.env` eintragen (CONTEXT.md: User setzt Key manuell) |
| Build artifacts | Docker-Image enthält `openai` Package — wird durch Rebuild ersetzt | `docker-compose build` beim Deployment |

---

## Environment Availability

> Step 2.6: SKIPPED (Phase betrifft nur Python-Paket-Swap + Konfigurationsänderung, keine neuen externen Dienste. Anthropic API ist eine Remote-API ohne lokale Infrastruktur-Anforderung.)

---

## Validation Architecture

> nyquist_validation ist in `.planning/config.json` explizit auf `false` gesetzt. Section entfällt.

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `openai.chat.completions.create()` | `anthropic.messages.create()` | Phase 16 | Andere Response-Struktur, kein temperature < 1.0 |
| `response.choices[0].message.content` | `response.content[0].text` | Phase 16 | Direkter String-Zugriff |
| `OpenAIError` Exception | `APIConnectionError`, `RateLimitError`, `APIStatusError` | Phase 16 | Granularere Fehlertypen |

**Deprecated/outdated nach Phase 16:**
- `openai==1.70.0`: Aus requirements.txt entfernt
- `OPENAI_API_KEY`: Aus docker-compose.yaml und Server-`.env` entfernt
- `analyze_headlines_openai_batch()`: Umbenannt zu `analyze_headlines_batch()`
- `analyze_sentiment_openai()`: Umbenannt zu `analyze_sentiment_claude()`
- `openai_client` Variable: Umbenannt zu `anthropic_client`

---

## Open Questions

1. **claude-haiku-4-5-20251001 Model-ID Verfügbarkeit**
   - What we know: CONTEXT.md nennt diesen Namen explizit als locked decision
   - What's unclear: Ob die exakte Model-ID `claude-haiku-4-5-20251001` die korrekte Anthropic-Schreibweise ist (Anthropic-Docs nutzen z.B. `claude-haiku-4-5` oder ähnliche Kurzformen)
   - Recommendation: Beim ersten Deployment prüfen — bei `404 NotFoundError` auf `claude-haiku-4-5` testen. Die Model-ID ist in `config.h`/`app.py` als Konstante definieren damit sie leicht änderbar ist.

2. **Prompt-Kalibrierung: Ausreichend oder weiteres Tuning nötig?**
   - What we know: Kalibrierungs-Ankerpunkte aus CONTEXT.md sind Pflicht
   - What's unclear: Ob der Prompt nach einem Deploy-Zyklus sofort ausgewogene Scores liefert oder mehrere Iterationen braucht
   - Recommendation: Nach erstem Produktions-Deployment einen manuellen Test mit 5-10 bekannten Schlagzeilen durchführen (einmal positiv, einmal negativ, einmal neutral). Logging der Raw-Scores analysieren.

---

## Sources

### Primary (HIGH confidence)
- `https://platform.claude.com/docs/en/api/sdks/python` — SDK-Syntax, Response-Struktur, Error-Typen, Timeout-Konfiguration, Temperature-Verhalten
- `https://platform.claude.com/docs/en/api/client-sdks` — Installations-Command, Quick-Start-Beispiel
- PyPI `pip3 index versions anthropic` — Version 0.86.0 verifiziert (2026-03-26)

### Secondary (MEDIUM confidence)
- Codebase-Analyse: `sentiment-api/app.py`, `background_worker.py`, `docker-compose.yaml`, `requirements.txt` — bestehende Integrationspunkte vollständig kartiert

### Tertiary (LOW confidence)
- Keine.

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — Anthropic SDK 0.86.0 auf PyPI verifiziert, offiziell dokumentiert
- Architecture: HIGH — Bestehendes Code vollständig gelesen, Integrationspunkte eindeutig
- Pitfalls: HIGH — SDK-Dokumentation bestätigt temperature-Einschränkung, Response-Struktur-Unterschied direkt aus Docs

**Research date:** 2026-03-26
**Valid until:** 2026-04-25 (30 Tage — SDK ist stabil, geringe Änderungsrate)
