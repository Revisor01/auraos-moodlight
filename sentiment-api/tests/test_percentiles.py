# -*- coding: utf-8 -*-
"""
Unit-Tests für get_score_percentiles() und compute_led_index()

Tests sind absichtlich ohne DB-Verbindung ausführbar:
- compute_led_index wird direkt importiert und getestet
- get_score_percentiles wird über Mock-Objekte getestet
"""

import sys
import os
import unittest
from unittest.mock import MagicMock, patch, PropertyMock

# Sentiment-API Verzeichnis in Suchpfad aufnehmen
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

# psycopg2 und redis mocken damit Tests lokal ohne installierte Pakete laufen
sys.modules.setdefault('psycopg2', MagicMock())
sys.modules.setdefault('psycopg2.extras', MagicMock())
sys.modules.setdefault('psycopg2.pool', MagicMock())
sys.modules.setdefault('redis', MagicMock())


class TestComputeLedIndex(unittest.TestCase):
    """Tests für die module-level Hilfsfunktion compute_led_index()"""

    def setUp(self):
        """Standard-Schwellwerte für alle Tests"""
        self.thresholds = {
            'p20': -0.5,
            'p40': -0.2,
            'p60': 0.1,
            'p80': 0.4
        }

    def test_import_erfolgreic(self):
        """compute_led_index kann direkt aus database importiert werden"""
        from database import compute_led_index
        self.assertTrue(callable(compute_led_index))

    def test_sehr_negativer_score_ergibt_index_0(self):
        """Score deutlich unter p20 → LED-Index 0"""
        from database import compute_led_index
        result = compute_led_index(-0.8, self.thresholds)
        self.assertEqual(result, 0)

    def test_negativer_score_ergibt_index_1(self):
        """Score zwischen p20 und p40 → LED-Index 1"""
        from database import compute_led_index
        result = compute_led_index(-0.3, self.thresholds)
        self.assertEqual(result, 1)

    def test_leicht_negativer_score_ergibt_index_2(self):
        """Score zwischen p40 und p60 → LED-Index 2"""
        from database import compute_led_index
        result = compute_led_index(-0.1, self.thresholds)
        self.assertEqual(result, 2)

    def test_positiver_score_ergibt_index_3(self):
        """Score zwischen p60 und p80 → LED-Index 3"""
        from database import compute_led_index
        result = compute_led_index(0.2, self.thresholds)
        self.assertEqual(result, 3)

    def test_sehr_positiver_score_ergibt_index_4(self):
        """Score über p80 → LED-Index 4"""
        from database import compute_led_index
        result = compute_led_index(0.6, self.thresholds)
        self.assertEqual(result, 4)

    def test_grenzwert_p20_nicht_kleiner(self):
        """Score genau auf p20 ist nicht mehr < p20 → Index 1"""
        from database import compute_led_index
        result = compute_led_index(-0.5, self.thresholds)
        self.assertEqual(result, 1, "Score == p20 soll Index 1 ergeben (nicht < p20)")

    def test_grenzwert_p40_nicht_kleiner(self):
        """Score genau auf p40 ist nicht mehr < p40 → Index 2"""
        from database import compute_led_index
        result = compute_led_index(-0.2, self.thresholds)
        self.assertEqual(result, 2, "Score == p40 soll Index 2 ergeben (nicht < p40)")

    def test_grenzwert_p60_nicht_kleiner(self):
        """Score genau auf p60 ist nicht mehr < p60 → Index 3"""
        from database import compute_led_index
        result = compute_led_index(0.1, self.thresholds)
        self.assertEqual(result, 3, "Score == p60 soll Index 3 ergeben (nicht < p60)")

    def test_grenzwert_p80_nicht_kleiner(self):
        """Score genau auf p80 ist nicht mehr < p80 → Index 4"""
        from database import compute_led_index
        result = compute_led_index(0.4, self.thresholds)
        self.assertEqual(result, 4, "Score == p80 soll Index 4 ergeben (nicht < p80)")

    def test_return_typ_ist_int(self):
        """Rückgabewert ist immer int"""
        from database import compute_led_index
        result = compute_led_index(0.0, self.thresholds)
        self.assertIsInstance(result, int)

    def test_fehlende_schwellwerte_verwenden_fallback_defaults(self):
        """Leeres thresholds-Dict fällt auf Standardwerte zurück"""
        from database import compute_led_index
        # Mit leerem Dict: Fallback-Defaults p20=-0.5, p40=-0.2, p60=0.1, p80=0.4
        result = compute_led_index(-0.8, {})
        self.assertEqual(result, 0)


class TestGetScorePercentilesMethods(unittest.TestCase):
    """Tests für Database.get_score_percentiles()"""

    def test_methode_existiert_in_database_klasse(self):
        """get_score_percentiles() ist eine Methode der Database-Klasse"""
        from database import Database
        self.assertTrue(hasattr(Database, 'get_score_percentiles'))

    def test_methode_ist_aufrufbar(self):
        """get_score_percentiles ist ein callable"""
        from database import Database
        self.assertTrue(callable(getattr(Database, 'get_score_percentiles', None)))

    def test_fallback_bei_zu_wenig_datenpunkten(self):
        """Gibt Fallback zurück wenn count < 3"""
        from database import Database

        db = Database.__new__(Database)

        # Mock-Cursor der count=2 zurückgibt
        mock_row = {
            'count': 2,
            'p20': None, 'p40': None, 'p60': None, 'p80': None,
            'median': None, 'min': None, 'max': None
        }

        mock_cursor = MagicMock()
        mock_cursor.fetchone.return_value = mock_row
        mock_cursor.__enter__ = MagicMock(return_value=mock_cursor)
        mock_cursor.__exit__ = MagicMock(return_value=False)

        with patch.object(db, 'get_cursor', return_value=mock_cursor):
            result = db.get_score_percentiles(days=7)

        self.assertTrue(result['fallback'])
        self.assertEqual(result['p20'], -0.5)
        self.assertEqual(result['p40'], -0.2)
        self.assertEqual(result['p60'], 0.1)
        self.assertEqual(result['p80'], 0.4)

    def test_fallback_bei_null_datenpunkten(self):
        """Gibt Fallback mit count=0 wenn keine Daten vorhanden"""
        from database import Database

        db = Database.__new__(Database)

        mock_row = {
            'count': 0,
            'p20': None, 'p40': None, 'p60': None, 'p80': None,
            'median': None, 'min': None, 'max': None
        }

        mock_cursor = MagicMock()
        mock_cursor.fetchone.return_value = mock_row
        mock_cursor.__enter__ = MagicMock(return_value=mock_cursor)
        mock_cursor.__exit__ = MagicMock(return_value=False)

        with patch.object(db, 'get_cursor', return_value=mock_cursor):
            result = db.get_score_percentiles(days=7)

        self.assertTrue(result['fallback'])
        self.assertEqual(result['count'], 0)

    def test_echte_werte_bei_ausreichend_datenpunkten(self):
        """Gibt echte Perzentile zurück wenn count >= 3"""
        from database import Database

        db = Database.__new__(Database)

        mock_row = {
            'count': 10,
            'p20': -0.35, 'p40': -0.1, 'p60': 0.15,
            'p80': 0.45, 'median': 0.05, 'min': -0.8, 'max': 0.9
        }

        mock_cursor = MagicMock()
        mock_cursor.fetchone.return_value = mock_row
        mock_cursor.__enter__ = MagicMock(return_value=mock_cursor)
        mock_cursor.__exit__ = MagicMock(return_value=False)

        with patch.object(db, 'get_cursor', return_value=mock_cursor):
            result = db.get_score_percentiles(days=7)

        self.assertFalse(result['fallback'])
        self.assertEqual(result['count'], 10)
        self.assertAlmostEqual(result['p20'], -0.35, places=4)
        self.assertAlmostEqual(result['p40'], -0.1, places=4)
        self.assertAlmostEqual(result['p60'], 0.15, places=4)
        self.assertAlmostEqual(result['p80'], 0.45, places=4)
        self.assertAlmostEqual(result['median'], 0.05, places=4)
        self.assertAlmostEqual(result['min'], -0.8, places=4)
        self.assertAlmostEqual(result['max'], 0.9, places=4)

    def test_rückgabe_enthält_alle_pflichtfelder(self):
        """Rückgabe-Dict enthält immer alle erwarteten Schlüssel"""
        from database import Database

        db = Database.__new__(Database)
        mock_row = {'count': 0}

        mock_cursor = MagicMock()
        mock_cursor.fetchone.return_value = mock_row
        mock_cursor.__enter__ = MagicMock(return_value=mock_cursor)
        mock_cursor.__exit__ = MagicMock(return_value=False)

        with patch.object(db, 'get_cursor', return_value=mock_cursor):
            result = db.get_score_percentiles()

        pflichtfelder = {'p20', 'p40', 'p60', 'p80', 'min', 'max', 'median', 'count', 'fallback'}
        self.assertEqual(set(result.keys()), pflichtfelder)

    def test_fallback_bei_datenbankfehler(self):
        """Gibt Fallback zurück wenn DB-Fehler auftritt (kein Exception-Raise)"""
        from database import Database

        db = Database.__new__(Database)

        with patch.object(db, 'get_cursor', side_effect=Exception("DB Verbindungsfehler")):
            result = db.get_score_percentiles(days=7)

        self.assertTrue(result['fallback'])

    def test_default_days_parameter_ist_7(self):
        """Standardwert für days ist 7"""
        import inspect
        from database import Database
        sig = inspect.signature(Database.get_score_percentiles)
        self.assertEqual(sig.parameters['days'].default, 7)


if __name__ == '__main__':
    unittest.main(verbosity=2)
