# -*- coding: utf-8 -*-
"""Unit-Tests: die Login-Sperre zählt je Client aus X-Real-IP, nicht je Proxy-Adresse."""

import sys
import os
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
sys.path.insert(0, os.path.dirname(__file__))

sys.modules.setdefault('psycopg2', MagicMock())
sys.modules.setdefault('psycopg2.extras', MagicMock())
sys.modules.setdefault('psycopg2.pool', MagicMock())
sys.modules.setdefault('redis', MagicMock())
sys.modules.setdefault('feedparser', MagicMock())
sys.modules.setdefault('requests', MagicMock())

from test_login_rate_limit import _install_flask_stub  # noqa: E402

PROXY = '192.168.128.1'


class TestLoginClientIp(unittest.TestCase):

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

    def setUp(self):
        self.app_module._login_attempts.clear()

    def _anfrage(self, real_ip=None):
        anfrage = MagicMock()
        anfrage.method = 'POST'
        anfrage.remote_addr = PROXY
        anfrage.headers = {} if real_ip is None else {'X-Real-IP': real_ip}
        anfrage.form = {'password': 'falsch'}
        anfrage.args = {}
        return anfrage

    def _login(self, real_ip):
        with patch.object(self.app_module, 'request', self._anfrage(real_ip)), \
                patch.object(self.app_module, 'render_template', MagicMock(return_value='')), \
                patch.object(self.app_module, '_admin_password_hash', None):
            return self.app_module.login_page()[1]

    def test_client_ip_aus_x_real_ip(self):
        with patch.object(self.app_module, 'request', self._anfrage('203.0.113.7')):
            self.assertEqual(self.app_module._client_ip(), '203.0.113.7')

    def test_client_ip_ohne_oder_mit_ungueltigem_header_ist_remote_addr(self):
        for wert in (None, '', 'kein-ip, 1.2.3.4'):
            with patch.object(self.app_module, 'request', self._anfrage(wert)):
                self.assertEqual(self.app_module._client_ip(), PROXY)

    def test_verboten_fuenf_fehlversuche_sperren_diesen_client(self):
        status = [self._login('203.0.113.7') for _ in range(6)]
        self.assertEqual(status, [401, 401, 401, 401, 401, 429])

    def test_erlaubt_anderer_client_hinter_demselben_proxy_bleibt_frei(self):
        for _ in range(5):
            self._login('203.0.113.7')
        self.assertEqual(self._login('198.51.100.9'), 401)


if __name__ == '__main__':
    unittest.main(verbosity=2)
