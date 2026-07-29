# -*- coding: utf-8 -*-
"""
Unit-Tests für die Worker-Lebensdauer-Fixes (Review-Runde 2, B-MITTEL).

Deckt die drei bestätigten Bugs in SentimentUpdateWorker._worker_loop() ab:
1. Thread stirbt nicht dauerhaft, wenn reconfigure() während der ersten 10s
   Startverzögerung aufgerufen wird (vorher: jedes Event.set() beendete den Thread).
2. Ein Wake durch reconfigure() löst KEINE sofortige Analyse aus (Vertrag von
   reconfigure() ist reines Intervall-Update, nicht "jetzt analysieren").
3. Die Deadline wird nach reconfigure() korrekt gegen das neue interval_seconds
   neu berechnet.
"""

import sys
import os
import time
import unittest
from unittest.mock import MagicMock, patch

# Sentiment-API Verzeichnis in Suchpfad aufnehmen
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

# psycopg2 und redis mocken damit Tests lokal ohne installierte Pakete laufen
sys.modules.setdefault('psycopg2', MagicMock())
sys.modules.setdefault('psycopg2.extras', MagicMock())
sys.modules.setdefault('psycopg2.pool', MagicMock())
sys.modules.setdefault('redis', MagicMock())


class TestWorkerLifecycle(unittest.TestCase):
    """Tests für SentimentUpdateWorker._worker_loop() Thread-Lebensdauer"""

    def _make_worker(self):
        from background_worker import SentimentUpdateWorker

        mock_app = MagicMock()
        mock_app.app_context.return_value.__enter__ = MagicMock(return_value=None)
        mock_app.app_context.return_value.__exit__ = MagicMock(return_value=False)

        analyze_function = MagicMock(return_value={
            "results": [], "total_sentiment": 0.0,
            "statistics": {"analyzed_count": 0, "sentiment_distribution": {}}
        })
        # Kurzes Intervall für schnelle Tests
        worker = SentimentUpdateWorker(app=mock_app, analyze_function=analyze_function, interval_seconds=3600)
        return worker

    def test_reconfigure_waehrend_startverzoegerung_beendet_thread_nicht(self):
        """
        Bug 1: reconfigure() innerhalb der ersten 10s darf den Worker-Thread
        NICHT dauerhaft beenden — nur stop() darf das.
        """
        worker = self._make_worker()

        with patch.object(worker, '_perform_update') as mock_perform:
            worker.start()
            # Kurz warten, damit der Thread die Startverzoegerungs-Schleife betritt
            time.sleep(0.2)

            # reconfigure() feuert das Wake-Event — simuliert Bug-Trigger
            worker.reconfigure(interval_seconds=7200)

            # Thread muss weiterhin laufen (nicht durch das Event beendet)
            time.sleep(0.2)
            self.assertTrue(worker.thread.is_alive(), "Worker-Thread wurde faelschlich durch reconfigure() beendet")

            worker.stop()
            self.assertFalse(worker.thread.is_alive())

    def test_reconfigure_loest_keine_sofortige_analyse_aus(self):
        """
        Bug 3: reconfigure() darf NICHT sofort _perform_update() ausloesen —
        nur das Intervall aendert sich, trigger() bleibt der Weg für Sofort-Analysen.
        """
        worker = self._make_worker()

        with patch.object(worker, '_perform_update') as mock_perform:
            worker.start()
            time.sleep(0.2)  # Sicherstellen, dass der Thread in der Wartephase ist

            worker.reconfigure(interval_seconds=7200)
            time.sleep(0.3)  # Genug Zeit für einen (falschen) Sofort-Trigger

            mock_perform.assert_not_called()

            worker.stop()

    def test_reconfigure_aendert_intervall_sofort_wirksam(self):
        """
        Deadline wird nach reconfigure() gegen das NEUE interval_seconds neu
        berechnet — mit einem sehr kurzen neuen Intervall muss die naechste
        Analyse zeitnah erfolgen (nicht erst nach dem urspruenglichen langen Intervall).
        """
        worker = self._make_worker()

        with patch.object(worker, '_perform_update') as mock_perform:
            worker.start()
            time.sleep(0.2)

            # Intervall auf sehr kurz aendern — Analyse sollte danach zeitnah erfolgen
            worker.reconfigure(interval_seconds=1)

            # Warten bis (deutlich) laenger als das neue Intervall, aber kuerzer
            # als das urspruengliche (3600s) — beweist, dass die Deadline neu
            # berechnet wurde und nicht das alte lange Intervall gilt
            time.sleep(2.0)

            self.assertTrue(mock_perform.called, "Analyse wurde nicht innerhalb des neuen kurzen Intervalls ausgeloest")

            worker.stop()

    def test_stop_beendet_thread_waehrend_startverzoegerung(self):
        """Gegenprobe: stop() MUSS den Thread auch waehrend der Startverzoegerung beenden."""
        worker = self._make_worker()

        with patch.object(worker, '_perform_update'):
            worker.start()
            time.sleep(0.1)
            worker.stop()
            self.assertFalse(worker.thread.is_alive())


if __name__ == '__main__':
    unittest.main(verbosity=2)
