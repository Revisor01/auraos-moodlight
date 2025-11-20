# -*- coding: utf-8 -*-
"""
Background Worker für automatische Sentiment-Updates
Läuft alle 30 Minuten und speichert Ergebnisse in PostgreSQL
"""

import logging
import time
from threading import Thread
from datetime import datetime
from database import get_database, get_cache

logger = logging.getLogger(__name__)


class SentimentUpdateWorker:
    """Background Worker für periodische Sentiment-Updates"""

    def __init__(self, app, analyze_function, interval_seconds=1800):
        """
        Initialisiere Worker

        Args:
            app: Flask App-Instanz (für App-Context)
            analyze_function: Funktion die Headlines analysiert
            interval_seconds: Update-Intervall in Sekunden (default: 30 Min)
        """
        self.app = app
        self.analyze_function = analyze_function
        self.interval_seconds = interval_seconds
        self.running = False
        self.thread = None

    def start(self):
        """Starte Background Worker"""
        if self.running:
            logger.warning("Background Worker läuft bereits")
            return

        self.running = True
        self.thread = Thread(target=self._worker_loop, daemon=True)
        self.thread.start()
        logger.info(f"Background Worker gestartet (Intervall: {self.interval_seconds}s)")

    def stop(self):
        """Stoppe Background Worker"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=5)
        logger.info("Background Worker gestoppt")

    def _worker_loop(self):
        """Haupt-Worker-Loop"""
        # Erste Ausführung nach 10 Sekunden (damit App Zeit hat hochzufahren)
        time.sleep(10)

        while self.running:
            try:
                with self.app.app_context():
                    self._perform_update()
            except Exception as e:
                logger.error(f"Fehler im Background Worker: {e}", exc_info=True)

            # Warten bis zum nächsten Update
            logger.info(f"Nächstes Update in {self.interval_seconds} Sekunden...")
            time.sleep(self.interval_seconds)

    def _perform_update(self):
        """Führe Sentiment-Update durch"""
        start_time = time.time()
        logger.info("=== Background Worker: Starte Sentiment-Update ===")

        try:
            # 1. Headlines von Feeds abrufen
            headlines = self._fetch_headlines()

            if not headlines:
                logger.warning("Keine Headlines gefunden - Update abgebrochen")
                return

            logger.info(f"Headlines gesammelt: {len(headlines)}")

            # 2. Sentiment analysieren (nutzt die bestehende analyze_function)
            analysis_result = self.analyze_function(headlines)

            if not analysis_result or 'total_sentiment' not in analysis_result:
                logger.error("Ungültige Analyse-Ergebnisse")
                return

            sentiment_score = analysis_result['total_sentiment']
            stats = analysis_result.get('statistics', {})
            headlines_analyzed = stats.get('analyzed_count', len(headlines))

            logger.info(f"Sentiment analysiert: {sentiment_score:.3f} ({headlines_analyzed} Headlines)")

            # 3. In Datenbank speichern
            db = get_database()
            category = self._get_category(sentiment_score)

            response_time_ms = int((time.time() - start_time) * 1000)

            db.save_sentiment(
                sentiment_score=sentiment_score,
                category=category,
                headlines_analyzed=headlines_analyzed,
                source_count=12,  # Anzahl RSS-Feeds
                api_response_time_ms=response_time_ms,
                metadata={
                    'sentiment_distribution': stats.get('sentiment_distribution', {}),
                    'worker': 'background',
                    'timestamp': datetime.now().isoformat()
                }
            )

            logger.info(f"Sentiment in DB gespeichert: {sentiment_score:.3f}")

            # 4. Redis-Cache invalidieren
            cache = get_cache()
            cache.delete('moodlight:current')
            logger.info("Cache invalidiert - nächster Request holt frische Daten")

            # 5. Statistik loggen
            elapsed = time.time() - start_time
            logger.info(f"=== Update abgeschlossen in {elapsed:.2f}s ===")

        except Exception as e:
            logger.error(f"Fehler beim Sentiment-Update: {e}", exc_info=True)

    def _fetch_headlines(self):
        """
        Hole Headlines von RSS-Feeds
        (Nutzt die gleiche Logik wie in app.py)
        """
        import feedparser
        import socket

        rss_feeds = {
            "Zeit": "https://newsfeed.zeit.de/index",
            "Tagesschau": "https://www.tagesschau.de/xml/rss2",
            "Sueddeutsche": "https://rss.sueddeutsche.de/rss/Alles",
            "FAZ": "https://www.faz.net/rss/aktuell/",
            "Die Welt": "https://www.welt.de/feeds/latest.rss",
            "Handelsblatt": "https://www.handelsblatt.com/contentexport/feed/schlagzeilen",
            "n-tv": "https://www.n-tv.de/rss",
            "Focus": "https://rss.focus.de/fol/XML/rss_folnews.xml",
            "Stern": "https://www.stern.de/feed/standard/alle-nachrichten/",
            "Telekom": "https://www.t-online.de/feed.rss",
            "TAZ": "https://taz.de/!p4608;rss/",
            "Deutschlandfunk": "https://www.deutschlandfunk.de/nachrichten-100.rss"
        }

        headlines = []
        processed_links = set()
        socket_timeout = 10.0
        num_headlines_per_source = 1  # Für Background-Worker nur 1 Headline/Quelle

        # Temporäres Timeout setzen
        original_timeout = socket.getdefaulttimeout()
        timeout_changed = False

        if original_timeout is None or original_timeout != socket_timeout:
            try:
                socket.setdefaulttimeout(socket_timeout)
                timeout_changed = True
            except Exception as e:
                logger.warning(f"Konnte Socket-Timeout nicht setzen: {e}")

        # Feeds abrufen
        for source, url in rss_feeds.items():
            try:
                feed = feedparser.parse(url, request_headers={'User-Agent': 'WorldMoodAnalyzer/2.0'})

                if feed.bozo and isinstance(feed.bozo_exception, Exception):
                    if isinstance(feed.bozo_exception, socket.timeout):
                        logger.warning(f"Timeout bei {source}")
                    else:
                        logger.warning(f"Feed-Fehler bei {source}: {feed.bozo_exception}")
                    continue

                headlines_from_source = 0
                if feed.entries:
                    for entry in feed.entries:
                        if headlines_from_source >= num_headlines_per_source:
                            break

                        link = getattr(entry, 'link', None)
                        title = getattr(entry, 'title', None)

                        if title and title.strip():
                            headline_text = title.strip()
                            unique_key = link if link else headline_text

                            if unique_key not in processed_links:
                                headlines.append({
                                    "headline": headline_text,
                                    "source": source,
                                    "link": link
                                })
                                processed_links.add(unique_key)
                                headlines_from_source += 1

            except socket.timeout:
                logger.warning(f"Timeout bei {source}")
            except Exception as e:
                logger.error(f"Fehler bei {source}: {e}")

        # Timeout zurücksetzen
        if timeout_changed and original_timeout is not None:
            try:
                socket.setdefaulttimeout(original_timeout)
            except Exception as e:
                logger.warning(f"Konnte Timeout nicht zurücksetzen: {e}")

        return headlines

    def _get_category(self, sentiment_score):
        """Bestimme Kategorie aus Sentiment-Score"""
        if sentiment_score >= 0.30:
            return "sehr positiv"
        elif sentiment_score >= 0.10:
            return "positiv"
        elif sentiment_score >= -0.20:
            return "neutral"
        elif sentiment_score >= -0.50:
            return "negativ"
        else:
            return "sehr negativ"


# Singleton-Instanz
_worker = None


def start_background_worker(app, analyze_function, interval_seconds=1800):
    """
    Starte den Background Worker

    Args:
        app: Flask App-Instanz
        analyze_function: Funktion für Sentiment-Analyse
        interval_seconds: Update-Intervall (default: 30 Min)
    """
    global _worker

    if _worker is not None:
        logger.warning("Worker wurde bereits gestartet")
        return _worker

    _worker = SentimentUpdateWorker(app, analyze_function, interval_seconds)
    _worker.start()

    return _worker


def stop_background_worker():
    """Stoppe den Background Worker"""
    global _worker

    if _worker is not None:
        _worker.stop()
        _worker = None
