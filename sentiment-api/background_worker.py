# -*- coding: utf-8 -*-
"""Background Worker, der alle 30 Minuten die Sentiment-Analyse ausführt und speichert."""

import logging
import time
from threading import Thread, Event, Lock
from datetime import datetime
from database import get_database, get_cache
from shared_config import get_sentiment_category
from shared_config import CACHE_KEY_CURRENT, CACHE_KEY_CURRENT_LEGACY

logger = logging.getLogger(__name__)


def _invalidate_moodlight_cache():
    """Invalidiert alle gecachten Moodlight-Endpunkte nach einem neuen Sentiment-Update."""
    cache = get_cache()
    cache.delete(CACHE_KEY_CURRENT)
    cache.delete(CACHE_KEY_CURRENT_LEGACY)
    cache.delete_pattern("moodlight:history:*")
    cache.delete_pattern("moodlight:trend")
    cache.delete_pattern("moodlight:stats")
    cache.delete_pattern("moodlight:feeds:trends:*")


class SentimentUpdateWorker:
    """Background Worker für periodische Sentiment-Updates"""

    def __init__(self, app, analyze_function, interval_seconds=1800):
        """Initialisiere Worker."""
        self.app = app
        self.analyze_function = analyze_function
        self.interval_seconds = interval_seconds
        self.headlines_per_source = 1  # dynamisch via reconfigure() änderbar
        self.running = False
        self.thread = None
        # Event statt sleep(), damit stop() und reconfigure() sofort wirken.
        self._wake_event = Event()
        # Verhindert parallele Analysen von Dashboard-Trigger und Worker (doppelte Kosten).
        self._update_lock = Lock()

    def start(self):
        """Starte Background Worker"""
        if self.running:
            logger.warning("Background Worker läuft bereits")
            return

        self.running = True
        self._wake_event.clear()
        self.thread = Thread(target=self._worker_loop, daemon=True)
        self.thread.start()
        logger.info(f"Background Worker gestartet (Intervall: {self.interval_seconds}s)")

    def stop(self):
        """Stoppe Background Worker"""
        self.running = False
        self._wake_event.set()  # weckt _worker_loop() sofort aus dem Wartezustand
        if self.thread:
            self.thread.join(timeout=5)
        logger.info("Background Worker gestoppt")

    def reconfigure(self, interval_seconds: int = None, headlines_per_source: int = None):
        """Rekonfiguriere Worker-Parameter zur Laufzeit (ohne Neustart)."""
        if interval_seconds is not None and interval_seconds > 0:
            old_interval = self.interval_seconds
            self.interval_seconds = interval_seconds
            logger.info(f"Worker-Intervall geändert: {old_interval}s → {interval_seconds}s")
            # Sofort aufwecken, damit das neue Intervall ab jetzt gilt.
            self._wake_event.set()

        if headlines_per_source is not None and headlines_per_source > 0:
            old_headlines = self.headlines_per_source
            self.headlines_per_source = headlines_per_source
            logger.info(f"Worker-Headlines geändert: {old_headlines} → {headlines_per_source}")

    def trigger(self) -> dict:
        """Führe sofortige Sentiment-Analyse durch (für manuellen Trigger vom Dashboard)."""
        if not self._update_lock.acquire(blocking=False):
            raise RuntimeError("Es läuft bereits eine Sentiment-Analyse — bitte warten")

        try:
            return self._do_trigger()
        finally:
            self._update_lock.release()

    def _do_trigger(self) -> dict:
        """Eigentliche Trigger-Logik, wird nur unter _update_lock aufgerufen."""
        start = time.time()
        logger.info("=== Manueller Trigger: Starte Sentiment-Update ===")

        headlines = self._fetch_headlines()
        if not headlines:
            raise RuntimeError("Keine Headlines von den Feeds erhalten")

        feed_count = len(get_database().get_active_feeds())

        # None = API-Fehler oder Teilparse (siehe analyze_sentiment_claude)
        analysis_result = self.analyze_function(headlines)
        if analysis_result is None or 'total_sentiment' not in analysis_result:
            raise RuntimeError("Analyse lieferte kein verwertbares Ergebnis (Anthropic API-Fehler oder Teilparse)")

        sentiment_score = analysis_result['total_sentiment']
        stats = analysis_result.get('statistics', {})
        headlines_analyzed = stats.get('analyzed_count', len(headlines))

        db = get_database()
        category = get_sentiment_category(sentiment_score)
        response_time_ms = int((time.time() - start) * 1000)

        sentiment_history_id = db.save_sentiment(
            sentiment_score=sentiment_score,
            category=category,
            headlines_analyzed=headlines_analyzed,
            source_count=feed_count,
            api_response_time_ms=response_time_ms,
            metadata={
                'sentiment_distribution': stats.get('sentiment_distribution', {}),
                'worker': 'manual_trigger',
                'timestamp': datetime.now().isoformat()
            }
        )

        # Headlines persistieren (Fehler bricht nicht ab)
        try:
            headline_results = analysis_result.get('results', [])
            if headline_results:
                db.save_headlines(sentiment_history_id=sentiment_history_id, results=headline_results)
        except Exception as e:
            logger.error(f"Fehler beim Persistieren der Headlines (manueller Trigger): {e}")

        # Alle gecachten Endpunkte invalidieren
        _invalidate_moodlight_cache()

        elapsed = time.time() - start
        logger.info(f"=== Manueller Trigger abgeschlossen in {elapsed:.2f}s ===")

        return {
            'sentiment_score': sentiment_score,
            'category': category,
            'headlines_analyzed': headlines_analyzed,
            'source_count': feed_count,
            'duration_seconds': round(elapsed, 2)
        }

    def _worker_loop(self):
        """Haupt-Worker-Loop."""
        deadline = time.time() + 10  # Erste Ausfuehrung nach 10s Startverzoegerung

        while self.running:
            remaining = deadline - time.time()
            if remaining > 0:
                # In Schritten von höchstens 5 s, damit stop() und reconfigure() schnell greifen.
                woke = self._wake_event.wait(timeout=min(remaining, 5))
                if woke:
                    if not self.running:
                        return  # stop() wurde aufgerufen
                    # Deadline neu berechnen ohne sofortige Analyse; kein clear() hier, sonst geht ein Wake verloren.
                    self._wake_event.clear()
                    deadline = time.time() + self.interval_seconds
                continue  # Deadline noch nicht erreicht (oder neu gesetzt) — weiter warten

            try:
                with self.app.app_context():
                    self._perform_update()
            except Exception as e:
                logger.error(f"Fehler im Background Worker: {e}", exc_info=True)

            if not self.running:
                return

            logger.info(f"Nächstes Update in {self.interval_seconds} Sekunden...")
            # Vor der Deadline leeren; ein reconfigure() während des Updates steht schon in interval_seconds.
            self._wake_event.clear()
            deadline = time.time() + self.interval_seconds

    def _perform_update(self):
        """Führe Sentiment-Update durch."""
        if not self._update_lock.acquire(blocking=False):
            logger.warning("Periodisches Update übersprungen — es läuft bereits eine Analyse (manueller Trigger)")
            return

        try:
            self._do_perform_update()
        finally:
            self._update_lock.release()

    def _do_perform_update(self):
        """Eigentliche Update-Logik, wird nur unter _update_lock aufgerufen."""
        start_time = time.time()
        logger.info("=== Background Worker: Starte Sentiment-Update ===")

        try:
            headlines = self._fetch_headlines()
            feed_count = len(get_database().get_active_feeds())

            if not headlines:
                logger.warning("Keine Headlines gefunden - Update abgebrochen")
                return

            logger.info(f"Headlines gesammelt: {len(headlines)}")

            # None heißt API-Fehler oder Teilparse: ohne DB-Write überspringen, sonst würde 0.0 gespeichert.
            analysis_result = self.analyze_function(headlines)

            if analysis_result is None or 'total_sentiment' not in analysis_result:
                logger.warning(
                    "Sentiment-Update übersprungen: Analyse lieferte kein verwertbares "
                    "Ergebnis (Anthropic API-Fehler oder Teilparse) — kein DB-Write."
                )
                return

            sentiment_score = analysis_result['total_sentiment']
            stats = analysis_result.get('statistics', {})
            headlines_analyzed = stats.get('analyzed_count', len(headlines))

            logger.info(f"Sentiment analysiert: {sentiment_score:.3f} ({headlines_analyzed} Headlines)")

            db = get_database()
            category = get_sentiment_category(sentiment_score)

            response_time_ms = int((time.time() - start_time) * 1000)

            sentiment_history_id = db.save_sentiment(
                sentiment_score=sentiment_score,
                category=category,
                headlines_analyzed=headlines_analyzed,
                source_count=feed_count,
                api_response_time_ms=response_time_ms,
                metadata={
                    'sentiment_distribution': stats.get('sentiment_distribution', {}),
                    'worker': 'background',
                    'timestamp': datetime.now().isoformat()
                }
            )

            logger.info(f"Sentiment in DB gespeichert: {sentiment_score:.3f} (id={sentiment_history_id})")

            # Headlines persistieren; ein Fehler bricht das Update nicht ab
            try:
                headline_results = analysis_result.get('results', [])
                if headline_results:
                    saved_count = db.save_headlines(
                        sentiment_history_id=sentiment_history_id,
                        results=headline_results
                    )
                    logger.info(f"Headlines persistiert: {saved_count} Einträge")
                else:
                    logger.warning("Keine Einzel-Headlines in analysis_result vorhanden")
            except Exception as headline_err:
                logger.error(f"Fehler beim Persistieren der Headlines (Sentiment bleibt gespeichert): {headline_err}")

            # Redis-Cache invalidieren: aktiver Key, Legacy-Key und Pattern
            _invalidate_moodlight_cache()
            logger.info("Cache invalidiert - nächster Request holt frische Daten")

            elapsed = time.time() - start_time
            logger.info(f"=== Update abgeschlossen in {elapsed:.2f}s ===")

        except Exception as e:
            logger.error(f"Fehler beim Sentiment-Update: {e}", exc_info=True)

    @staticmethod
    def _fetch_single_feed(feed_row: dict, num_headlines_per_source: int) -> tuple:
        """Holt Headlines von einem einzelnen Feed, ausgelagert für den ThreadPoolExecutor."""
        import feedparser
        import requests

        source = feed_row['name']
        url = feed_row['url']
        entries = []
        try:
            try:
                response = requests.get(url, timeout=10, headers={'User-Agent': 'WorldMoodAnalyzer/2.0'})
                response.raise_for_status()
                feed = feedparser.parse(response.content)
            except requests.exceptions.Timeout:
                logger.warning(f"Timeout bei {source}")
                return (feed_row, entries, False)
            except requests.exceptions.RequestException as e:
                logger.warning(f"Fehler beim Abrufen von {source}: {e}")
                return (feed_row, entries, False)

            if feed.bozo and isinstance(feed.bozo_exception, Exception):
                logger.warning(f"Feed-Fehler bei {source}: {feed.bozo_exception}")
                return (feed_row, entries, False)

            count = 0
            if feed.entries:
                for entry in feed.entries:
                    if count >= num_headlines_per_source:
                        break
                    link = getattr(entry, 'link', None)
                    title = getattr(entry, 'title', None)
                    if title and title.strip():
                        entries.append((title.strip(), link))
                        count += 1

            return (feed_row, entries, True)

        except Exception as e:
            logger.error(f"Fehler bei {source}: {e}")
            return (feed_row, entries, False)

    def _fetch_headlines(self):
        """Hole Headlines von RSS-Feeds (aus PostgreSQL-Datenbank)."""
        from concurrent.futures import ThreadPoolExecutor

        db = get_database()
        feeds = db.get_active_feeds()

        if not feeds:
            logger.warning("Keine aktiven Feeds in der Datenbank gefunden — Update übersprungen")
            return []

        headlines = []
        processed_links = set()
        num_headlines_per_source = self.headlines_per_source

        with ThreadPoolExecutor(max_workers=6) as executor:
            # map() hält die Reihenfolge der Feeds.
            results = executor.map(
                lambda f: self._fetch_single_feed(f, num_headlines_per_source),
                feeds
            )

            for feed_row, entries, success in results:
                feed_id = feed_row['id']
                source = feed_row['name']
                for headline_text, link in entries:
                    unique_key = link if link else headline_text
                    if unique_key not in processed_links:
                        headlines.append({
                            "headline": headline_text,
                            "source": source,
                            "link": link,
                            "feed_id": feed_id  # NEU: numerische Feed-ID für DB-FK
                        })
                        processed_links.add(unique_key)

                db.update_feed_fetch_status(feed_id, success=success)

        return headlines

_worker = None


def start_background_worker(app, analyze_function, interval_seconds=1800):
    """Starte den Background Worker."""
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


def get_background_worker() -> 'SentimentUpdateWorker':
    """Hole die laufende Worker-Instanz (für reconfigure()-Aufrufe aus API-Endpoints)."""
    global _worker
    return _worker
