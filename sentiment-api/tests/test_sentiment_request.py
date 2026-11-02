# -*- coding: utf-8 -*-
"""Unit-Tests für die Anfrage an die Anthropic-API in analyze_sentiment_claude()."""

import sys
import os
import unittest
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

sys.path.insert(0, os.path.dirname(__file__))
from test_headlines_cap import _install_flask_stub  # noqa: E402


class _FakeMessages:
    def __init__(self, content):
        self.content = content
        self.kwargs = None

    def create(self, **kwargs):
        self.kwargs = kwargs
        return SimpleNamespace(content=self.content)


def _text(t):
    return SimpleNamespace(type="text", text=t)


class TestSentimentRequest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        _install_flask_stub()
        os.environ['SECRET_KEY'] = 'test-secret-key-for-unit-tests'
        mock_db = MagicMock()
        mock_db.get_setting.return_value = None
        mock_db.get_active_feeds.return_value = []
        with patch('database.get_database', return_value=mock_db):
            import app as app_module
        cls.app_module = app_module

    def _run(self, content, headlines):
        messages = _FakeMessages(content)
        with patch.object(self.app_module, 'anthropic_client', SimpleNamespace(messages=messages)):
            result = self.app_module.analyze_sentiment_claude(headlines)
        return result, messages.kwargs

    def test_anfrage_ohne_sampling_parameter_mit_aktuellem_modell(self):
        _, kwargs = self._run([_text("1: 0.5")], ["Friedensverhandlungen beginnen"])
        self.assertEqual(kwargs['model'], 'claude-haiku-5-5')
        self.assertEqual(kwargs['output_config'], {"effort": "low"})
        for name in ('temperature', 'top_p', 'top_k'):
            self.assertNotIn(name, kwargs)
        self.assertEqual(kwargs['max_tokens'], 2048)

    def test_max_tokens_waechst_mit_der_zahl_der_schlagzeilen(self):
        _, kwargs = self._run([_text("")], [f"Schlagzeile {i}" for i in range(60)])
        self.assertEqual(kwargs['max_tokens'], 60 * 25 + 1024)

    def test_denkblock_vor_dem_text_wird_uebersprungen(self):
        content = [SimpleNamespace(type="thinking", thinking="", signature="sig"), _text("1: -0.4\n2: 0.7")]
        result, _ = self._run(content, ["Krise", "Durchbruch"])
        self.assertEqual(result, [-0.4, 0.7])

    def test_unvollstaendige_antwort_wird_verworfen(self):
        result, _ = self._run([_text("1: -0.4")], ["Krise", "Durchbruch"])
        self.assertIsNone(result)


if __name__ == '__main__':
    unittest.main()
