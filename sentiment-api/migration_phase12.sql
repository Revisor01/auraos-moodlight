-- Migration Phase 12: headlines-Tabelle für Headline-Persistenz
-- Sicher bei Mehrfachausführung (IF NOT EXISTS Guards)
-- Ausführung: docker exec -i <postgres-container> psql -U moodlight -d moodlight < migration_phase12.sql

SET TIME ZONE 'Europe/Berlin';

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

DO $$
BEGIN
    RAISE NOTICE 'Migration Phase 12: headlines-Tabelle bereit';
END $$;
