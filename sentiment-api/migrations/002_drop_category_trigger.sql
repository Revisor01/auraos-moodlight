-- Entfernt den Kategorie-Trigger, die Kategorie kommt nur noch aus Python; idempotent.
-- Aufruf: psql -U moodlight -d moodlight < migrations/002_drop_category_trigger.sql

DROP TRIGGER IF EXISTS trigger_set_category ON sentiment_history;
DROP FUNCTION IF EXISTS set_sentiment_category();
DROP FUNCTION IF EXISTS get_sentiment_category(FLOAT);

-- Bestätigung
DO $$
BEGIN
    RAISE NOTICE 'Migration 002_drop_category_trigger.sql erfolgreich ausgeführt.';
END $$;
