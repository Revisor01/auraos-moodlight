-- Migrations-Skript: 001_add_feeds_table.sql
-- Zweck: feeds-Tabelle für laufendes Produktionssystem anlegen
-- Datum: Phase 9 — DB-Schema & Worker-Integration
--
-- Ausführung auf dem Server (einmalig):
--   docker exec -i moodlight-postgres psql -U moodlight -d moodlight < migrations/001_add_feeds_table.sql
--
-- Das Skript ist idempotent (kann mehrfach ausgeführt werden ohne Fehler).

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

-- Indizes
CREATE INDEX IF NOT EXISTS idx_feeds_active ON feeds(active);
CREATE INDEX IF NOT EXISTS idx_feeds_url ON feeds(url);

COMMENT ON TABLE feeds IS 'RSS-Feeds für Sentiment-Analyse';
COMMENT ON COLUMN feeds.active IS 'Inaktive Feeds werden vom Worker ignoriert';
COMMENT ON COLUMN feeds.error_count IS 'Anzahl aufeinanderfolgender Fehler beim Feed-Abruf';
COMMENT ON COLUMN feeds.url IS 'UNIQUE — verhindert doppelte Feed-Einträge';

-- Default-Feeds einfügen (ohne Focus.de — FEED-06: gibt 404 zurück)
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

-- Bestätigung
DO $$
BEGIN
    RAISE NOTICE 'Migration 001_add_feeds_table.sql erfolgreich ausgeführt.';
    RAISE NOTICE 'Feeds in Tabelle: %', (SELECT COUNT(*) FROM feeds);
END $$;
