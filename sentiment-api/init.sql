-- PostgreSQL Initialisierungs-Schema für Moodlight System
-- Erstellt automatisch beim ersten Start von docker-compose

-- Datenbank-Einstellungen
SET TIME ZONE 'Europe/Berlin';

-- ===== SENTIMENT HISTORY TABLE =====
-- Speichert alle Sentiment-Analysen zentral
CREATE TABLE IF NOT EXISTS sentiment_history (
    id SERIAL PRIMARY KEY,
    timestamp TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    sentiment_score FLOAT NOT NULL CHECK (sentiment_score BETWEEN -1.0 AND 1.0),
    category VARCHAR(20) NOT NULL,
    headlines_analyzed INTEGER,
    source_count INTEGER DEFAULT 12,
    api_response_time_ms INTEGER,
    metadata JSONB,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Indizes für Performance
CREATE INDEX IF NOT EXISTS idx_sentiment_timestamp ON sentiment_history(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_sentiment_category ON sentiment_history(category);
CREATE INDEX IF NOT EXISTS idx_sentiment_created ON sentiment_history(created_at DESC);

-- ===== DEVICE STATISTICS TABLE =====
-- Tracking aller Moodlight-Geräte
CREATE TABLE IF NOT EXISTS device_statistics (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(50) UNIQUE NOT NULL,
    device_name VARCHAR(100),
    mac_address VARCHAR(17),
    firmware_version VARCHAR(20),
    first_seen TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_seen TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    total_requests INTEGER DEFAULT 0,
    location VARCHAR(100),
    owner VARCHAR(100),
    ip_address VARCHAR(45),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Indizes
CREATE INDEX IF NOT EXISTS idx_device_last_seen ON device_statistics(last_seen DESC);
CREATE INDEX IF NOT EXISTS idx_device_id ON device_statistics(device_id);

-- ===== DEVICE REQUESTS TABLE =====
-- Optional: Detaillierte Request-Logs pro Gerät
CREATE TABLE IF NOT EXISTS device_requests (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    timestamp TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    endpoint VARCHAR(100),
    response_time_ms INTEGER,
    http_status INTEGER,
    user_agent TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Indizes
CREATE INDEX IF NOT EXISTS idx_device_requests_timestamp ON device_requests(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_device_requests_device ON device_requests(device_id);
CREATE INDEX IF NOT EXISTS idx_device_requests_endpoint ON device_requests(endpoint);

-- Fremdschlüssel (optional - erlaubt auch unbekannte Geräte)
-- ALTER TABLE device_requests ADD CONSTRAINT fk_device
--   FOREIGN KEY (device_id) REFERENCES device_statistics(device_id) ON DELETE CASCADE;

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

-- Indizes für Performance
CREATE INDEX IF NOT EXISTS idx_feeds_active ON feeds(active);
CREATE INDEX IF NOT EXISTS idx_feeds_url ON feeds(url);

COMMENT ON TABLE feeds IS 'RSS-Feeds für Sentiment-Analyse';
COMMENT ON COLUMN feeds.active IS 'Inaktive Feeds werden vom Worker ignoriert';
COMMENT ON COLUMN feeds.error_count IS 'Anzahl aufeinanderfolgender Fehler beim Feed-Abruf';

-- ===== HEADLINES TABLE =====
-- Einzelne Headlines mit Einzel-Scores pro Sentiment-Analyse (Phase 12)
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

-- ===== SETTINGS TABLE =====
-- Key-Value-Store für Backend-Konfiguration (Phase 19)
CREATE TABLE IF NOT EXISTS settings (
    key VARCHAR(100) PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

COMMENT ON TABLE settings IS 'Key-Value-Store für Backend-Konfiguration';
COMMENT ON COLUMN settings.key IS 'Einstellungs-Schlüssel: analysis_interval, headlines_per_source, anthropic_api_key, admin_password_hash';
COMMENT ON COLUMN settings.value IS 'Einstellungs-Wert als TEXT (Zahlen werden als String gespeichert)';

-- Trigger: Auto-Update updated_at bei Änderung
DROP TRIGGER IF EXISTS trigger_update_settings ON settings;
CREATE TRIGGER trigger_update_settings
    BEFORE UPDATE ON settings
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- Default-Werte — werden beim Start durch DB-Werte oder Env-Variablen überschrieben
-- analysis_interval: Sekunden zwischen Sentiment-Updates (1800 = 30 Min)
-- headlines_per_source: Headlines pro RSS-Feed pro Analyse
-- anthropic_api_key: Leer — wird aus ANTHROPIC_API_KEY Env-Variable geladen
-- admin_password_hash: Leer — wird aus ADMIN_PASSWORD Env-Variable berechnet
INSERT INTO settings (key, value) VALUES
    ('analysis_interval', '1800'),
    ('headlines_per_source', '1'),
    ('anthropic_api_key', ''),
    ('admin_password_hash', '')
ON CONFLICT (key) DO NOTHING;

-- Default-Feeds (ohne Focus.de — FEED-06)
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

-- ===== HELPER FUNCTIONS =====

-- Funktion: Kategorie aus Sentiment-Score bestimmen
CREATE OR REPLACE FUNCTION get_sentiment_category(score FLOAT)
RETURNS VARCHAR(20) AS $$
BEGIN
    IF score >= 0.30 THEN RETURN 'sehr positiv';
    ELSIF score >= 0.10 THEN RETURN 'positiv';
    ELSIF score >= -0.20 THEN RETURN 'neutral';
    ELSIF score >= -0.50 THEN RETURN 'negativ';
    ELSE RETURN 'sehr negativ';
    END IF;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

-- Funktion: Automatische Kategorie beim Insert/Update
CREATE OR REPLACE FUNCTION set_sentiment_category()
RETURNS TRIGGER AS $$
BEGIN
    NEW.category := get_sentiment_category(NEW.sentiment_score);
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Trigger: Automatische Kategorie-Zuweisung
DROP TRIGGER IF EXISTS trigger_set_category ON sentiment_history;
CREATE TRIGGER trigger_set_category
    BEFORE INSERT OR UPDATE ON sentiment_history
    FOR EACH ROW
    EXECUTE FUNCTION set_sentiment_category();

-- Funktion: Updated_at Timestamp automatisch aktualisieren
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Trigger: Auto-Update updated_at
DROP TRIGGER IF EXISTS trigger_update_device_stats ON device_statistics;
CREATE TRIGGER trigger_update_device_stats
    BEFORE UPDATE ON device_statistics
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- ===== VIEWS =====

-- View: Aktuelle Sentiment-Statistik
CREATE OR REPLACE VIEW v_current_sentiment AS
SELECT
    sentiment_score,
    category,
    timestamp,
    headlines_analyzed,
    created_at
FROM sentiment_history
ORDER BY timestamp DESC
LIMIT 1;

-- View: 24h Sentiment-Verlauf
CREATE OR REPLACE VIEW v_sentiment_24h AS
SELECT
    timestamp,
    sentiment_score,
    category,
    headlines_analyzed
FROM sentiment_history
WHERE timestamp >= NOW() - INTERVAL '24 hours'
ORDER BY timestamp ASC;

-- View: Aktive Geräte (last_seen < 2h)
CREATE OR REPLACE VIEW v_active_devices AS
SELECT
    device_id,
    device_name,
    firmware_version,
    last_seen,
    total_requests,
    location,
    EXTRACT(EPOCH FROM (NOW() - last_seen)) / 60 AS minutes_since_last_seen
FROM device_statistics
WHERE last_seen >= NOW() - INTERVAL '2 hours'
ORDER BY last_seen DESC;

-- View: Tages-Statistik
CREATE OR REPLACE VIEW v_daily_stats AS
SELECT
    DATE(timestamp) AS date,
    COUNT(*) AS total_analyses,
    ROUND(AVG(sentiment_score)::numeric, 3) AS avg_sentiment,
    ROUND(MIN(sentiment_score)::numeric, 3) AS min_sentiment,
    ROUND(MAX(sentiment_score)::numeric, 3) AS max_sentiment,
    ROUND(STDDEV(sentiment_score)::numeric, 3) AS stddev_sentiment,
    SUM(headlines_analyzed) AS total_headlines
FROM sentiment_history
GROUP BY DATE(timestamp)
ORDER BY date DESC;

-- ===== INITIAL DATA =====

-- Beispiel-Eintrag (optional)
-- INSERT INTO sentiment_history (sentiment_score, headlines_analyzed, source_count)
-- VALUES (-0.45, 120, 12);

-- ===== GRANTS =====
-- (Optional: Wenn separate DB-User verwendet werden)
-- GRANT SELECT, INSERT, UPDATE ON sentiment_history TO moodlight_app;
-- GRANT SELECT, INSERT, UPDATE ON device_statistics TO moodlight_app;
-- GRANT SELECT ON v_current_sentiment TO moodlight_readonly;

-- ===== KOMMENTARE =====
COMMENT ON TABLE sentiment_history IS 'Zentrale Speicherung aller Sentiment-Analysen';
COMMENT ON TABLE device_statistics IS 'Tracking aller registrierten Moodlight-Geräte';
COMMENT ON TABLE device_requests IS 'Detaillierte Request-Logs pro Gerät (optional)';
COMMENT ON COLUMN sentiment_history.sentiment_score IS 'Sentiment-Wert von -1.0 (sehr negativ) bis +1.0 (sehr positiv)';
COMMENT ON COLUMN sentiment_history.category IS 'Automatisch berechnete Kategorie basierend auf Score';
COMMENT ON COLUMN device_statistics.device_id IS 'Eindeutige Geräte-ID (ESP32 MAC-Hash)';
COMMENT ON TABLE feeds IS 'RSS-Feeds für Sentiment-Analyse';
COMMENT ON COLUMN feeds.url IS 'UNIQUE — verhindert doppelte Feed-Einträge';

-- Fertig!
DO $$
BEGIN
    RAISE NOTICE 'Moodlight Database Schema erfolgreich initialisiert!';
END $$;
