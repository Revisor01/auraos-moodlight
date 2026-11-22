# -*- coding: utf-8 -*-
"""Unit-Tests: /api/news löst Anthropic-Aufrufe nur mit Admin-Sitzung aus."""

import sys
import os
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

sys.modules.setdefault('psycopg2', MagicMock())
sys.modules.setdefault('psycopg2.extras', MagicMock())
sys.modules.setdefault('psycopg2.pool', MagicMock())
sys.modules.setdefault('redis', MagicMock())
sys.modules.setdefault('feedparser', MagicMock())
sys.modules.setdefault('requests', MagicMock())

sys.path.insert(0, os.path.dirname(__file__))
from test_login_rate_limit import _install_flask_stub  # noqa: E402


class TestNewsAuth(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        if 'app' not in sys.modules:
            _install_flask_stub()
        os.environ['SECRET_KEY'] = 'test-secret-key-for-unit-tests'
        mock_db = MagicMock()
        mock_db.get_setting.return_value = None
        mock_db.get_active_feeds.return_value = []
        with patch('database.get_database', return_value=mock_db):
            import app as app_module
        cls.app_module = app_module
        cls.extensions = sys.modules['moodlight_extensions']

    def _aufruf(self, angemeldet):
        sitzung = MagicMock()
        sitzung.get = MagicMock(side_effect=lambda key, default=None: True if angemeldet and key == 'authenticated' else default)
        # Ohne Anthropic-Client endet ein durchgelassener Aufruf mit 503, bevor etwas abgerufen wird.
        with patch.object(self.extensions, 'session', sitzung), \
                patch.object(self.extensions, 'jsonify', MagicMock(return_value={})), \
                patch.object(self.app_module, 'anthropic_client', None), \
                patch.object(self.app_module, 'get_headlines_per_source') as headlines:
            ergebnis = self.app_module.get_news()
        return ergebnis, headlines

    def test_verboten_ohne_sitzung_401_und_kein_abruf(self):
        ergebnis, headlines = self._aufruf(angemeldet=False)
        self.assertEqual(ergebnis[1], 401)
        headlines.assert_not_called()

    def test_erlaubt_mit_sitzung_kommt_an_der_pruefung_vorbei(self):
        ergebnis, _ = self._aufruf(angemeldet=True)
        self.assertEqual(ergebnis[1], 503)


if __name__ == '__main__':
    unittest.main(verbosity=2)
