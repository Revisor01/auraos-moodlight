# Phase 2: Backend-Hardening - Research

**Researched:** 2026-03-25
**Domain:** Flask/Gunicorn WSGI migration, Python shared-config refactoring, Git hygiene
**Confidence:** HIGH — alle Befunde basieren auf direkter Codeanalyse der tatsächlichen Dateien

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

Alle Implementation-Entscheidungen:

- **BE-01 / Gunicorn**: MUSS `-w 1` verwenden — Background Worker (`SentimentUpdateWorker`) wird in `app.py` vor `app.run()` gestartet und würde bei `-w 2` doppelt laufen (doppelte OpenAI-Kosten, DB Race Conditions)
- **shared_config.py**: Neue Datei für RSS-Feeds und `get_sentiment_category()` — minimale Invasivität
- **DATA-02 Thresholds**: Die `background_worker.py`/`moodlight_extensions.py` Thresholds (0.30/0.10/-0.20/-0.50) verwenden, NICHT die abweichenden `app.py` Thresholds (0.85/0.2)
- **BE-02 feedparser Timeout**: `requests.get(url, timeout=15)` + `feedparser.parse(response.content)` statt globalem Socket-Timeout, da feedparser keinen nativen Timeout-Parameter hat

### Claude's Discretion

Alle Implementation-Details innerhalb der genannten Constraints sind an Claude's Discretion — reine Infrastruktur-Phase mit technisch spezifizierten Fixes aus der Research.

### Deferred Ideas (OUT OF SCOPE)

Keine — die Diskussion blieb im Phase-Scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| BE-01 | Flask läuft hinter Gunicorn statt Dev-Server — `gunicorn -w 1 -b 0.0.0.0:6237 app:app` in Dockerfile, Background Worker startet nur einmal | Dockerfile CMD Zeile 18, app.py Zeile 574 identifiziert; Gunicorn pre-fork Mechanismus verstanden |
| BE-02 | Socket-Timeouts sind per-Connection statt global — kein `socket.setdefaulttimeout()`, stattdessen `requests.get(url, timeout=15)` | socket.setdefaulttimeout() in app.py Zeilen 368-377 und background_worker.py Zeilen 157-166 identifiziert; requests bereits in requirements.txt vorhanden |
| DATA-01 | RSS-Feed-Liste existiert nur einmal — `background_worker.py` importiert Feeds aus shared Config | Duplikat in app.py Zeilen 55-68 und background_worker.py Zeilen 137-150 verifiziert — identischer Inhalt |
| DATA-02 | Sentiment-Kategorie-Thresholds sind konsistent — eine einzige `get_sentiment_category()` Funktion | Drei Definitionen kartiert: app.py Zeilen 205-209 (0.85/0.2), background_worker.py Zeilen 216-227 (0.30/0.10), moodlight_extensions.py Zeilen 24-35 (0.30/0.10) |
| DATA-03 | Tote API-Endpoints sind entfernt — `/api/dashboard` und `/api/logs` existieren nicht mehr | app.py Zeilen 346-354: beide Endpoints geben `jsonify({...})` zurück — Python SyntaxError bei Aufruf |
| REPO-01 | `.env` ist in `.gitignore` — kein Risiko dass API-Keys oder DB-Passwörter versehentlich committed werden | .gitignore analysiert: kein `.env` Eintrag vorhanden; `sentiment-api/.env.example` existiert, impliziert `.env` wird erwartet |
| REPO-02 | Temporäre und binäre Dateien sind aus Git entfernt — `setup.html.tmp.html` gelöscht, `releases/`, `*.tmp.html` in `.gitignore` | firmware/data/setup.html.tmp.html existiert; releases/v9.0/ enthält .bin und .tgz Dateien im Repo |
</phase_requirements>

---

## Summary

Diese Phase umfasst sieben isolierte, technisch klar spezifizierte Fixes am Flask-Backend und Repository. Kein Firmware-Code wird berührt. Alle Probleme wurden durch direkte Codeanalyse verifiziert — keine spekulativen Befunde.

Das kritischste Fix ist BE-01 (Gunicorn). Der `SentimentUpdateWorker` wird in `app.py` außerhalb des `if __name__ == '__main__'` Blocks gestartet — nämlich direkt im Modul-Body via `start_background_worker()` an Zeile 568 (innerhalb des `if __name__` Blocks). Das bedeutet: Bei Gunicorn pre-fork wird der Worker nur im ursprünglichen Prozess gestartet, NICHT in jedem Worker. Das ist jedoch nur dann sicher, wenn `-w 1` verwendet wird — mit mehreren Workern würde der `post_fork` Hook den Thread nicht neu starten, aber die Singleton-Prüfung (`_worker is not None`) würde im geforkten Prozess greifen, da Python's fork den gesamten Speicher dupliziert. **Sicherste Wahl: `-w 1 --threads 4`**, wie in CONTEXT.md entschieden.

Die DATA-02 Threshold-Konsolidierung ist inhaltlich wichtig: `app.py` verwendet Schwellenwerte von 0.85/0.2, die anderen zwei Dateien 0.30/0.10/-0.20/-0.50. Ein Score von 0.50 wird in `app.py` als "positiv" kategorisiert, in den anderen als "sehr positiv". Die canonical Thresholds sind die aus `background_worker.py` und `moodlight_extensions.py` — diese bestimmen was in der DB gespeichert wird und was der ESP32 empfängt.

**Primary recommendation:** `shared_config.py` zuerst erstellen (DATA-01 + DATA-02), dann Gunicorn-Migration (BE-01 + BE-02), dann Cleanup (DATA-03 + REPO-01 + REPO-02).

---

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| gunicorn | 25.2.0 | Production WSGI Server | Ersetzt Flask Dev-Server; `-w 1 --threads 4` ist sicher mit In-Process Background Worker |
| requests | 2.32.2 | HTTP Client mit per-Connection Timeout | Bereits in requirements.txt; löst globalen socket.setdefaulttimeout() |
| feedparser | 6.0.11 | RSS XML Parsing | Bereits in use; bleibt als reiner Parser — HTTP-Layer wird an requests übergeben |

### Neue Datei

| Datei | Zweck |
|-------|-------|
| `sentiment-api/shared_config.py` | Einzige neue Datei dieser Phase — enthält `RSS_FEEDS` dict und `get_sentiment_category()` Funktion |

### Installation

```bash
# In sentiment-api/requirements.txt hinzufügen:
gunicorn==25.2.0
```

`requests` ist bereits vorhanden (`requests==2.32.2`) — kein Upgrade nötig.

---

## Architecture Patterns

### Empfohlene Dateistruktur nach Phase 2

```
sentiment-api/
├── app.py                    # socket.setdefaulttimeout entfernt, tote Endpoints entfernt,
│                             # RSS_FEEDS importiert von shared_config
├── shared_config.py          # NEU: RSS_FEEDS dict + get_sentiment_category() Funktion
├── moodlight_extensions.py   # get_category_from_score() durch Import aus shared_config ersetzt
├── background_worker.py      # lokale rss_feeds + _get_category() durch Imports ersetzt
├── requirements.txt          # gunicorn==25.2.0 hinzugefügt
└── Dockerfile                # CMD auf gunicorn umgestellt
```

### Pattern 1: shared_config.py Inhalt

**Was:** Neue Datei als Single Source of Truth für RSS-Feeds und Kategorie-Thresholds.

```python
# sentiment-api/shared_config.py
# Einzige Quelle für RSS-Feed-Liste und Sentiment-Kategorisierung

RSS_FEEDS = {
    "Zeit": "https://newsfeed.zeit.de/index",
    "Tagesschau": "https://www.tagesschau.de/xml/rss2",
    "Sueddeutsche": "https://rss.sueddeutsche.de/rss/Alles",
    "FAZ": "https://www.faz.net/rss/aktuell/",
    "Die Welt": "https://www.welt.de/feeds/latest.rss",
    "Handelsblatt": "https://www.handelsblatt.com/contentexport/feed/schlagzeilen",
    "n-tv": "https://www.n-tv.de/rss",
    "Focus": "https://rss.focus.de/fol/XML/rss_folnews.xml",
    "Stern": "https://www.stern.de/feed/standard/alle-nachrichten/",
    "Telekom": "https://www.t-online.de/feed.rss",
    "TAZ": "https://taz.de/!p4608;rss/",
    "Deutschlandfunk": "https://www.deutschlandfunk.de/nachrichten-100.rss"
}

def get_sentiment_category(score: float) -> str:
    """Bestimmt Sentiment-Kategorie aus Score. Einzige Definition im gesamten Projekt."""
    if score >= 0.30:
        return "sehr positiv"
    elif score >= 0.10:
        return "positiv"
    elif score >= -0.20:
        return "neutral"
    elif score >= -0.50:
        return "negativ"
    else:
        return "sehr negativ"
```

### Pattern 2: Gunicorn CMD in Dockerfile

**Was:** Ersetzt `CMD ["python", "app.py"]` mit Gunicorn.

```dockerfile
CMD ["gunicorn", "-w", "1", "--threads", "4", "-b", "0.0.0.0:6237", \
     "--timeout", "120", "--access-logfile", "-", "app:app"]
```

- `-w 1` — Ein Worker-Prozess, Background Worker läuft genau einmal
- `--threads 4` — Concurrency ohne Prozess-Duplikation
- `--timeout 120` — Abdeckt langsame OpenAI API Calls (Standard: 30s reicht nicht)
- `--access-logfile -` — Access Logs in stdout für `docker logs`

**WICHTIG:** Der `start_background_worker()` Aufruf in `app.py` Zeilen 568-572 liegt innerhalb des `if __name__ == '__main__':` Blocks. Das bedeutet: Gunicorn ruft diesen Block NICHT auf (Gunicorn importiert das Modul, führt `__main__` nicht aus). Der Background Worker startet also gar nicht, wenn die `start_background_worker()` Zeile im `__main__` Block bleibt.

**Lösung:** `start_background_worker()` muss VOR den `if __name__ == '__main__':` Block verschoben werden — auf Modul-Level, direkt nach den Imports von `moodlight_extensions` und `background_worker`.

```python
# NACH Zeile 555 (register_moodlight_endpoints(app))
# VOR if __name__ == '__main__':

# Background Worker starten (alle 30 Minuten)
# Muss außerhalb von __main__ sein damit Gunicorn ihn startet
start_background_worker(
    app=app,
    analyze_function=analyze_headlines_openai_batch,
    interval_seconds=1800
)

if __name__ == '__main__':
    # app.run() bleibt für lokale Entwicklung
    ...
```

### Pattern 3: Per-Connection Timeout für feedparser

**Was:** Ersetzt `socket.setdefaulttimeout()` durch expliziten `requests`-Fetch.

```python
# Statt:
import socket
socket.setdefaulttimeout(10.0)
feed = feedparser.parse(url, ...)

# Nach dem Fix:
import requests
import feedparser

try:
    response = requests.get(url, timeout=15, headers={'User-Agent': 'WorldMoodAnalyzer/1.0'})
    response.raise_for_status()
    feed = feedparser.parse(response.content)
except requests.exceptions.Timeout:
    logger.warning(f"Timeout bei {source}")
    continue
except requests.exceptions.RequestException as e:
    logger.warning(f"Fehler beim Abrufen von {source}: {e}")
    continue
```

Dieser Ersatz muss in **beiden** Stellen vorgenommen werden:
- `app.py` in der `get_news()` Funktion (Zeilen 364-432)
- `background_worker.py` in der `_fetch_headlines()` Methode (Zeilen 129-214)

### Pattern 4: Tote Endpoints entfernen (DATA-03)

**Was:** `/api/dashboard` (Zeile 346) und `/api/logs` (Zeile 351) komplett entfernen — beide Routen-Dekoratoren und die Funktionsdefinitionen.

```python
# Diese Blöcke aus app.py entfernen (Zeilen 346-354):

@app.route('/api/dashboard')
def get_dashboard_data():
  # Hier Daten für das Dashboard generieren
  return jsonify({...})

@app.route('/api/logs')
def get_logs():
  # Hier Logs bereitstellen
  return jsonify({...})
```

Nach dem Entfernen: `GET /api/dashboard` und `GET /api/logs` geben Flask's automatische 404-Response zurück.

### Pattern 5: .gitignore Ergänzungen (REPO-01 + REPO-02)

```gitignore
# Secrets
.env
sentiment-api/.env

# Releases (Binaries via GitHub Releases, nicht im Repo)
releases/

# Temporäre HTML-Dateien
*.tmp.html
```

### Anti-Patterns to Avoid

- **Gunicorn `-w 2` oder mehr verwenden**: Der Background Worker würde mit dem Pre-Fork-Modell in jedem Prozess starten. Die `_worker is not None` Singleton-Prüfung greift nicht über Prozessgrenzen.
- **`socket.setdefaulttimeout()` nach requests-Migration behalten**: Ist thread-unsafe in Multi-Thread-Umgebung (Gunicorn + Background Thread). Muss vollständig entfernt werden.
- **app.py Thresholds (0.85/0.2) als canonical wählen**: Die DB enthält Kategorien die mit background_worker Thresholds (0.30/0.10) gespeichert wurden. Die ESP32-Response kommt aus moodlight_extensions — auch 0.30/0.10. Die `app.py` Thresholds sind die falsch abweichenden.
- **`feedparser.parse(url)` ohne Timeout behalten in `app.py`**: Dieser Pfad (in `get_news()`) nutzt weiterhin feedparser direkt mit globalem Socket-Timeout. Muss auch auf requests umgestellt werden.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| WSGI Production Server | Eigenen TCP-Server | Gunicorn 25.2.0 | Signal Handling, Graceful Shutdown, Worker Management ist 10+ Jahre Arbeit |
| Per-Connection HTTP Timeout | Eigenes Socket-Wrapping | `requests.get(url, timeout=15)` | requests handhabt Redirects, SSL, Connection-Pooling korrekt |
| Module-übergreifende Config | Erneutes Kopieren | `from shared_config import RSS_FEEDS, get_sentiment_category` | Python's Import-System ist die richtige Lösung |

---

## Exakte Code-Lokalisierungen (verifiziert durch direkte Lektüre)

### app.py — was muss wo geändert werden

| Zeile(n) | Was | Aktion |
|----------|-----|--------|
| 55-68 | `rss_feeds = {...}` Dict | Durch `from shared_config import RSS_FEEDS as rss_feeds` ersetzen |
| 205-209 | `if score > 0.85: strength = "sehr_positiv"...` | Durch `strength = get_sentiment_category(score)` ersetzen (Import aus shared_config) |
| 346-354 | `/api/dashboard` und `/api/logs` Routen | Komplett löschen |
| 364-432 | `get_news()` — socket.setdefaulttimeout Block | Durch requests.get(url, timeout=15) + feedparser.parse(response.content) ersetzen |
| 368-377 | `socket.setdefaulttimeout(socket_timeout)` | Entfernen — wird durch requests-Timeout ersetzt |
| 427-432 | `socket.setdefaulttimeout(original_timeout)` Rücksetzblock | Entfernen |
| 551-552 | Import-Block | `from shared_config import get_sentiment_category` hinzufügen |
| 559-572 | `if __name__ == '__main__':` Block mit start_background_worker | `start_background_worker()` Aufruf VOR den `__main__` Block verschieben |
| 574 | `app.run(...)` | Bleibt für lokale Entwicklung — Gunicorn übernimmt in Production |

### background_worker.py — was muss wo geändert werden

| Zeile(n) | Was | Aktion |
|----------|-----|--------|
| 134-135 | `import feedparser`, `import socket` (lokal in `_fetch_headlines`) | `import requests` statt `import socket` hinzufügen |
| 137-150 | Lokales `rss_feeds = {...}` Dict | Durch `from shared_config import RSS_FEEDS as rss_feeds` am Modul-Anfang ersetzen |
| 157-166 | `socket.setdefaulttimeout(socket_timeout)` Block | Entfernen — wird durch requests.get() Timeout ersetzt |
| 169-205 | `feedparser.parse(url, ...)` in Feed-Loop | Auf `requests.get(url, timeout=15)` + `feedparser.parse(response.content)` umstellen |
| 207-212 | Timeout-Rücksetzblock | Entfernen |
| 216-227 | `_get_category()` Methode | Durch `from shared_config import get_sentiment_category` am Modul-Anfang + Delegation ersetzen |

### moodlight_extensions.py — was muss wo geändert werden

| Zeile(n) | Was | Aktion |
|----------|-----|--------|
| 24-35 | `get_category_from_score()` Funktion | Durch Import ersetzen: `from shared_config import get_sentiment_category as get_category_from_score` |

### requirements.txt

```
gunicorn==25.2.0
```
— eine Zeile hinzufügen.

### Dockerfile

Zeile 18: `CMD ["python", "app.py"]` → `CMD ["gunicorn", "-w", "1", "--threads", "4", "-b", "0.0.0.0:6237", "--timeout", "120", "--access-logfile", "-", "app:app"]`

### .gitignore

Drei Blöcke hinzufügen (`.env`, `releases/`, `*.tmp.html`).

### firmware/data/setup.html.tmp.html

Datei löschen. Auch in `.claude/worktrees/*/firmware/data/setup.html.tmp.html` vorhanden — worktree-Kopien müssen nicht explizit gelöscht werden (worktrees sind temporär).

---

## Common Pitfalls

### Pitfall 1: Background Worker startet nicht unter Gunicorn

**What goes wrong:** `start_background_worker()` liegt im `if __name__ == '__main__':` Block. Gunicorn importiert `app.py` als Modul — `__main__` wird nicht ausgeführt. Worker startet nie, keine Sentiment-Updates, `/api/moodlight/current` gibt Datenbank-Fehler zurück.

**Why it happens:** Klassischer Fehler bei Flask-zu-Gunicorn Migration. Flask Dev-Server führt `__main__` aus, Gunicorn nicht.

**How to avoid:** `start_background_worker()` auf Modul-Level verschieben — direkt nach `register_moodlight_endpoints(app)`, VOR `if __name__ == '__main__':`.

**Warning signs:** Docker-Logs zeigen keine "Background Worker gestartet" Meldung nach Container-Start. `/api/moodlight/current` gibt 503 zurück (keine Daten in DB).

### Pitfall 2: feedparser bozo-Exception-Handling nach requests-Migration

**What goes wrong:** Das bestehende `feed.bozo and isinstance(feed.bozo_exception, socket.timeout)` Check funktioniert nicht mehr, wenn der HTTP-Fehler bereits von requests abgefangen wurde. `feedparser.parse(response.content)` bekommt keinen fehlerhaften Feed — also nie bozo=True für HTTP-Fehler.

**Why it happens:** Die Logik geht davon aus dass feedparser die HTTP-Verbindung selbst aufbaut.

**How to avoid:** Nach der requests-Migration:
- HTTP-Fehler werden von `requests.exceptions.Timeout` / `requests.exceptions.RequestException` abgefangen (vor feedparser)
- `feed.bozo` Check bleibt für XML-Parse-Fehler (valide Feed-Inhalte aber kaputtes XML)
- `isinstance(feed.bozo_exception, socket.timeout)` muss entfernt werden (socket.timeout kommt nie mehr von feedparser)

### Pitfall 3: Doppelter Background-Worker bei Gunicorn `-w 2`

**What goes wrong:** Mit `-w 2` werden 2 Prozesse geforkt. Wenn `start_background_worker()` auf Modul-Level liegt, startet Python den Worker zweimal — einmal im ursprünglichen Prozess (der dann geforkt wird) und der Fork kopiert den laufenden Thread-State. Ergebnis: 2 Worker, doppelte OpenAI-Kosten.

**Why it happens:** Python's `fork()` kopiert den gesamten Prozess-Zustand inkl. laufender Threads. Die `_worker is not None` Singleton-Prüfung greift nicht über Prozessgrenzen.

**How to avoid:** `-w 1` wie in CONTEXT.md entschieden. Nicht `-w 2` "für Performance" verwenden — Redis-Cache macht den zweiten Worker sinnlos.

### Pitfall 4: `.env` bleibt nach .gitignore-Eintrag noch getrackt

**What goes wrong:** `.gitignore` verhindert nur das Tracking neuer Dateien. Falls `.env` bereits committed wurde, bleibt sie getrackt trotz .gitignore-Eintrag.

**Why it happens:** Git unterscheidet zwischen "nicht getrackt" und "aus Tracking entfernen".

**How to avoid:** Nach .gitignore-Eintrag: `git status` prüfen. Falls `.env` getrackt ist: `git rm --cached .env && git rm --cached sentiment-api/.env`. Aktueller Stand (aus Glob-Analyse): `.env` erscheint nicht im Repo — nur `.env.example` ist vorhanden. Daher ist `git rm --cached` wahrscheinlich nicht nötig, aber nach .gitignore-Eintrag verifizieren.

### Pitfall 5: `releases/` directory hat bereits getrackte Binaries

**What goes wrong:** `releases/v9.0/Firmware-9.0-AuraOS.bin` und `UI-9.0-AuraOS.tgz` sind bereits im Repo committed (Glob-Analyse bestätigt). .gitignore-Eintrag allein entfernt sie nicht aus der History.

**Why it happens:** Siehe Pitfall 4.

**How to avoid:** Für REPO-02 sind zwei Schritte nötig:
1. `releases/` zu `.gitignore` hinzufügen
2. `git rm -r --cached releases/` um tracked files zu entfernen (entfernt nur aus Index, nicht aus Filesystem)

Die Binaries bleiben in der Git-History — History-Bereinigung (BFG/filter-branch) ist für diesen Scope nicht erforderlich und bleibt deferred.

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Flask Dev-Server (`python app.py`) | Gunicorn mit `-w 1 --threads 4` | Phase 2 | Produktionstauglicher WSGI-Layer, graceful shutdown |
| `socket.setdefaulttimeout()` global | `requests.get(url, timeout=15)` pro Call | Phase 2 | Thread-sichere Timeouts, kein Einfluss auf PostgreSQL/Redis-Verbindungen |
| 3x duplizierte Threshold-Definitionen | 1x `get_sentiment_category()` in shared_config.py | Phase 2 | Score 0.35 ergibt überall "positiv" |
| 2x duplizierte RSS-Feed-Liste | 1x `RSS_FEEDS` in shared_config.py | Phase 2 | Kein Drift mehr zwischen app.py und background_worker.py |
| `/api/dashboard` und `/api/logs` geben 500 | Endpoints entfernt, 404 | Phase 2 | Kein unerklärlicher Server-Error bei Aufruf |

---

## Open Questions

1. **`start_background_worker()` Verschiebung — doppelter Start bei `python app.py`?**
   - Was wir wissen: `start_background_worker()` hat eine `_worker is not None` Prüfung
   - Was unklar: Wenn die Funktion auf Modul-Level liegt und dann auch im `__main__` Block aufgerufen wird, könnte es zu doppeltem Start kommen
   - Recommendation: `start_background_worker()` aus dem `__main__` Block ENTFERNEN wenn er auf Modul-Level verschoben wird — kein doppelter Aufruf

2. **`docker-compose.yaml` volume `./:/app` — gefährlich mit Gunicorn?**
   - Was wir wissen: Die aktuelle `docker-compose.yaml` mounted das gesamte Verzeichnis als Volume
   - Was unklar: Gunicorn's `--reload` würde bei Dateiänderungen automatisch neustarten — aber `--reload` ist hier nicht konfiguriert
   - Recommendation: Kein Problem, Volume-Mount bleibt unverändert (keine `--reload` Flag)

3. **`get_category_from_score` vs. `get_sentiment_category` — Funktionsname in moodlight_extensions.py?**
   - Was wir wissen: `moodlight_extensions.py` verwendet intern `get_category_from_score()` — aber diese Funktion wird nur lokal in der Datei aufgerufen
   - Recommendation: Import-Alias nutzen: `from shared_config import get_sentiment_category as get_category_from_score` — keine weiteren Codeänderungen nötig

---

## Environment Availability

Step 2.6: Diese Phase ist rein code/config-basiert. Keine externen Dependencies außer Docker/Gunicorn, die im Container selbst installiert werden. Lokale Umgebungsinstallation nicht erforderlich.

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Docker | Container-Build | ✓ | (Server) | — |
| gunicorn | BE-01 | ✗ lokal, ✓ via requirements.txt | 25.2.0 | — |
| requests | BE-02 | ✓ (requirements.txt 2.32.2) | 2.32.2 | — |

**Keine blockierenden Dependencies** — alle Fixes sind Code-Änderungen, gunicorn wird in requirements.txt hinzugefügt und beim Docker-Build installiert.

---

## Sources

### Primary (HIGH confidence)

- Direkte Codeanalyse: `sentiment-api/app.py` — alle Zeilen gelesen und verifiziert
- Direkte Codeanalyse: `sentiment-api/background_worker.py` — alle Zeilen gelesen und verifiziert
- Direkte Codeanalyse: `sentiment-api/moodlight_extensions.py` — alle Zeilen gelesen und verifiziert
- Direkte Codeanalyse: `sentiment-api/Dockerfile` — Zeile 18 verifiziert
- Direkte Codeanalyse: `.gitignore` — komplette Datei gelesen, fehlende Einträge bestätigt
- Direkte Codeanalyse: `releases/v9.0/` — Glob-Ergebnis bestätigt tracked Binaries
- Direkte Codeanalyse: `firmware/data/setup.html.tmp.html` — Glob-Ergebnis bestätigt Existenz
- `.planning/research/STACK.md` — Gunicorn-Patterns, feedparser-Timeout-Strategie
- `.planning/research/PITFALLS.md` — Gunicorn pre-fork / Background Worker Pitfall
- `.planning/research/ARCHITECTURE.md` — Build-Reihenfolge und Abhängigkeitsgraph
- `.planning/codebase/CONCERNS.md` — vollständige Problem-Kartierung mit Zeilennummern

### Secondary (MEDIUM confidence)

- `.planning/phases/02-backend-hardening/02-CONTEXT.md` — Locked decisions, Integration Points

---

## Metadata

**Confidence breakdown:**
- Exakte Zeilen-Lokalisierungen: HIGH — direkt aus Code gelesen
- Gunicorn `__main__` vs. Modul-Level Problem: HIGH — Python Import-Mechanismus ist etabliert
- feedparser bozo-Handling nach Migration: HIGH — direkte Logik-Analyse
- releases/ tracking-Status: HIGH — Glob-Analyse bestätigt .bin und .tgz im Repo

**Research date:** 2026-03-25
**Valid until:** 2026-04-24 (stabile Dependencies, keine Breaking Changes erwartet)
