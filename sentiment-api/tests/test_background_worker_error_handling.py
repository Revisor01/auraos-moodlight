# -*- coding: utf-8 -*-
"""
Unit-Tests für Background-Worker-Fehlerbehandlung (B2)

Prüft, dass ein Anthropic-API-Fehler (analyze_function gibt None zurück statt
eines Ergebnis-Dicts) NICHT als Sentiment-Wert 0.0 in der Datenbank landet —
das war die Ursache der 30-Tage-Nullserie in der Produktions-DB.
"""

import sys
import os
import unittest
from unittest.mock import MagicMock, patch

# Sentiment-API Verzeichnis in Suchpfad aufnehmen
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

# psycopg2 und redis mocken damit Tests lokal ohne installierte Pakete laufen
sys.modules.setdefault('psycopg2', MagicMock())
sys.modules.setdefault('psycopg2.extras', MagicMock())
sys.modules.setdefault('psycopg2.pool', MagicMock())
sys.modules.setdefault('redis', MagicMock())


class TestPerformUpdateSkipsOnAnalysisFailure(unittest.TestCase):
    """Tests für SentimentUpdateWorker._perform_update() / _do_perform_update()"""

    def _make_worker(self, analyze_return_value):
        """Erzeugt einen Worker mit gemockter analyze_function und Flask-App."""
        from background_worker import SentimentUpdateWorker

        mock_app = MagicMock()
        mock_app.app_context.return_value.__enter__ = MagicMock(return_value=None)
        mock_app.app_context.return_value.__exit__ = MagicMock(return_value=False)

        analyze_function = MagicMock(return_value=analyze_return_value)
        worker = SentimentUpdateWorker(app=mock_app, analyze_function=analyze_function)
        return worker

    def test_none_ergebnis_fuehrt_zu_keinem_db_write(self):
        """analyze_function() gibt None zurück (Anthropic-Fehler) → save_sentiment() wird NICHT aufgerufen (B2)."""
        worker = self._make_worker(analyze_return_value=None)

        mock_db = MagicMock()
        mock_db.get_active_feeds.return_value = [{'id': 1, 'name': 'Test-Feed', 'url': 'https://example.test/rss'}]

        mock_feed_row = [{'headline': 'Testschlagzeile', 'source': 'Test-Feed', 'link': None, 'feed_id': 1}]

        with patch.object(worker, '_fetch_headlines', return_value=mock_feed_row), \
             patch('background_worker.get_database', return_value=mock_db), \
             patch('background_worker.get_cache', return_value=MagicMock()):
            worker._do_perform_update()

        mock_db.save_sentiment.assert_not_called()

    def test_fehlendes_total_sentiment_feld_fuehrt_zu_keinem_db_write(self):
        """analyze_function() liefert Dict ohne 'total_sentiment' → ebenfalls kein DB-Write."""
        worker = self._make_worker(analyze_return_value={"results": []})

        mock_db = MagicMock()
        mock_db.get_active_feeds.return_value = [{'id': 1, 'name': 'Test-Feed', 'url': 'https://example.test/rss'}]
        mock_feed_row = [{'headline': 'Testschlagzeile', 'source': 'Test-Feed', 'link': None, 'feed_id': 1}]

        with patch.object(worker, '_fetch_headlines', return_value=mock_feed_row), \
             patch('background_worker.get_database', return_value=mock_db), \
             patch('background_worker.get_cache', return_value=MagicMock()):
            worker._do_perform_update()

        mock_db.save_sentiment.assert_not_called()

    def test_erfolgreiches_ergebnis_fuehrt_zu_db_write(self):
        """Gegenprobe: gültiges Analyse-Ergebnis führt weiterhin zu save_sentiment()-Aufruf."""
        worker = self._make_worker(analyze_return_value={
            "results": [{"headline": "x", "source": "Test-Feed", "feed_id": 1, "sentiment": 0.3, "strength": "positiv"}],
            "total_sentiment": 0.3,
            "statistics": {"analyzed_count": 1, "sentiment_distribution": {"positiv": 1}}
        })

        mock_db = MagicMock()
        mock_db.get_active_feeds.return_value = [{'id': 1, 'name': 'Test-Feed', 'url': 'https://example.test/rss'}]
        mock_db.save_sentiment.return_value = 42
        mock_feed_row = [{'headline': 'Testschlagzeile', 'source': 'Test-Feed', 'link': None, 'feed_id': 1}]

        with patch.object(worker, '_fetch_headlines', return_value=mock_feed_row), \
             patch('background_worker.get_database', return_value=mock_db), \
             patch('background_worker.get_cache', return_value=MagicMock()):
            worker._do_perform_update()

        mock_db.save_sentiment.assert_called_once()
        called_kwargs = mock_db.save_sentiment.call_args.kwargs
        self.assertEqual(called_kwargs['sentiment_score'], 0.3)


if __name__ == '__main__':
    unittest.main(verbosity=2)
