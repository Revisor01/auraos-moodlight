-- Migration: settings-Tabelle anlegen (Phase 19 — Einstellungs-Persistenz)
-- Idempotent: kann mehrfach ausgefuehrt werden (IF NOT EXISTS, ON CONFLICT DO NOTHING)
-- Ausfuehrung auf Produktions-DB:
--   docker exec -i moodlight-postgres psql -U moodlight moodlight < migrate_settings.sql

SET TIME ZONE 'Europe/Berlin';

CREATE TABLE IF NOT EXISTS settings (
    key VARCHAR(100) PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

COMMENT ON TABLE settings IS 'Key-Value-Store für Backend-Konfiguration';
COMMENT ON COLUMN settings.key IS 'Einstellungs-Schlüssel: analysis_interval, headlines_per_source, anthropic_api_key, admin_password_hash';
COMMENT ON COLUMN settings.value IS 'Einstellungs-Wert als TEXT (Zahlen werden als String gespeichert)';

-- Trigger: Auto-Update updated_at bei Änderung
-- Funktion update_updated_at_column() muss bereits existieren (aus init.sql)
DROP TRIGGER IF EXISTS trigger_update_settings ON settings;
CREATE TRIGGER trigger_update_settings
    BEFORE UPDATE ON settings
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- Default-Einträge — ON CONFLICT DO NOTHING damit bestehende Werte erhalten bleiben
INSERT INTO settings (key, value) VALUES
    ('analysis_interval', '1800'),
    ('headlines_per_source', '1'),
    ('anthropic_api_key', ''),
    ('admin_password_hash', '')
ON CONFLICT (key) DO NOTHING;

DO $$
BEGIN
    RAISE NOTICE 'settings-Tabelle erfolgreich migriert!';
END $$;
