# -*- coding: utf-8 -*-
"""
Prüfskript für die Definitionsreihenfolge in init.sql (Review-Runde 2, B-KRITISCH-2).

Eine frische DB kann in diesen Tests nicht aufgebaut werden — stattdessen wird
per Text-Analyse sichergestellt, dass jede Funktion, die von einem Trigger
referenziert wird, VOR dem ersten referenzierenden CREATE TRIGGER-Statement
definiert ist. Das reproduziert exakt den Fehler, der eine frische DB beim
ersten Start brechen würde ("function does not exist").
"""

import os
import re
import unittest

INIT_SQL_PATH = os.path.join(os.path.dirname(__file__), '..', 'init.sql')


class TestInitSqlDefinitionOrder(unittest.TestCase):
    """Stellt sicher, dass Funktionen vor ihrer ersten Verwendung definiert sind."""

    @classmethod
    def setUpClass(cls):
        with open(INIT_SQL_PATH, encoding='utf-8') as f:
            cls.sql = f.read()

    def _first_index_of(self, pattern):
        match = re.search(pattern, self.sql)
        self.assertIsNotNone(match, f"Pattern nicht gefunden: {pattern}")
        return match.start()

    def test_update_updated_at_column_vor_erster_verwendung_definiert(self):
        """
        update_updated_at_column() wird von den Triggern auf settings und
        device_statistics referenziert — die Funktionsdefinition muss vor dem
        ersten EXECUTE FUNCTION-Aufruf stehen.
        """
        function_def_pos = self._first_index_of(
            r"CREATE OR REPLACE FUNCTION update_updated_at_column\(\)"
        )
        first_usage_pos = self._first_index_of(
            r"EXECUTE FUNCTION update_updated_at_column\(\)"
        )
        self.assertLess(
            function_def_pos, first_usage_pos,
            "update_updated_at_column() muss VOR der ersten Verwendung (CREATE TRIGGER) definiert sein"
        )

    def test_settings_tabelle_nach_helper_funktionen(self):
        """Die settings-Tabelle (nutzt den Trigger) muss nach der Helper-Sektion stehen."""
        helper_section_pos = self._first_index_of(r"HELPER FUNCTIONS")
        settings_table_pos = self._first_index_of(
            r"CREATE TABLE IF NOT EXISTS settings"
        )
        self.assertLess(helper_section_pos, settings_table_pos)

    def test_set_sentiment_category_trigger_entfernt(self):
        """
        Kategorie-Duplikat-Logik (B-MITTEL): der DB-seitige Trigger und die
        zugehörige Funktion dürfen nicht mehr in init.sql vorkommen — Python
        (shared_config.get_sentiment_category) ist die einzige Quelle.
        """
        self.assertNotIn("trigger_set_category", self.sql)
        self.assertNotIn("FUNCTION set_sentiment_category", self.sql)
        self.assertNotIn("FUNCTION get_sentiment_category", self.sql)

    def test_migration_002_existiert_und_droppt_trigger(self):
        """migrations/002_drop_category_trigger.sql muss die DROP-Statements enthalten."""
        migration_path = os.path.join(
            os.path.dirname(__file__), '..', 'migrations', '002_drop_category_trigger.sql'
        )
        self.assertTrue(os.path.isfile(migration_path), "Migrationsdatei fehlt")
        with open(migration_path, encoding='utf-8') as f:
            migration_sql = f.read()
        self.assertIn("DROP TRIGGER IF EXISTS", migration_sql)
        self.assertIn("DROP FUNCTION IF EXISTS", migration_sql)


if __name__ == '__main__':
    unittest.main(verbosity=2)
