# -*- coding: utf-8 -*-
"""
Unit-Tests für das Login-Rate-Limiting in app.py (Review-Runde 2, B-HOCH-4).

app.py importiert Flask/psycopg2/redis/anthropic auf Modulebene, daher werden
diese wie in den bestehenden Tests (test_percentiles.py) mit MagicMock-Stubs
ersetzt, damit die Tests ohne installierte Pakete lokal lauffähig sind.
Die Rate-Limit-Hilfsfunktionen (_login_rate_limit_*) sind reine Python-Logik
ohne Flask-Request-Kontext und daher direkt testbar.
"""

import sys
import os
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

# Fremdpakete mocken, die in der lokalen Testumgebung nicht installiert sind
sys.modules.setdefault('psycopg2', MagicMock())
sys.modules.setdefault('psycopg2.extras', MagicMock())
sys.modules.setdefault('psycopg2.pool', MagicMock())
sys.modules.setdefault('redis', MagicMock())
sys.modules.setdefault('feedparser', MagicMock())
sys.modules.setdefault('requests', MagicMock())


def _install_flask_stub():
    """
    Minimaler Flask-Stub für den app.py-Modulimport. Nur die von app.py auf
    Modulebene benötigten Namen werden bereitgestellt (Flask-Klasse, jsonify,
    request, render_template, session, redirect, url_for).
    """
    flask_stub = MagicMock()

    class _FakeFlaskApp:
        def __init__(self, *a, **kw):
            self.secret_key = None
            self.permanent_session_lifetime = None
            self.config = {}

        def route(self, *a, **kw):
            def decorator(f):
                return f
            return decorator

        def run(self, *a, **kw):
            pass

    flask_stub.Flask = _FakeFlaskApp
    flask_stub.jsonify = MagicMock(side_effect=lambda *a, **kw: (a, kw))
    flask_stub.request = MagicMock()
    flask_stub.render_template = MagicMock(return_value="")
    flask_stub.session = MagicMock()
    flask_stub.redirect = MagicMock()
    flask_stub.url_for = MagicMock(return_value="/dashboard")

    sys.modules['flask'] = flask_stub

    werkzeug_security_stub = MagicMock()
    werkzeug_security_stub.generate_password_hash = MagicMock(side_effect=lambda pw: f"hash:{pw}")
    werkzeug_security_stub.check_password_hash = MagicMock(side_effect=lambda h, pw: h == f"hash:{pw}")
    sys.modules.setdefault('werkzeug', MagicMock())
    sys.modules['werkzeug.security'] = werkzeug_security_stub

    anthropic_stub = MagicMock()

    class _FakeAnthropic:
        def __init__(self, *a, **kw):
            pass

    anthropic_stub.Anthropic = _FakeAnthropic
    anthropic_stub.APIConnectionError = Exception
    anthropic_stub.RateLimitError = Exception
    anthropic_stub.APIStatusError = Exception
    sys.modules.setdefault('anthropic', anthropic_stub)


class TestLoginRateLimit(unittest.TestCase):
    """Tests für die In-Memory Login-Rate-Limit-Hilfsfunktionen in app.py"""

    @classmethod
    def setUpClass(cls):
        _install_flask_stub()
        os.environ['SECRET_KEY'] = 'test-secret-key-for-unit-tests'

        # database.get_database() wird beim Modulimport von app.py aufgerufen
        # (load_settings_from_db) — mocken um DB-Zugriff zu vermeiden
        mock_db = MagicMock()
        mock_db.get_setting.return_value = None
        mock_db.get_active_feeds.return_value = []

        with patch('database.get_database', return_value=mock_db):
            import app as app_module
        cls.app_module = app_module

    def setUp(self):
        # Rate-Limit-State zwischen Tests zurücksetzen (globales Dict)
        self.app_module._login_attempts.clear()

    def test_erlaubt_bei_leerer_historie(self):
        allowed, retry_after = self.app_module._login_rate_limit_check('1.2.3.4')
        self.assertTrue(allowed)
        self.assertEqual(retry_after, 0)

    def test_sperrt_nach_fuenf_fehlversuchen(self):
        ip = '10.0.0.1'
        for _ in range(5):
            self.app_module._login_rate_limit_record_failure(ip)

        allowed, retry_after = self.app_module._login_rate_limit_check(ip)
        self.assertFalse(allowed)
        self.assertGreater(retry_after, 0)

    def test_erlaubt_noch_bei_vier_fehlversuchen(self):
        ip = '10.0.0.2'
        for _ in range(4):
            self.app_module._login_rate_limit_record_failure(ip)

        allowed, _ = self.app_module._login_rate_limit_check(ip)
        self.assertTrue(allowed, "Nach nur 4 Fehlversuchen darf noch kein Lockout aktiv sein")

    def test_reset_hebt_sperre_auf(self):
        ip = '10.0.0.3'
        for _ in range(5):
            self.app_module._login_rate_limit_record_failure(ip)
        self.assertFalse(self.app_module._login_rate_limit_check(ip)[0])

        self.app_module._login_rate_limit_reset(ip)
        allowed, _ = self.app_module._login_rate_limit_check(ip)
        self.assertTrue(allowed, "Nach Reset (erfolgreicher Login) darf keine Sperre mehr aktiv sein")

    def test_verschiedene_ips_unabhaengig_voneinander(self):
        ip_a = '10.0.0.4'
        ip_b = '10.0.0.5'
        for _ in range(5):
            self.app_module._login_rate_limit_record_failure(ip_a)

        self.assertFalse(self.app_module._login_rate_limit_check(ip_a)[0])
        self.assertTrue(self.app_module._login_rate_limit_check(ip_b)[0], "IP B darf von der Sperre der IP A nicht betroffen sein")

    def test_speicher_wird_auf_max_ips_begrenzt(self):
        """Nach Ueberschreiten von _LOGIN_MAX_TRACKED_IPS werden aelteste Eintraege entfernt."""
        max_ips = self.app_module._LOGIN_MAX_TRACKED_IPS
        for i in range(max_ips + 50):
            self.app_module._login_rate_limit_record_failure(f'192.0.2.{i % 250}.{i}')

        self.assertLessEqual(len(self.app_module._login_attempts), max_ips)


if __name__ == '__main__':
    unittest.main(verbosity=2)
