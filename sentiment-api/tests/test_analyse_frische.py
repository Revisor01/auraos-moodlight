# -*- coding: utf-8 -*-
"""Tests für die Frische-Prüfung der letzten Analyse im Health-Check."""

import os
import sys
import unittest
from datetime import datetime, timedelta, timezone

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from gesundheit import analyse_frische  # noqa: E402

JETZT = datetime(2026, 11, 22, 12, 0, tzinfo=timezone.utc)


class AnalyseFrischeTest(unittest.TestCase):
    def test_frische_analyse_ist_gesund(self):
        ergebnis = analyse_frische(JETZT - timedelta(minutes=20), 1800, JETZT)
        self.assertEqual(ergebnis["status"], "healthy")
        self.assertEqual(ergebnis["age_minutes"], 20)
        self.assertEqual(ergebnis["max_age_minutes"], 120)

    def test_veraltete_analyse_ist_ungesund(self):
        ergebnis = analyse_frische(JETZT - timedelta(minutes=121), 1800, JETZT)
        self.assertEqual(ergebnis["status"], "unhealthy")
        self.assertEqual(ergebnis["age_minutes"], 121)

    def test_grenze_folgt_dem_worker_intervall(self):
        # Sechs Stunden Takt: drei Intervalle = 18 h Toleranz.
        ergebnis = analyse_frische(JETZT - timedelta(hours=10), 6 * 3600, JETZT)
        self.assertEqual(ergebnis["status"], "healthy")
        self.assertEqual(ergebnis["max_age_minutes"], 1080)

    def test_ohne_analyse_nur_eingeschraenkt(self):
        ergebnis = analyse_frische(None, 1800, JETZT)
        self.assertEqual(ergebnis["status"], "degraded")
        self.assertIsNone(ergebnis["last_analysis"])

    def test_zeit_ohne_zone_gilt_als_utc(self):
        naiv = (JETZT - timedelta(minutes=5)).replace(tzinfo=None)
        ergebnis = analyse_frische(naiv, 1800, JETZT)
        self.assertEqual(ergebnis["status"], "healthy")
        self.assertEqual(ergebnis["age_minutes"], 5)
        self.assertEqual(ergebnis["last_analysis"], "2026-11-22T11:55:00+00:00")


if __name__ == "__main__":
    unittest.main()
