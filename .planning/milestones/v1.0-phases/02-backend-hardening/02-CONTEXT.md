# Phase 2: Backend-Hardening - Context

**Gathered:** 2026-03-25
**Status:** Ready for planning
**Mode:** Auto-generated (infrastructure phase — all fixes technically specified in research)

<domain>
## Phase Boundary

Backend läuft produktionstauglich hinter Gunicorn, ohne duplizierte Konfiguration oder Secret-Leaks im Repository. Sieben konkrete Fixes:

1. **BE-01**: Flask Dev-Server → Gunicorn mit `-w 1` (Background Worker darf nicht dupliziert werden)
2. **BE-02**: Globale `socket.setdefaulttimeout()` → per-Connection Timeouts via `requests.get(url, timeout=15)`
3. **DATA-01**: Duplizierte RSS-Feed-Liste → shared_config.py, background_worker importiert von dort
4. **DATA-02**: Inkonsistente Sentiment-Kategorie-Thresholds → eine `get_sentiment_category()` Funktion
5. **DATA-03**: Tote Endpoints `/api/dashboard` und `/api/logs` entfernen
6. **REPO-01**: `.env` in `.gitignore` eintragen
7. **REPO-02**: `setup.html.tmp.html` löschen, `releases/`, `*.tmp.html` in `.gitignore`

</domain>

<decisions>
## Implementation Decisions

### Claude's Discretion
All implementation choices are at Claude's discretion — pure infrastructure phase with technically specified fixes from research. Key constraints:

- **Gunicorn**: MUSS `-w 1` verwenden — Background Worker (`SentimentUpdateWorker`) wird in `app.py` vor `app.run()` gestartet und würde bei `-w 2` doppelt laufen (doppelte OpenAI-Kosten, DB Race Conditions)
- **shared_config.py**: Neue Datei für RSS-Feeds und `get_sentiment_category()` — minimale Invasivität
- **Thresholds**: Die `background_worker.py`/`moodlight_extensions.py` Thresholds (0.30/0.10/-0.20/-0.50) verwenden, nicht die abweichenden `app.py` Thresholds (0.85/0.2)
- **feedparser Timeout**: `requests.get(url, timeout=15)` + `feedparser.parse(response.content)` statt globalem Socket-Timeout, da feedparser keinen nativen Timeout-Parameter hat

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `sentiment-api/database.py` — Database/RedisCache Singletons, geeignet als Host für shared Functions
- `sentiment-api/moodlight_extensions.py` — bereits registriert via `register_moodlight_endpoints(app)`
- `sentiment-api/requirements.txt` — muss um `gunicorn` erweitert werden
- `sentiment-api/Dockerfile` — CMD muss von `python app.py` auf `gunicorn` geändert werden

### Established Patterns
- Flask Blueprint-artiges Pattern via `register_moodlight_endpoints(app)`
- Singleton-Getter: `get_database()`, `get_cache()`
- Background Worker als Daemon-Thread in `app.py`

### Integration Points
- `sentiment-api/app.py` Zeile 574: `app.run()` → Gunicorn CMD
- `sentiment-api/app.py` Zeilen 55-68: `rss_feeds` Dict
- `sentiment-api/app.py` Zeilen 205-209: Kategorie-Thresholds (abweichend!)
- `sentiment-api/background_worker.py` Zeilen 137-150: duplizierte `rss_feeds`
- `sentiment-api/background_worker.py` Zeilen 216-227: Kategorie-Thresholds
- `sentiment-api/moodlight_extensions.py` Zeilen 24-35: Kategorie-Thresholds
- `sentiment-api/app.py` Zeilen 368-377: `socket.setdefaulttimeout()` in Request-Handler
- `sentiment-api/background_worker.py` Zeilen 157-166: `socket.setdefaulttimeout()` in Worker
- `sentiment-api/app.py` Zeile 349: `/api/dashboard` Stub
- `sentiment-api/app.py` Zeile 354: `/api/logs` Stub

</code_context>

<specifics>
## Specific Ideas

No specific requirements — infrastructure phase. Refer to ROADMAP phase description, success criteria, and .planning/research/ documents.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>
