# -*- coding: utf-8 -*-
"""
Unit-Tests für den headlines_per_source-Kostenvektor-Cap in app.py
(Review-Runde 2, B-HOCH-3).

Verhindert, dass ein Request beliebig viele Headlines pro RSS-Quelle anfordert
und dadurch die Anthropic-API-Kosten unbegrenzt in die Hoehe treibt.
"""

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


def _install_flask_stub():
    """Siehe test_login_rate_limit.py für Details zum Stub-Ansatz."""
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
    # request wird in get_headlines_per_source() für request.args.get() gebraucht —
    # jeder Test setzt flask_stub.request.args gezielt neu
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

    return flask_stub


class TestHeadlinesPerSourceCap(unittest.TestCase):
    """Tests für get_headlines_per_source() Kostenvektor-Cap"""

    @classmethod
    def setUpClass(cls):
        cls.flask_stub = _install_flask_stub()
        os.environ['SECRET_KEY'] = 'test-secret-key-for-unit-tests'

        mock_db = MagicMock()
        mock_db.get_setting.return_value = None
        mock_db.get_active_feeds.return_value = []

        with patch('database.get_database', return_value=mock_db):
            import app as app_module
        cls.app_module = app_module

    def _set_url_param(self, value):
        """Simuliert request.args.get('headlines_per_source', type=int) -> value."""
        def fake_get(key, default=None, type=None):
            if key == 'headlines_per_source':
                return value
            return default
        self.app_module.request.args.get = MagicMock(side_effect=fake_get)

    def test_wert_unter_cap_wird_unveraendert_uebernommen(self):
        self._set_url_param(5)
        result = self.app_module.get_headlines_per_source(route_default=2)
        self.assertEqual(result, 5)

    def test_wert_ueber_cap_wird_auf_zehn_gedeckelt(self):
        self._set_url_param(500)
        result = self.app_module.get_headlines_per_source(route_default=2)
        self.assertEqual(result, 10)

    def test_wert_genau_am_cap_bleibt_zehn(self):
        self._set_url_param(10)
        result = self.app_module.get_headlines_per_source(route_default=2)
        self.assertEqual(result, 10)

    def test_kein_url_parameter_nutzt_env_fallback(self):
        self._set_url_param(None)
        # DEFAULT_HEADLINES_FROM_ENV ist beim Modulimport gesetzt (aus Env oder Default)
        result = self.app_module.get_headlines_per_source(route_default=2)
        self.assertEqual(result, self.app_module.DEFAULT_HEADLINES_FROM_ENV)


if __name__ == '__main__':
    unittest.main(verbosity=2)
