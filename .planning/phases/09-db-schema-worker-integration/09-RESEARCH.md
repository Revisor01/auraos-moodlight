# Phase 9: DB-Schema & Worker-Integration - Research

**Researched:** 2026-03-26
**Domain:** PostgreSQL Schema-Erweiterung, Python Background Worker, RSS-Feed-Persistenz
**Confidence:** HIGH

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
Alle Implementierungsentscheidungen liegen im Ermessen von Claude — reine Infrastruktur-Phase. Vorgaben durch ROADMAP Phase-Goal und Success Criteria.

Key constraints:
- Existierendes init.sql-Pattern für DB-Schema nutzen
- Background Worker hat bereits DB-Zugriff via Database-Klasse
- shared_config.py existiert als aktuelle Zwischenlösung für Feed-Listen
- Feed-Liste ist in app.py und background_worker.py dupliziert

### Claude's Discretion
Alle Implementierungsdetails (Feldnamen, SQL-Patterns, Python-Struktur, Migration-Strategie) liegen bei Claude.

### Deferred Ideas (OUT OF SCOPE)
Keine deferrierten Ideen — reine Infrastruktur-Phase.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| FEED-01 | Feed-Liste in PostgreSQL persistieren statt hardcoded in Python-Dateien | DB-Schema feeds-Tabelle + get_active_feeds()-Methode in Database-Klasse + Worker-Integration |
| FEED-06 | Focus.de Feed (404) aus Default-Liste entfernen | Focus-Eintrag nicht in INSERT-Daten der neuen feeds-Tabelle aufnehmen |
</phase_requirements>

---

## Summary

Phase 9 erweitert das bestehende PostgreSQL-Schema um eine `feeds`-Tabelle und stellt den Background Worker von der hardcodierten `RSS_FEEDS`-Dict auf DB-seitige Feed-Verwaltung um. Die Codebasis ist dafür gut vorbereitet: `database.py` stellt eine robuste `Database`-Klasse mit `get_cursor()` Context Manager und `conn.commit()` Pattern bereit, `background_worker.py` importiert `get_database()` bereits, und `init.sql` wird beim Container-Start automatisch ausgeführt.

Die zentrale Herausforderung ist das Schema-Migration-Problem: Die PostgreSQL-Instanz auf dem Server enthält bereits Produktionsdaten (sentiment_history, device_statistics). `init.sql` wird von PostgreSQL nur beim ersten Start (wenn das Volume leer ist) ausgeführt — der `CREATE TABLE IF NOT EXISTS` Guard verhindert Fehler, aber neue Tabellen werden auf dem laufenden System nicht automatisch angelegt. Für das Live-System wird daher ein separates Migrations-SQL notwendig.

Der Background Worker liest in `_fetch_headlines()` die Feed-Liste via `rss_feeds = RSS_FEEDS` aus shared_config.py. Nach der Migration liest er stattdessen per `db.get_active_feeds()` aus der DB — beim Start und zu Beginn jedes Analyse-Zyklus. `shared_config.py` kann nach erfolgreicher Migration entfernt werden (nur `get_sentiment_category` muss dabei in `shared_config.py` verbleiben oder in eine andere Datei wandern, da app.py und background_worker.py beide diese Funktion nutzen).

**Primary recommendation:** `feeds`-Tabelle in `init.sql` ergänzen (für neue Deployments), Migrations-SQL separat für das laufende System erstellen, `get_active_feeds()` in Database-Klasse hinzufügen, Background Worker auf DB-Lesepfad umstellen, `shared_config.py` Feed-Dict entfernen.

---

## Standard Stack

### Core (bereits vorhanden — kein neues Paket nötig)

| Komponente | Version | Zweck | Status |
|-----------|---------|-------|--------|
| PostgreSQL | 16-alpine | Persistenz der feeds-Tabelle | Produktiv, läuft |
| psycopg2-binary | 2.9.9 | DB-Zugriff aus Python | Bereits installiert |
| Database-Klasse | — | Wrapper mit get_cursor(), commit() | Bereit in database.py |
| init.sql | — | Schema-Definition für neue Container | Erweitern, nicht ersetzen |

**Installation:** Keine neuen Pakete erforderlich.

---

## Architecture Patterns

### Empfohlene feeds-Tabellen-Struktur

```sql
CREATE TABLE IF NOT EXISTS feeds (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    url TEXT NOT NULL UNIQUE,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    last_fetched_at TIMESTAMPTZ,
    error_count INTEGER NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

**Warum diese Felder:**
- `name` — Anzeigename (z.B. "Tagesschau"), wird in späteren Phasen (Phase 10: API, Phase 11: UI) genutzt
- `url` — UNIQUE constraint verhindert doppelte Einträge
- `active` — Phase 10 braucht Toggle-Funktion; Worker filtert `WHERE active = TRUE`
- `last_fetched_at` — Phase 11 zeigt Feed-Status (UI-03); Worker aktualisiert nach jedem Fetch
- `error_count` — Phase 11 zeigt Fehler-Count (UI-03); Worker erhöht bei Fehlern
- `created_at` / `updated_at` — Konsistent mit bestehenden Tabellen (device_statistics hat updated_at)

### Pattern 1: Schema-Erweiterung in init.sql (für neue Deployments)

`init.sql` wird nur beim ersten Container-Start ausgeführt (wenn das PostgreSQL-Volume leer ist). `CREATE TABLE IF NOT EXISTS` ist idempotent — kein Risiko beim erneuten Ausführen.

```sql
-- ===== FEEDS TABLE =====
CREATE TABLE IF NOT EXISTS feeds (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    url TEXT NOT NULL UNIQUE,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    last_fetched_at TIMESTAMPTZ,
    error_count INTEGER NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_feeds_active ON feeds(active);
CREATE INDEX IF NOT EXISTS idx_feeds_url ON feeds(url);

COMMENT ON TABLE feeds IS 'RSS-Feeds für Sentiment-Analyse';
```

### Pattern 2: Migrations-SQL für das laufende Produktionssystem

Da das laufende System PostgreSQL-Daten im Volume hat, wird `init.sql` nicht erneut ausgeführt. Ein separates Migrations-Skript ist notwendig:

```sql
-- sentiment-api/migrations/001_add_feeds_table.sql
-- Manuell auszuführen auf dem Produktionssystem
-- psql -U moodlight -d moodlight -f migrations/001_add_feeds_table.sql

CREATE TABLE IF NOT EXISTS feeds (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    url TEXT NOT NULL UNIQUE,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    last_fetched_at TIMESTAMPTZ,
    error_count INTEGER NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_feeds_active ON feeds(active);
CREATE INDEX IF NOT EXISTS idx_feeds_url ON feeds(url);

-- Default-Feeds einfügen (ohne Focus.de)
INSERT INTO feeds (name, url) VALUES
    ('Zeit', 'https://newsfeed.zeit.de/index'),
    ('Tagesschau', 'https://www.tagesschau.de/xml/rss2'),
    ('Sueddeutsche', 'https://rss.sueddeutsche.de/rss/Alles'),
    ('FAZ', 'https://www.faz.net/rss/aktuell/'),
    ('Die Welt', 'https://www.welt.de/feeds/latest.rss'),
    ('Handelsblatt', 'https://www.handelsblatt.com/contentexport/feed/schlagzeilen'),
    ('n-tv', 'https://www.n-tv.de/rss'),
    ('Stern', 'https://www.stern.de/feed/standard/alle-nachrichten/'),
    ('Telekom', 'https://www.t-online.de/feed.rss'),
    ('TAZ', 'https://taz.de/!p4608;rss/'),
    ('Deutschlandfunk', 'https://www.deutschlandfunk.de/nachrichten-100.rss')
ON CONFLICT (url) DO NOTHING;
```

**Hinweis:** 11 statt 12 Feeds — Focus.de (FEED-06) ist nicht enthalten.

### Pattern 3: get_active_feeds() in der Database-Klasse

Konsistent mit dem bestehenden Stil von `database.py` (RealDictCursor, try/except, Logger):

```python
def get_active_feeds(self) -> list:
    """
    Hole alle aktiven RSS-Feeds aus der Datenbank.

    Returns:
        Liste von Dicts mit 'name' und 'url' Feldern
    """
    query = """
        SELECT id, name, url
        FROM feeds
        WHERE active = TRUE
        ORDER BY name ASC;
    """
    try:
        with self.get_cursor(cursor_factory=RealDictCursor) as cur:
            cur.execute(query)
            results = cur.fetchall()
            return [dict(row) for row in results]
    except Exception as e:
        logger.error(f"Fehler beim Laden der aktiven Feeds: {e}")
        return []
```

### Pattern 4: Worker-Integration — DB-Lesepfad

In `background_worker.py`, `_fetch_headlines()` ersetzt den Import von `RSS_FEEDS` durch einen DB-Call:

```python
def _fetch_headlines(self):
    """Hole Headlines von RSS-Feeds (aus PostgreSQL)"""
    import feedparser
    import requests

    db = get_database()
    feeds = db.get_active_feeds()

    if not feeds:
        logger.warning("Keine aktiven Feeds in der Datenbank gefunden")
        return []

    headlines = []
    processed_links = set()
    num_headlines_per_source = 1

    for feed_row in feeds:
        source = feed_row['name']
        url = feed_row['url']
        # ... restliche Fetch-Logik unverändert ...
```

**Wichtig:** Die Schleife iteriert jetzt über `feeds` (Liste von Dicts mit `name`/`url`) statt über `rss_feeds.items()` (Dict). Der innere Fetch-Code bleibt identisch.

### Pattern 5: app.py RSS_FEEDS Migration

`app.py` nutzt `rss_feeds = RSS_FEEDS` an Zeile 54 für den `/api/news` Endpoint (Legacy). Nach der Migration holt es die Feeds lazy beim Request aus der DB:

```python
# Statt: rss_feeds = RSS_FEEDS
# Am Anfang der Route-Handler, die rss_feeds benötigen:
from database import get_database
rss_feeds_list = get_database().get_active_feeds()
rss_feeds = {f['name']: f['url'] for f in rss_feeds_list}
```

### Pattern 6: shared_config.py — was bleibt, was geht

`shared_config.py` enthält zwei Dinge:
1. `RSS_FEEDS` — nach Migration obsolet, kann entfernt werden
2. `get_sentiment_category()` — wird weiterhin in `app.py` und `background_worker.py` genutzt

**Entscheidung:** `get_sentiment_category()` in `shared_config.py` belassen und nur den `RSS_FEEDS`-Dict entfernen. Alternativ kann die Funktion in `database.py` oder eine neue `utils.py` wandern — aber für Phase 9 ist das kleinste Diff: nur den Dict-Eintrag aus `shared_config.py` entfernen.

### Anti-Patterns zu vermeiden

- **Nicht:** init.sql auf dem Produktionssystem manuell laden und alle bestehenden Tabellen droppen — zerstört Produktionsdaten.
- **Nicht:** Feed-Liste beim Start einmalig in eine In-Memory-Variable laden und dann nie aktualisieren — Phase 10 braucht Live-Updates ohne Worker-Neustart.
- **Nicht:** `RSS_FEEDS` aus shared_config.py als Fallback behalten — verursacht Split-Brain wenn DB-Feeds von shared_config.py abweichen.

---

## Don't Hand-Roll

| Problem | Nicht bauen | Stattdessen nutzen | Warum |
|---------|------------|-------------------|-------|
| Migrations-Tracking | Eigenes migrations_log-System | Einfaches `CREATE TABLE IF NOT EXISTS` | Nur eine Migration in Phase 9, kein Alembic-Overhead gerechtfertigt |
| Feed-Validierung | URL-Erreichbarkeit prüfen beim Insert | Nichts — das ist Phase 10 (FEED-05) | Out of scope für Phase 9 |
| ORM | SQLAlchemy Model für feeds | Raw SQL mit psycopg2 via get_cursor() | Konsistent mit bestehender Codebasis |

---

## Runtime State Inventory

| Kategorie | Gefundenes | Aktion |
|-----------|-----------|--------|
| Stored data | PostgreSQL Volume auf Server: `/opt/stacks/auraos-moodlight/data/postgres` — enthält sentiment_history, device_statistics — **noch keine feeds-Tabelle** | Migrations-SQL ausführen (code edit + manueller psql-Aufruf) |
| Live service config | Docker Container `moodlight-analyzer` läuft auf Port 6237 — kein Neustart nötig für Schema-Änderung, aber notwendig damit Worker neuen Code lädt | `docker-compose restart news-analyzer` oder `up -d --build` |
| OS-registered state | Keine — Docker Compose managed alles | Keine |
| Secrets/env vars | DATABASE_URL in docker-compose.yaml — unverändert | Keine |
| Build artifacts | `./:/app` Volume-Mount — Source-Änderungen werden live übernommen ohne Rebuild | Kein Rebuild nötig für Python-Änderungen; `docker-compose restart news-analyzer` reicht |

**Migrations-Ausführung:** Das Migrations-SQL muss einmalig auf dem Server via `docker exec` oder `psql` ausgeführt werden, bevor der Worker neugestartet wird. Reihenfolge: (1) SQL ausführen, (2) App-Container neustarten.

---

## Common Pitfalls

### Pitfall 1: init.sql wird auf dem laufenden System nicht nochmal ausgeführt

**Was schiefgeht:** Entwickler ergänzt feeds-Tabelle in init.sql und erwartet, dass sie nach `docker-compose up -d` auf dem Server existiert.

**Warum:** PostgreSQL führt `init.sql` nur aus, wenn das Daten-Volume zum ersten Mal erstellt wird. Das Volume `/opt/stacks/auraos-moodlight/data/postgres` existiert bereits und enthält Produktionsdaten.

**Vermeidung:** Separates Migrations-SQL (`migrations/001_add_feeds_table.sql`) erstellen und Deployment-Anweisung im Plan dokumentieren. init.sql trotzdem ergänzen — für zukünftige Neuinstallationen.

### Pitfall 2: Worker crasht wenn feeds-Tabelle leer oder nicht vorhanden

**Was schiefgeht:** Worker startet, ruft `get_active_feeds()` auf, bekommt leere Liste oder Exception, und führt dann `_perform_update()` mit 0 Headlines durch — kein Sentiment wird gespeichert.

**Warum:** Deployment-Reihenfolge: App startet neu, Worker feuert sofort (10s Delay), aber Migration wurde noch nicht ausgeführt.

**Vermeidung:** `get_active_feeds()` gibt bei Exception eine leere Liste zurück (bereits im Pattern oben berücksichtigt). `_fetch_headlines()` prüft `if not feeds: return []`. `_perform_update()` bricht ab bei `if not headlines`. Kein Absturz — nur ein übersprungener Zyklus.

### Pitfall 3: source_count in save_sentiment() ist hardcoded auf 12

**Was schiefgeht:** `background_worker.py` Zeile 107 ruft `db.save_sentiment(..., source_count=12, ...)` auf — nach Migration mit 11 Feeds (ohne Focus.de) ist das falsch.

**Vermeidung:** `source_count=len(feeds)` statt hardcoded `12` übergeben. Feeds-Liste muss in `_perform_update()` sichtbar sein — entweder als Return-Wert von `_fetch_headlines()` oder via separaten DB-Call.

### Pitfall 4: get_sentiment_category() Duplikation bleibt

**Was schiefgeht:** Phase 9 entfernt RSS_FEEDS aus shared_config.py, aber lässt get_sentiment_category() dort. Spätere Phasen könnten denken shared_config.py sei "tot" und löschen die Funktion, breaking app.py und background_worker.py.

**Vermeidung:** Kommentar in shared_config.py ergänzen: "RSS_FEEDS wurde nach Phase 9 entfernt. get_sentiment_category() wird noch von app.py und background_worker.py genutzt."

---

## Code Examples

### Tabellendefinition (vollständig, konsistent mit init.sql-Stil)

```sql
-- Source: Analyse des bestehenden init.sql-Patterns
-- ===== FEEDS TABLE =====
-- RSS-Feeds für Sentiment-Analyse — verwaltet via API (Phase 10)
CREATE TABLE IF NOT EXISTS feeds (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    url TEXT NOT NULL UNIQUE,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    last_fetched_at TIMESTAMPTZ,
    error_count INTEGER NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_feeds_active ON feeds(active);
CREATE INDEX IF NOT EXISTS idx_feeds_url ON feeds(url);

COMMENT ON TABLE feeds IS 'RSS-Feeds für Sentiment-Analyse';
COMMENT ON COLUMN feeds.active IS 'Inaktive Feeds werden vom Worker ignoriert';
COMMENT ON COLUMN feeds.error_count IS 'Anzahl aufeinanderfolgender Fehler beim Feed-Abruf';
```

### Default-Daten INSERT (11 Feeds, kein Focus.de)

```sql
-- Source: shared_config.py RSS_FEEDS Dict (bereinigt)
INSERT INTO feeds (name, url) VALUES
    ('Zeit', 'https://newsfeed.zeit.de/index'),
    ('Tagesschau', 'https://www.tagesschau.de/xml/rss2'),
    ('Sueddeutsche', 'https://rss.sueddeutsche.de/rss/Alles'),
    ('FAZ', 'https://www.faz.net/rss/aktuell/'),
    ('Die Welt', 'https://www.welt.de/feeds/latest.rss'),
    ('Handelsblatt', 'https://www.handelsblatt.com/contentexport/feed/schlagzeilen'),
    ('n-tv', 'https://www.n-tv.de/rss'),
    ('Stern', 'https://www.stern.de/feed/standard/alle-nachrichten/'),
    ('Telekom', 'https://www.t-online.de/feed.rss'),
    ('TAZ', 'https://taz.de/!p4608;rss/'),
    ('Deutschlandfunk', 'https://www.deutschlandfunk.de/nachrichten-100.rss')
ON CONFLICT (url) DO NOTHING;
```

### Vollständiger _fetch_headlines()-Ersatz

```python
# Source: Analyse von background_worker.py _fetch_headlines()
def _fetch_headlines(self):
    """
    Hole Headlines von RSS-Feeds (aus PostgreSQL-Datenbank)
    """
    import feedparser
    import requests

    db = get_database()
    feeds = db.get_active_feeds()

    if not feeds:
        logger.warning("Keine aktiven Feeds in der Datenbank gefunden — Update übersprungen")
        return []

    headlines = []
    processed_links = set()
    num_headlines_per_source = 1  # Für Background-Worker nur 1 Headline/Quelle

    for feed_row in feeds:
        source = feed_row['name']
        url = feed_row['url']
        try:
            try:
                response = requests.get(url, timeout=15, headers={'User-Agent': 'WorldMoodAnalyzer/2.0'})
                response.raise_for_status()
                feed = feedparser.parse(response.content)
            except requests.exceptions.Timeout:
                logger.warning(f"Timeout bei {source}")
                continue
            except requests.exceptions.RequestException as e:
                logger.warning(f"Fehler beim Abrufen von {source}: {e}")
                continue

            if feed.bozo and isinstance(feed.bozo_exception, Exception):
                logger.warning(f"Feed-Fehler bei {source}: {feed.bozo_exception}")
                continue

            headlines_from_source = 0
            if feed.entries:
                for entry in feed.entries:
                    if headlines_from_source >= num_headlines_per_source:
                        break
                    link = getattr(entry, 'link', None)
                    title = getattr(entry, 'title', None)
                    if title and title.strip():
                        headline_text = title.strip()
                        unique_key = link if link else headline_text
                        if unique_key not in processed_links:
                            headlines.append({
                                "headline": headline_text,
                                "source": source,
                                "link": link
                            })
                            processed_links.add(unique_key)
                            headlines_from_source += 1

        except Exception as e:
            logger.error(f"Fehler bei {source}: {e}")

    return headlines
```

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| PostgreSQL | feeds-Tabelle | Produktiv auf Server | 16-alpine | — |
| psycopg2-binary | get_active_feeds() | Bereits installiert | 2.9.9 | — |
| Docker Compose | Deployment | Produktiv auf Server | — | — |

**Keine blockierenden fehlenden Dependencies.**

---

## Open Questions

1. **Migrations-Ausführungsweg auf dem Server**
   - Was wir wissen: Docker-Volume enthält Produktionsdaten, init.sql wird nicht nochmal ausgeführt
   - Was unklar ist: Soll der Plan eine konkrete `docker exec`-Zeile vorgeben oder nur das SQL-File erstellen?
   - Empfehlung: Plan erstellt `migrations/001_add_feeds_table.sql` und dokumentiert die Ausführungszeile als Deployment-Schritt.

2. **Umgang mit source_count in save_sentiment()**
   - Was wir wissen: Hardcoded auf 12, wird nach Migration falsch sein (11 Feeds)
   - Empfehlung: `len(feeds)` als Parameter übergeben — erfordert, dass feeds-Liste in `_perform_update()` verfügbar ist. Entweder `_fetch_headlines()` gibt ein Tupel `(headlines, feeds)` zurück oder `_perform_update()` lädt Feeds separat.

---

## Sources

### Primary (HIGH confidence)
- Direkte Codeanalyse: `sentiment-api/database.py` — Database-Klasse API, get_cursor(), commit()-Pattern
- Direkte Codeanalyse: `sentiment-api/init.sql` — Bestehende Schema-Konventionen, Tabellenmuster, Index-Pattern, INSERT-Patterns
- Direkte Codeanalyse: `sentiment-api/background_worker.py` — _fetch_headlines()-Logik, get_database()-Nutzung
- Direkte Codeanalyse: `sentiment-api/shared_config.py` — RSS_FEEDS Dict (12 Feeds incl. Focus.de)
- Direkte Codeanalyse: `sentiment-api/docker-compose.yaml` — Volume-Mount-Strategie für init.sql

### Secondary (MEDIUM confidence)
- PostgreSQL-Dokumentation (Training): `CREATE TABLE IF NOT EXISTS` Idempotenz, `ON CONFLICT DO NOTHING` Syntax
- PostgreSQL-Dokumentation (Training): init-Skripte werden nur beim ersten Volume-Start ausgeführt

---

## Metadata

**Confidence breakdown:**
- Standard Stack: HIGH — Keine neuen Libraries, bestehende Codebasis vollständig analysiert
- Architecture: HIGH — Patterns direkt aus bestehendem Code abgeleitet
- Pitfalls: HIGH — PostgreSQL init.sql-Verhalten ist bekannt und verifiziert durch docker-compose.yaml-Analyse

**Research date:** 2026-03-26
**Valid until:** 2026-06-26 (stabiler Stack, 90 Tage Gültigkeit)
