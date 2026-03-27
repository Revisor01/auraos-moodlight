# Phase 12: Headline-Persistenz - Research

**Researched:** 2026-03-26
**Domain:** PostgreSQL Schema-Erweiterung + Python Backend (psycopg2 Bulk-INSERT)
**Confidence:** HIGH

## Summary

Phase 12 erweitert das bestehende Backend um dauerhafte Speicherung der einzelnen Headlines mit ihren Einzel-Scores nach jeder Sentiment-Analyse. Aktuell speichert `_perform_update()` im Background Worker nur den aggregierten `sentiment_score` in `sentiment_history`. Die Funktion `analyze_headlines_openai_batch()` liefert bereits ein `results`-Array mit vollständigen Einzel-Daten (headline, source, sentiment, strength) — diese Daten werden bisher nur im RAM gehalten und gehen nach dem Analyse-Zyklus verloren.

Die Umsetzung erfordert drei koordinierte Änderungen: (1) eine neue `headlines`-Tabelle in der PostgreSQL-Datenbank mit Fremdschlüssel zu `sentiment_history.id` und `feeds.id`, (2) eine neue `save_headlines()`-Methode in der `Database`-Klasse mit Bulk-INSERT via `executemany`, (3) eine Erweiterung von `_perform_update()` im Background Worker, die nach `save_sentiment()` die `results`-Liste in die neue Tabelle schreibt. Da `save_sentiment()` bereits die neue `sentiment_history.id` per `RETURNING id` zurückgibt, ist die Verknüpfung ohne zusätzliche Queries möglich.

Der FK zu `feeds.id` erfordert, dass `_fetch_headlines()` die `feed_id` aus der Datenbank mit durchreicht — aktuell enthält das `headlines`-Dict nur `source` (Name) und `link`, nicht die numerische ID. Das ist die einzige nicht-triviale Änderung im Datenpfad.

**Primary recommendation:** Headlines-Tabelle mit `sentiment_history_id` FK (NOT NULL) und `feed_id` FK (NULLABLE als Fallback), Bulk-INSERT via `executemany`, feed_id durch Erweiterung von `_fetch_headlines()`.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- headlines-Tabelle braucht Fremdschlüssel zu feeds und sentiment_updates
- Bestehende Analyse-Funktion analyze_headlines_openai_batch() liefert bereits Einzel-Scores
- Die Scores müssen nach der Analyse in die DB geschrieben werden (background_worker.py)
- Performance: Bulk-INSERT für ~100 Headlines pro Analyse-Zyklus
- Bestehende Funktionalität darf nicht brechen

### Claude's Discretion
All implementation choices are at Claude's discretion — pure infrastructure phase.

### Deferred Ideas (OUT OF SCOPE)
Keine — Infrastruktur-Phase.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| HEAD-01 | Backend speichert bei jeder Analyse die einzelnen Headlines mit ihren Einzel-Scores in der DB | headlines-Tabelle + save_headlines() + _perform_update()-Erweiterung |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| psycopg2-binary | 2.9.9 (bereits installiert) | PostgreSQL Bulk-INSERT via executemany | Bereits im Projekt, effizienter als einzelne INSERTs |
| PostgreSQL | 16-alpine (bereits laufend) | Persistente Speicherung | Bereits im Projekt |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| psycopg2 extras.execute_values | Built-in in 2.9.9 | Schnellerer Bulk-INSERT als executemany | Für >50 Rows pro Batch bevorzugt |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| executemany | execute_values (extras) | execute_values generiert ein einzelnes INSERT mit VALUES-Liste — schneller bei großen Batches, aber executemany ausreichend für ~11 Headlines/Zyklus |
| FK zu feeds(id) NULLABLE | FK zu feeds(id) NOT NULL | NOT NULL erzwingt feed_id im Insert; NULLABLE ermöglicht Fallback wenn Feed gelöscht wurde — NULLABLE ist robuster |

**Installation:** Keine neuen Pakete — alles bereits vorhanden.

## Architecture Patterns

### Recommended Project Structure
Keine strukturellen Änderungen — alle Änderungen in bestehenden Dateien:
```
sentiment-api/
├── init.sql              # +headlines Tabelle, +Index, +Migration-SQL-Block
├── database.py           # +save_headlines() Methode
└── background_worker.py  # _perform_update() erweitert: results mitgeben, save_headlines() aufrufen
```

### Pattern 1: Migrations-SQL für Produktionssystem
**Was:** Da `init.sql` nur beim ersten `docker-compose up` ausgeführt wird (PostgreSQL Volume bereits befüllt), braucht das Produktionssystem ein separates Migrations-SQL.
**Wann verwenden:** Immer wenn ein laufendes System ein neues DB-Schema bekommt.
**Vorgehen:** Migration als Block am Ende von `init.sql` mit `IF NOT EXISTS` Guard einbauen, plus separates `migration_phase12.sql` für die manuelle Einmalausführung auf dem Server.

```sql
-- In init.sql einfügen (Phase 12):
CREATE TABLE IF NOT EXISTS headlines (
    id SERIAL PRIMARY KEY,
    sentiment_history_id INTEGER NOT NULL REFERENCES sentiment_history(id) ON DELETE CASCADE,
    feed_id INTEGER REFERENCES feeds(id) ON DELETE SET NULL,
    headline TEXT NOT NULL,
    source_name VARCHAR(100) NOT NULL,
    sentiment_score FLOAT NOT NULL CHECK (sentiment_score BETWEEN -1.0 AND 1.0),
    strength VARCHAR(20) NOT NULL,
    link TEXT,
    analyzed_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_headlines_sentiment_history
    ON headlines(sentiment_history_id);
CREATE INDEX IF NOT EXISTS idx_headlines_feed
    ON headlines(feed_id);
CREATE INDEX IF NOT EXISTS idx_headlines_analyzed_at
    ON headlines(analyzed_at DESC);
```

### Pattern 2: feed_id Durchreichen durch den Datenpfad
**Was:** `_fetch_headlines()` lädt Feeds bereits aus der DB (`db.get_active_feeds()` gibt `id`, `name`, `url` zurück). Die `id` muss im `headlines`-Dict mitgereicht werden.
**Aktuell:** `{"headline": ..., "source": source_name, "link": link}`
**Neu:** `{"headline": ..., "source": source_name, "link": link, "feed_id": feed_row['id']}`

`analyze_headlines_openai_batch()` in `app.py` übergibt die Einzel-Dicts transparent durch das `results`-Array. Die `feed_id` überlebt die Analyse-Funktion, wenn sie im Input-Dict enthalten ist, da `original_headline_obj` direkt übernommen wird:

```python
# In analyze_headlines_openai_batch() — Zeilen 192-197 in app.py
results.append({
    "headline": original_headline_obj['headline'].strip(),
    "source": original_headline_obj.get('source', 'unknown'),
    "feed_id": original_headline_obj.get('feed_id'),  # wird durchgereicht
    "sentiment": score,
    "strength": strength
})
```

Dies ist die einzige Änderung in `app.py`.

### Pattern 3: save_headlines() Bulk-INSERT mit executemany
**Was:** Neue Methode in `database.py`, die eine Liste von Analyse-Ergebnissen in die `headlines`-Tabelle schreibt.
**Warum executemany statt einzelne INSERTs:** ~11 Headlines pro Zyklus — kein signifikanter Unterschied, aber konsistenter mit DB-Best-Practices.

```python
def save_headlines(
    self,
    sentiment_history_id: int,
    results: list
) -> int:
    """
    Speichere analysierte Headlines in der Datenbank.

    Args:
        sentiment_history_id: FK zu sentiment_history.id
        results: Liste von Dicts (headline, source, feed_id, sentiment, strength, link optional)

    Returns:
        Anzahl gespeicherter Headlines
    """
    if not results:
        return 0

    query = """
        INSERT INTO headlines
        (sentiment_history_id, feed_id, headline, source_name, sentiment_score, strength, link)
        VALUES (%s, %s, %s, %s, %s, %s, %s)
    """
    rows = [
        (
            sentiment_history_id,
            r.get('feed_id'),
            r['headline'],
            r.get('source', 'unknown'),
            r['sentiment'],
            r.get('strength', 'neutral'),
            r.get('link')
        )
        for r in results
    ]

    try:
        with self.get_cursor() as cur:
            cur.executemany(query, rows)
            self.conn.commit()
            logger.info(f"Headlines gespeichert: {len(rows)} Einträge für sentiment_history_id={sentiment_history_id}")
            return len(rows)
    except Exception as e:
        if self.conn:
            try:
                self.conn.rollback()
            except Exception:
                pass
        logger.error(f"Fehler beim Speichern der Headlines: {e}")
        raise
```

### Pattern 4: _perform_update() Erweiterung
**Was:** Nach `db.save_sentiment()` (gibt `sentiment_history_id` zurück) werden die `results` aus `analysis_result` direkt an `save_headlines()` übergeben.

```python
# In _perform_update() — nach dem bestehenden save_sentiment()-Call:
sentiment_history_id = db.save_sentiment(...)  # gibt bereits id zurück
logger.info(f"Sentiment in DB gespeichert: {sentiment_score:.3f}")

# NEU: Headlines speichern
headline_results = analysis_result.get('results', [])
if headline_results:
    saved_count = db.save_headlines(
        sentiment_history_id=sentiment_history_id,
        results=headline_results
    )
    logger.info(f"Headlines persistiert: {saved_count} Einträge")
```

### Anti-Patterns to Avoid
- **Kein separater SELECT für feed_id nach der Analyse:** feed_id muss im Datenpfad mitgeführt werden, nicht nachträglich per JOIN aufgelöst werden.
- **Kein Abbruch bei save_headlines()-Fehler:** Headline-Persistenz ist sekundär — wenn sie fehlschlägt, soll der Sentiment-Score trotzdem gespeichert bleiben. Exception sollte geloggt, aber nicht nach oben propagiert werden (try/except in _perform_update()).
- **Kein ON DELETE RESTRICT für feeds FK:** Feeds können gelöscht werden — `ON DELETE SET NULL` verhindert Constraint-Verletzungen bei nachträglichem Feed-Löschen.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Bulk-INSERT | Einzelne INSERT-Calls in Schleife | executemany oder execute_values | Atomare Transaktion, weniger Round-Trips |
| Migration auf Prod | Manueller ALTER TABLE ohne Guard | `CREATE TABLE IF NOT EXISTS` + IF NOT EXISTS-Checks | Idempotent, kann mehrfach ausgeführt werden |
| feed_id Lookup | Separate DB-Query nach der Analyse | feed_id im _fetch_headlines()-Dict mitführen | 0 zusätzliche DB-Queries nötig |

**Key insight:** Die Analyse-Funktion `analyze_headlines_openai_batch()` ist so gebaut, dass sie `original_headline_obj` transparent durchreicht — beliebige Keys aus dem Input-Dict sind im Output-`results` verfügbar.

## Common Pitfalls

### Pitfall 1: init.sql wird auf Produktionssystem nicht erneut ausgeführt
**What goes wrong:** `CREATE TABLE headlines` im `init.sql` hat keinen Effekt, weil das PostgreSQL-Volume bereits existiert und Docker Compose `init.sql` nur beim ersten Start (leerem Volume) ausführt.
**Why it happens:** PostgreSQL Docker entrypoint-Skripte überspringen `init.sql` wenn die Datenbank bereits initialisiert ist.
**How to avoid:** Migrations-SQL separat erstellen und manuell auf dem Server ausführen: `docker exec -i <postgres-container> psql -U moodlight -d moodlight < migration_phase12.sql`
**Warning signs:** Tabelle `headlines` existiert nicht — `save_headlines()` wirft `relation "headlines" does not exist`.

### Pitfall 2: feed_id fehlt im _fetch_headlines()-Output wenn /api/news-Route aufgerufen wird
**What goes wrong:** `/api/news` in `app.py` baut Headlines ohne `feed_id` (Zeile 376) — diese Route ruft `analyze_headlines_openai_batch()` direkt auf. Die `results` enthalten dann `feed_id: None`.
**Why it happens:** Zwei unabhängige Headline-Sammel-Pfade: Background Worker und `/api/news`-Route.
**How to avoid:** Für Phase 12 ist nur der Background Worker relevant (HEAD-01 bezieht sich auf die automatische Persistenz). `/api/news` ist ein Legacy-Endpoint ohne Persistenz-Anforderung — kein Handlungsbedarf in dieser Phase.
**Warning signs:** `feed_id` ist `None` in gespeicherten Headlines — akzeptabel wegen `NULLABLE` FK.

### Pitfall 3: Transaktion zwischen save_sentiment() und save_headlines() nicht atomar
**What goes wrong:** `sentiment_history`-Eintrag wird gespeichert, danach schlägt `save_headlines()` fehl — inkonsistenter Zustand (Sentiment ohne Headlines).
**Why it happens:** Zwei separate Commits in zwei Methoden.
**How to avoid:** Für Phase 12 ist dies akzeptabel — Headlines sind sekundäre Daten, der Sentiment-Score bleibt konsistent. Exception in `save_headlines()` wird in `_perform_update()` gecatcht und geloggt, aber nicht weitergeworfen. Keine gemeinsame Transaktion nötig.
**Warning signs:** Log-Eintrag "Fehler beim Speichern der Headlines" — Sentinel für Monitoring.

### Pitfall 4: `save_sentiment()` gibt aktuell keinen Rückgabewert zurück
**What goes wrong:** `_perform_update()` nutzt `db.save_sentiment()` heute ohne Rückgabewert. Für `save_headlines()` wird die `sentiment_history_id` benötigt.
**Why it happens:** `save_sentiment()` enthält `RETURNING id` und `result_id = cur.fetchone()[0]`, gibt aber `result_id` bereits per `return result_id` zurück (Zeile 174 in database.py). Der Aufrufer in `background_worker.py` ignoriert den Rückgabewert aktuell (kein Assignment).
**How to avoid:** In `_perform_update()` den Rückgabewert von `save_sentiment()` assignen: `sentiment_history_id = db.save_sentiment(...)`.
**Warning signs:** Kein Problem in database.py — nur Anpassung des Aufrufers nötig.

## Code Examples

### Bestehender Datenpfad (analysiert)

```python
# background_worker.py — _perform_update() aktuell
analysis_result = self.analyze_function(headlines)
# analysis_result = {
#   "results": [
#     {"headline": "...", "source": "Tagesschau", "sentiment": -0.3, "strength": "negativ"},
#     ...
#   ],
#   "total_sentiment": -0.47,
#   "statistics": {"analyzed_count": 11, ...}
# }
db.save_sentiment(sentiment_score=..., ...)  # gibt id zurück, wird aktuell ignoriert
```

### Erweiterter _fetch_headlines()-Output (neu)

```python
# background_worker.py — _fetch_headlines() — ein Dict-Eintrag
headlines.append({
    "headline": headline_text,
    "source": source,          # Feed-Name (String)
    "link": link,
    "feed_id": feed_row['id']  # NEU: numerische Feed-ID aus DB
})
```

### Migration SQL (separates File)

```sql
-- migration_phase12.sql — auf Produktionssystem einmalig ausführen
-- Sicher bei Mehrfachausführung (IF NOT EXISTS)

CREATE TABLE IF NOT EXISTS headlines (
    id SERIAL PRIMARY KEY,
    sentiment_history_id INTEGER NOT NULL REFERENCES sentiment_history(id) ON DELETE CASCADE,
    feed_id INTEGER REFERENCES feeds(id) ON DELETE SET NULL,
    headline TEXT NOT NULL,
    source_name VARCHAR(100) NOT NULL,
    sentiment_score FLOAT NOT NULL CHECK (sentiment_score BETWEEN -1.0 AND 1.0),
    strength VARCHAR(20) NOT NULL,
    link TEXT,
    analyzed_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_headlines_sentiment_history ON headlines(sentiment_history_id);
CREATE INDEX IF NOT EXISTS idx_headlines_feed ON headlines(feed_id);
CREATE INDEX IF NOT EXISTS idx_headlines_analyzed_at ON headlines(analyzed_at DESC);

COMMENT ON TABLE headlines IS 'Einzelne Headlines mit Einzel-Scores pro Sentiment-Analyse';
COMMENT ON COLUMN headlines.sentiment_history_id IS 'FK zu sentiment_history — welche Analyse';
COMMENT ON COLUMN headlines.feed_id IS 'FK zu feeds — NULLABLE falls Feed gelöscht wurde';
COMMENT ON COLUMN headlines.strength IS 'Kategorie: sehr negativ, negativ, neutral, positiv, sehr positiv';
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Einzel-INSERT in Schleife | executemany (Batch) | Standard seit psycopg2 2.x | Atomare Transaktion, weniger Overhead |
| execute_values (extras) | executemany | — | execute_values minimal schneller, aber executemany ausreichend für <100 Rows |

## Open Questions

1. **Soll `save_headlines()` bei Fehler die Transaktion von `save_sentiment()` rückgängig machen?**
   - Was wir wissen: `save_sentiment()` hat bereits committed — kein Rollback mehr möglich.
   - Was unklar ist: Ist ein Sentiment-Eintrag ohne Headlines ein akzeptabler Zustand?
   - Recommendation: Ja — Headlines sind sekundäre Daten. Sentiment bleibt gespeichert, Fehler wird geloggt.

2. **Wie viele Headlines werden pro Zyklus gespeichert?**
   - Was wir wissen: Background Worker holt 1 Headline/Feed, aktuell 11 aktive Feeds → ~11 Headlines/Zyklus. `/api/news` holt 2/Feed → ~22.
   - Auswirkung: Sehr kleiner Batch — executemany ist mehr als ausreichend, kein Optimierungsbedarf.

## Environment Availability

Step 2.6: SKIPPED — Phase 12 ist eine reine Code/Schema-Änderung am bestehenden Docker Stack. Alle externen Dependencies (PostgreSQL, Redis, Python, psycopg2) sind bereits laufend und in CLAUDE.md dokumentiert.

## Sources

### Primary (HIGH confidence)
- Direkter Code-Analyse von `sentiment-api/database.py`, `background_worker.py`, `app.py`, `init.sql` — vollständige Kenntnis des bestehenden Systems
- psycopg2 Dokumentation (bekannte API, Version 2.9.9 im Einsatz): executemany, cursor.execute mit RETURNING

### Secondary (MEDIUM confidence)
- PostgreSQL 16 CASCADE/SET NULL FK-Semantik — Standard, unverändert seit PostgreSQL 9.x

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — alle verwendeten Libraries bereits im Projekt, kein neuer Code nötig
- Architecture: HIGH — Datenpfad vollständig nachvollzogen, keine Unbekannten
- Pitfalls: HIGH — Produktionsmigration-Problem und save_sentiment()-Rückgabewert sind konkret identifiziert

**Research date:** 2026-03-26
**Valid until:** 2026-06-26 (stabiles Stack, keine schnellen Änderungen erwartet)
