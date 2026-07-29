-- Migrations-Skript: 002_drop_category_trigger.sql
-- Zweck: Kategorie-Duplikat-Logik aus der DB entfernen — Python
--        (shared_config.get_sentiment_category) ist jetzt die einzige Quelle
--        der Kategorie-Zuordnung. Der DB-Trigger duplizierte dieselbe Logik
--        mit eigenen Schwellwerten und konnte durch Drift von der Python-Seite
--        abweichen (Review-Runde 2, B-MITTEL).
-- Datum: Review-Runde 2
--
-- Ausführung auf dem Server (einmalig):
--   docker exec -i moodlight-postgres psql -U moodlight -d moodlight < migrations/002_drop_category_trigger.sql
--
-- Das Skript ist idempotent (kann mehrfach ausgeführt werden ohne Fehler).

DROP TRIGGER IF EXISTS trigger_set_category ON sentiment_history;
DROP FUNCTION IF EXISTS set_sentiment_category();
DROP FUNCTION IF EXISTS get_sentiment_category(FLOAT);

-- Bestätigung
DO $$
BEGIN
    RAISE NOTICE 'Migration 002_drop_category_trigger.sql erfolgreich ausgeführt.';
END $$;
