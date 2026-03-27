# -*- coding: utf-8 -*-
"""
Database Layer für Moodlight System
PostgreSQL + Redis Integration mit robustem Connection-Handling
"""

import psycopg2
from psycopg2.extras import RealDictCursor
from psycopg2 import pool
import redis
import json
import logging
import os
import time
from datetime import datetime, timedelta
from typing import Optional, Dict, List, Any
from contextlib import contextmanager

logger = logging.getLogger(__name__)

# Konstanten für Connection-Handling
MAX_RECONNECT_ATTEMPTS = 3
RECONNECT_DELAY_SECONDS = 1


class Database:
    """PostgreSQL Datenbank-Wrapper mit robustem Connection-Handling"""

    def __init__(self, database_url: str):
        """
        Initialisiere Datenbankverbindung

        Args:
            database_url: PostgreSQL Connection String
                Format: postgresql://user:password@host:port/database
        """
        self.database_url = database_url
        self.conn = None
        self._connection_pool = None

    def connect(self):
        """Stelle Verbindung zur Datenbank her mit Connection-Pool"""
        try:
            # Verwende Connection-Pool für bessere Stabilität
            self._connection_pool = pool.ThreadedConnectionPool(
                minconn=1,
                maxconn=5,
                dsn=self.database_url
            )
            # Erstelle initiale Verbindung für Kompatibilität
            self.conn = self._connection_pool.getconn()
            logger.info("PostgreSQL Connection-Pool erfolgreich initialisiert")
        except Exception as e:
            logger.error(f"Fehler bei PostgreSQL Verbindung: {e}")
            raise

    def disconnect(self):
        """Schließe alle Datenbankverbindungen"""
        if self._connection_pool:
            self._connection_pool.closeall()
            logger.info("PostgreSQL Connection-Pool geschlossen")
        elif self.conn:
            self.conn.close()
            logger.info("PostgreSQL Verbindung geschlossen")

    def _ensure_connection(self) -> bool:
        """
        Stelle sicher, dass eine gültige Verbindung besteht.
        Versucht automatisch Wiederverbindung bei Fehlern.

        Returns:
            True wenn Verbindung OK, False bei Fehler
        """
        for attempt in range(MAX_RECONNECT_ATTEMPTS):
            try:
                # Prüfe ob Verbindung noch lebt
                if self.conn and not self.conn.closed:
                    # Teste die Verbindung mit einfacher Query
                    with self.conn.cursor() as cur:
                        cur.execute("SELECT 1")
                    return True

                # Verbindung tot - versuche neue aus Pool
                logger.warning(f"Verbindung verloren, Wiederverbindung Versuch {attempt + 1}/{MAX_RECONNECT_ATTEMPTS}")

                if self._connection_pool:
                    # Alte Verbindung zurückgeben (falls vorhanden)
                    if self.conn:
                        try:
                            self._connection_pool.putconn(self.conn, close=True)
                        except Exception:
                            pass
                    # Neue Verbindung aus Pool holen
                    self.conn = self._connection_pool.getconn()
                    logger.info("Neue Verbindung aus Pool erhalten")
                    return True
                else:
                    # Kein Pool - direkte Verbindung
                    self.conn = psycopg2.connect(self.database_url)
                    logger.info("Neue direkte Verbindung hergestellt")
                    return True

            except Exception as e:
                logger.error(f"Verbindungsversuch {attempt + 1} fehlgeschlagen: {e}")
                if attempt < MAX_RECONNECT_ATTEMPTS - 1:
                    time.sleep(RECONNECT_DELAY_SECONDS)

        logger.error("Alle Wiederverbindungsversuche fehlgeschlagen")
        return False

    @contextmanager
    def get_cursor(self, cursor_factory=None):
        """
        Context Manager für sichere Cursor-Verwendung mit Auto-Reconnect.

        Args:
            cursor_factory: Optional Cursor-Factory (z.B. RealDictCursor)

        Yields:
            Datenbank-Cursor
        """
        if not self._ensure_connection():
            raise Exception("Datenbankverbindung nicht verfügbar")

        cursor = self.conn.cursor(cursor_factory=cursor_factory) if cursor_factory else self.conn.cursor()
        try:
            yield cursor
        finally:
            cursor.close()

    def save_sentiment(
        self,
        sentiment_score: float,
        category: str,
        headlines_analyzed: int,
        source_count: int = 12,
        api_response_time_ms: Optional[int] = None,
        metadata: Optional[Dict] = None
    ) -> int:
        """
        Speichere Sentiment-Analyse in Datenbank

        Args:
            sentiment_score: Sentiment-Wert (-1.0 bis +1.0)
            category: Kategorie (sehr negativ, negativ, neutral, positiv, sehr positiv)
            headlines_analyzed: Anzahl analysierter Headlines
            source_count: Anzahl der Quellen
            api_response_time_ms: API Response Time in ms
            metadata: Zusätzliche Metadaten als Dict

        Returns:
            ID des eingefügten Datensatzes
        """
        query = """
            INSERT INTO sentiment_history
            (sentiment_score, category, headlines_analyzed, source_count, api_response_time_ms, metadata)
            VALUES (%s, %s, %s, %s, %s, %s)
            RETURNING id;
        """

        try:
            with self.get_cursor() as cur:
                cur.execute(query, (
                    sentiment_score,
                    category,
                    headlines_analyzed,
                    source_count,
                    api_response_time_ms,
                    json.dumps(metadata) if metadata else None
                ))
                result_id = cur.fetchone()[0]
                self.conn.commit()
                logger.info(f"Sentiment-Daten gespeichert: ID={result_id}, Score={sentiment_score}")
                return result_id
        except Exception as e:
            if self.conn:
                try:
                    self.conn.rollback()
                except Exception:
                    pass
            logger.error(f"Fehler beim Speichern der Sentiment-Daten: {e}")
            raise

    def save_headlines(
        self,
        sentiment_history_id: int,
        results: list
    ) -> int:
        """
        Speichere analysierte Headlines in der Datenbank.

        Args:
            sentiment_history_id: FK zu sentiment_history.id
            results: Liste von Dicts (headline, source, feed_id, sentiment, strength, link optional)

        Returns:
            Anzahl gespeicherter Headlines (0 wenn results leer)
        """
        if not results:
            return 0

        query = """
            INSERT INTO headlines
            (sentiment_history_id, feed_id, headline, source_name, sentiment_score, strength, link)
            VALUES (%s, %s, %s, %s, %s, %s, %s)
        """
        rows = [
            (
                sentiment_history_id,
                r.get('feed_id'),
                r['headline'],
                r.get('source', 'unknown'),
                r['sentiment'],
                r.get('strength', 'neutral'),
                r.get('link')
            )
            for r in results
        ]

        try:
            with self.get_cursor() as cur:
                cur.executemany(query, rows)
                self.conn.commit()
                logger.info(f"Headlines gespeichert: {len(rows)} Einträge für sentiment_history_id={sentiment_history_id}")
                return len(rows)
        except Exception as e:
            if self.conn:
                try:
                    self.conn.rollback()
                except Exception:
                    pass
            logger.error(f"Fehler beim Speichern der Headlines: {e}")
            raise

    def get_latest_sentiment(self) -> Optional[Dict[str, Any]]:
        """
        Hole den letzten Sentiment-Wert aus der Datenbank

        Returns:
            Dict mit Sentiment-Daten oder None
        """
        query = """
            SELECT
                id,
                timestamp,
                sentiment_score,
                category,
                headlines_analyzed,
                source_count,
                created_at
            FROM sentiment_history
            ORDER BY timestamp DESC
            LIMIT 1;
        """

        try:
            with self.get_cursor(cursor_factory=RealDictCursor) as cur:
                cur.execute(query)
                result = cur.fetchone()
                return dict(result) if result else None
        except Exception as e:
            logger.error(f"Fehler beim Abrufen des letzten Sentiment-Werts: {e}")
            return None

    def get_sentiment_history(
        self,
        from_time: Optional[datetime] = None,
        to_time: Optional[datetime] = None,
        limit: int = 50000
    ) -> List[Dict[str, Any]]:
        """
        Hole Sentiment-Historie aus Datenbank

        Args:
            from_time: Start-Zeitpunkt (None = alle Daten)
            to_time: End-Zeitpunkt (default: now)
            limit: Maximale Anzahl Einträge

        Returns:
            Liste von Sentiment-Daten
        """
        if to_time is None:
            to_time = datetime.now()

        # Query dynamisch bauen je nachdem ob from_time gesetzt ist
        if from_time is None:
            # Alle Daten bis to_time
            query = """
                SELECT
                    timestamp,
                    sentiment_score,
                    category,
                    headlines_analyzed
                FROM sentiment_history
                WHERE timestamp <= %s
                ORDER BY timestamp ASC
                LIMIT %s;
            """
            params = (to_time, limit)
        else:
            # Zeitbereich
            query = """
                SELECT
                    timestamp,
                    sentiment_score,
                    category,
                    headlines_analyzed
                FROM sentiment_history
                WHERE timestamp BETWEEN %s AND %s
                ORDER BY timestamp ASC
                LIMIT %s;
            """
            params = (from_time, to_time, limit)

        try:
            with self.get_cursor(cursor_factory=RealDictCursor) as cur:
                cur.execute(query, params)
                results = cur.fetchall()
                return [dict(row) for row in results]
        except Exception as e:
            logger.error(f"Fehler beim Abrufen der Sentiment-Historie: {e}")
            return []

    def register_device(
        self,
        device_id: str,
        device_name: Optional[str] = None,
        mac_address: Optional[str] = None,
        firmware_version: Optional[str] = None,
        ip_address: Optional[str] = None,
        location: Optional[str] = None
    ) -> bool:
        """
        Registriere oder aktualisiere ein Moodlight-Gerät

        Args:
            device_id: Eindeutige Geräte-ID
            device_name: Name des Geräts
            mac_address: MAC-Adresse
            firmware_version: Firmware-Version
            ip_address: IP-Adresse
            location: Standort

        Returns:
            True bei Erfolg
        """
        query = """
            INSERT INTO device_statistics
            (device_id, device_name, mac_address, firmware_version, ip_address, location, total_requests)
            VALUES (%s, %s, %s, %s, %s, %s, 1)
            ON CONFLICT (device_id) DO UPDATE SET
                device_name = COALESCE(EXCLUDED.device_name, device_statistics.device_name),
                mac_address = COALESCE(EXCLUDED.mac_address, device_statistics.mac_address),
                firmware_version = COALESCE(EXCLUDED.firmware_version, device_statistics.firmware_version),
                ip_address = COALESCE(EXCLUDED.ip_address, device_statistics.ip_address),
                location = COALESCE(EXCLUDED.location, device_statistics.location),
                last_seen = NOW(),
                total_requests = device_statistics.total_requests + 1;
        """

        try:
            with self.get_cursor() as cur:
                cur.execute(query, (device_id, device_name, mac_address, firmware_version, ip_address, location))
                self.conn.commit()
                logger.debug(f"Gerät registriert/aktualisiert: {device_id}")
                return True
        except Exception as e:
            if self.conn:
                try:
                    self.conn.rollback()
                except Exception:
                    pass
            logger.error(f"Fehler beim Registrieren des Geräts {device_id}: {e}")
            return False

    def get_active_devices(self, hours: int = 2) -> List[Dict[str, Any]]:
        """
        Hole alle aktiven Geräte (last_seen < X Stunden)

        Args:
            hours: Zeitfenster in Stunden

        Returns:
            Liste von Geräten
        """
        query = """
            SELECT
                device_id,
                device_name,
                firmware_version,
                last_seen,
                total_requests,
                location,
                EXTRACT(EPOCH FROM (NOW() - last_seen)) / 60 AS minutes_since_last_seen
            FROM device_statistics
            WHERE last_seen >= NOW() - INTERVAL '%s hours'
            ORDER BY last_seen DESC;
        """

        try:
            with self.get_cursor(cursor_factory=RealDictCursor) as cur:
                cur.execute(query, (hours,))
                results = cur.fetchall()
                return [dict(row) for row in results]
        except Exception as e:
            logger.error(f"Fehler beim Abrufen aktiver Geräte: {e}")
            return []

    def get_active_feeds(self) -> List[Dict[str, Any]]:
        """
        Hole alle aktiven RSS-Feeds aus der Datenbank.

        Returns:
            Liste von Dicts mit 'id', 'name' und 'url' Feldern.
            Leere Liste bei Fehler oder keinen aktiven Feeds.
        """
        query = """
            SELECT id, name, url
            FROM feeds
            WHERE active = TRUE
            ORDER BY name ASC;
        """
        try:
            with self.get_cursor(cursor_factory=RealDictCursor) as cur:
                cur.execute(query)
                results = cur.fetchall()
                return [dict(row) for row in results]
        except Exception as e:
            logger.error(f"Fehler beim Laden der aktiven Feeds: {e}")
            return []

    def get_all_feeds(self) -> List[Dict[str, Any]]:
        """
        Hole alle RSS-Feeds aus der Datenbank (aktiv und inaktiv).

        Returns:
            Liste von Dicts mit id, name, url, active, last_fetched_at, error_count, created_at.
            Leere Liste bei Fehler.
        """
        query = """
            SELECT id, name, url, active, last_fetched_at, error_count, created_at
            FROM feeds
            ORDER BY name ASC;
        """
        try:
            with self.get_cursor(cursor_factory=RealDictCursor) as cur:
                cur.execute(query)
                results = cur.fetchall()
                return [dict(row) for row in results]
        except Exception as e:
            logger.error(f"Fehler beim Laden aller Feeds: {e}")
            return []

    def get_recent_headlines(self, limit: int = 50) -> List[Dict[str, Any]]:
        """
        Gibt die zuletzt analysierten Headlines mit Feed-Zuordnung zurück.

        Args:
            limit: Maximale Anzahl zurückgegebener Headlines (default: 50)

        Returns:
            Liste von Dicts mit id, headline, source_name, sentiment_score,
            strength, link, analyzed_at (ISO-String), feed_name, feed_id.
            Leere Liste bei Fehler.
        """
        query = """
            SELECT
                h.id,
                h.headline,
                h.source_name,
                h.sentiment_score,
                h.strength,
                h.link,
                h.analyzed_at,
                f.name AS feed_name,
                f.id AS feed_id
            FROM headlines h
            LEFT JOIN feeds f ON h.feed_id = f.id
            ORDER BY h.analyzed_at DESC
            LIMIT %s;
        """
        try:
            with self.get_cursor(cursor_factory=RealDictCursor) as cur:
                cur.execute(query, (limit,))
                results = cur.fetchall()
                rows = []
                for row in results:
                    d = dict(row)
                    if isinstance(d.get('analyzed_at'), datetime):
                        d['analyzed_at'] = d['analyzed_at'].isoformat()
                    rows.append(d)
                return rows
        except Exception as e:
            logger.error(f"Fehler beim Abrufen der Headlines: {e}")
            return []

    def add_feed(self, name: str, url: str) -> Optional[Dict[str, Any]]:
        """
        Füge neuen RSS-Feed in die Datenbank ein.

        Args:
            name: Anzeigename des Feeds
            url: Feed-URL (muss einzigartig sein)

        Returns:
            Dict mit id, name, url, active, created_at des neu angelegten Feeds.
            None bei Fehler oder Duplikat (psycopg2.errors.UniqueViolation).

        Raises:
            ValueError: Wenn die URL bereits existiert (UNIQUE-Constraint).
        """
        query = """
            INSERT INTO feeds (name, url, active)
            VALUES (%s, %s, TRUE)
            RETURNING id, name, url, active, created_at;
        """
        try:
            with self.get_cursor(cursor_factory=RealDictCursor) as cur:
                cur.execute(query, (name, url))
                result = cur.fetchone()
                self.conn.commit()
                logger.info(f"Neuer Feed angelegt: {name} ({url})")
                return dict(result)
        except psycopg2.errors.UniqueViolation:
            self.conn.rollback()
            raise ValueError(f"Feed-URL bereits vorhanden: {url}")
        except Exception as e:
            if self.conn:
                try:
                    self.conn.rollback()
                except Exception:
                    pass
            logger.error(f"Fehler beim Anlegen des Feeds: {e}")
            raise

    def delete_feed(self, feed_id: int) -> bool:
        """
        Lösche RSS-Feed aus der Datenbank.

        Args:
            feed_id: ID des zu löschenden Feeds

        Returns:
            True wenn Feed gefunden und gelöscht, False wenn ID nicht existiert.
        """
        query = """
            DELETE FROM feeds
            WHERE id = %s
            RETURNING id;
        """
        try:
            with self.get_cursor() as cur:
                cur.execute(query, (feed_id,))
                deleted = cur.fetchone()
                self.conn.commit()
                if deleted:
                    logger.info(f"Feed gelöscht: ID={feed_id}")
                    return True
                else:
                    logger.warning(f"Feed nicht gefunden: ID={feed_id}")
                    return False
        except Exception as e:
            if self.conn:
                try:
                    self.conn.rollback()
                except Exception:
                    pass
            logger.error(f"Fehler beim Löschen des Feeds {feed_id}: {e}")
            raise

    def get_statistics(self) -> Dict[str, Any]:
        """
        Hole System-Statistiken

        Returns:
            Dict mit verschiedenen Statistiken
        """
        queries = {
            'total_devices': "SELECT COUNT(*) FROM device_statistics;",
            'active_devices_24h': "SELECT COUNT(*) FROM device_statistics WHERE last_seen >= NOW() - INTERVAL '24 hours';",
            'total_analyses': "SELECT COUNT(*) FROM sentiment_history;",
            'avg_sentiment_24h': "SELECT ROUND(AVG(sentiment_score)::numeric, 3) FROM sentiment_history WHERE timestamp >= NOW() - INTERVAL '24 hours';",
            'avg_sentiment_7d': "SELECT ROUND(AVG(sentiment_score)::numeric, 3) FROM sentiment_history WHERE timestamp >= NOW() - INTERVAL '7 days';",
        }

        stats = {}
        try:
            with self.get_cursor() as cur:
                for key, query in queries.items():
                    cur.execute(query)
                    result = cur.fetchone()
                    stats[key] = result[0] if result and result[0] is not None else 0

                # Letztes Update
                cur.execute("SELECT MAX(timestamp) FROM sentiment_history;")
                last_update = cur.fetchone()[0]
                stats['last_update'] = last_update.isoformat() if last_update else None

                return stats
        except Exception as e:
            logger.error(f"Fehler beim Abrufen der Statistiken: {e}")
            return {}

    def get_score_percentiles(self, days: int = 7) -> Dict[str, Any]:
        """
        Berechnet historische Perzentile der Sentiment-Scores der letzten N Tage.

        Args:
            days: Anzahl Tage für das historische Fenster (default: 7)

        Returns:
            Dict mit p20, p40, p60, p80, min, max, median, count, fallback.
            Bei weniger als 3 Datenpunkten: fallback=True mit festen Standardwerten.
        """
        FALLBACK = {
            "p20": -0.5, "p40": -0.2, "p60": 0.1, "p80": 0.4,
            "min": -1.0, "max": 1.0, "median": 0.0,
            "count": 0, "fallback": True
        }

        query = """
            SELECT
                COUNT(*) AS count,
                percentile_cont(0.20) WITHIN GROUP (ORDER BY sentiment_score) AS p20,
                percentile_cont(0.40) WITHIN GROUP (ORDER BY sentiment_score) AS p40,
                percentile_cont(0.50) WITHIN GROUP (ORDER BY sentiment_score) AS median,
                percentile_cont(0.60) WITHIN GROUP (ORDER BY sentiment_score) AS p60,
                percentile_cont(0.80) WITHIN GROUP (ORDER BY sentiment_score) AS p80,
                MIN(sentiment_score) AS min,
                MAX(sentiment_score) AS max
            FROM sentiment_history
            WHERE timestamp >= NOW() - INTERVAL '%s days';
        """
        try:
            with self.get_cursor(cursor_factory=RealDictCursor) as cur:
                cur.execute(query, (days,))
                row = cur.fetchone()
                if not row or row['count'] < 3:
                    logger.warning(
                        f"Zu wenige Datenpunkte für Perzentil-Berechnung "
                        f"(count={row['count'] if row else 0}, days={days}) — verwende Fallback"
                    )
                    if row:
                        FALLBACK['count'] = int(row['count'])
                    return FALLBACK
                return {
                    "p20": round(float(row['p20']), 4),
                    "p40": round(float(row['p40']), 4),
                    "p60": round(float(row['p60']), 4),
                    "p80": round(float(row['p80']), 4),
                    "min": round(float(row['min']), 4),
                    "max": round(float(row['max']), 4),
                    "median": round(float(row['median']), 4),
                    "count": int(row['count']),
                    "fallback": False
                }
        except Exception as e:
            logger.error(f"Fehler bei Perzentil-Berechnung: {e}")
            return FALLBACK

    def check_connection_health(self) -> Dict[str, Any]:
        """
        Prüfe Gesundheit der Datenbankverbindung

        Returns:
            Dict mit Status-Informationen
        """
        health = {
            'connected': False,
            'pool_available': self._connection_pool is not None,
            'last_check': datetime.now().isoformat()
        }

        try:
            if self._ensure_connection():
                health['connected'] = True
                with self.get_cursor() as cur:
                    cur.execute("SELECT version();")
                    health['db_version'] = cur.fetchone()[0]
                    cur.execute("SELECT COUNT(*) FROM sentiment_history;")
                    health['total_records'] = cur.fetchone()[0]
        except Exception as e:
            health['error'] = str(e)

        return health

    def get_setting(self, key: str, default: Optional[str] = None) -> Optional[str]:
        """
        Lese einen Einstellungs-Wert aus der settings-Tabelle.

        Args:
            key: Einstellungs-Schlüssel (z.B. 'analysis_interval')
            default: Rückgabewert wenn Key nicht gefunden

        Returns:
            Wert als String oder default
        """
        query = "SELECT value FROM settings WHERE key = %s;"
        try:
            with self.get_cursor() as cur:
                cur.execute(query, (key,))
                result = cur.fetchone()
                return result[0] if result else default
        except Exception as e:
            logger.error(f"Fehler beim Lesen von setting '{key}': {e}")
            return default

    def set_setting(self, key: str, value: str) -> bool:
        """
        Speichere oder aktualisiere einen Einstellungs-Wert (UPSERT).

        Args:
            key: Einstellungs-Schlüssel
            value: Einstellungs-Wert als String

        Returns:
            True bei Erfolg, False bei Fehler
        """
        query = """
            INSERT INTO settings (key, value)
            VALUES (%s, %s)
            ON CONFLICT (key) DO UPDATE SET
                value = EXCLUDED.value,
                updated_at = NOW();
        """
        try:
            with self.get_cursor() as cur:
                cur.execute(query, (key, value))
                self.conn.commit()
                logger.info(f"Einstellung gespeichert: {key}='{value}'")
                return True
        except Exception as e:
            if self.conn:
                try:
                    self.conn.rollback()
                except Exception:
                    pass
            logger.error(f"Fehler beim Speichern von setting '{key}': {e}")
            return False

    def get_all_settings(self) -> Dict[str, str]:
        """
        Alle Einstellungen als Dict zurückgeben.

        Returns:
            Dict[key -> value], leer bei Fehler
        """
        query = "SELECT key, value FROM settings ORDER BY key ASC;"
        try:
            with self.get_cursor() as cur:
                cur.execute(query)
                results = cur.fetchall()
                return {row[0]: row[1] for row in results}
        except Exception as e:
            logger.error(f"Fehler beim Laden aller Einstellungen: {e}")
            return {}


class RedisCache:
    """Redis Cache-Wrapper"""

    def __init__(self, redis_url: str = "redis://localhost:6379/0"):
        """
        Initialisiere Redis-Verbindung

        Args:
            redis_url: Redis Connection String
        """
        self.redis_url = redis_url
        self.client = None

    def connect(self):
        """Stelle Verbindung zu Redis her"""
        try:
            self.client = redis.from_url(self.redis_url, decode_responses=True)
            self.client.ping()
            logger.info("Redis Verbindung erfolgreich hergestellt")
        except Exception as e:
            logger.error(f"Fehler bei Redis Verbindung: {e}")
            raise

    def get(self, key: str) -> Optional[Dict]:
        """
        Hole Wert aus Cache

        Args:
            key: Cache-Key

        Returns:
            Dict oder None
        """
        try:
            value = self.client.get(key)
            if value:
                return json.loads(value)
            return None
        except Exception as e:
            logger.error(f"Fehler beim Lesen aus Redis Cache ({key}): {e}")
            return None

    def set(self, key: str, value: Dict, ttl: int = 300):
        """
        Setze Wert in Cache

        Args:
            key: Cache-Key
            value: Wert als Dict
            ttl: Time-to-Live in Sekunden (default: 5 Min)
        """
        try:
            self.client.setex(key, ttl, json.dumps(value))
            logger.debug(f"Cache gesetzt: {key} (TTL: {ttl}s)")
        except Exception as e:
            logger.error(f"Fehler beim Schreiben in Redis Cache ({key}): {e}")

    def delete(self, key: str):
        """Lösche Wert aus Cache"""
        try:
            self.client.delete(key)
            logger.debug(f"Cache gelöscht: {key}")
        except Exception as e:
            logger.error(f"Fehler beim Löschen aus Redis Cache ({key}): {e}")

    def clear_all(self):
        """Lösche alle Cache-Einträge"""
        try:
            self.client.flushdb()
            logger.info("Gesamter Redis Cache gelöscht")
        except Exception as e:
            logger.error(f"Fehler beim Löschen des gesamten Cache: {e}")


# Singleton-Instanzen
_db = None
_cache = None


def get_database() -> Database:
    """Hole Database-Instanz (Singleton)"""
    global _db
    if _db is None:
        database_url = os.environ.get('DATABASE_URL', 'postgresql://moodlight:password@localhost:5432/moodlight')
        _db = Database(database_url)
        _db.connect()
    return _db


def get_cache() -> RedisCache:
    """Hole Redis-Cache-Instanz (Singleton)"""
    global _cache
    if _cache is None:
        redis_url = os.environ.get('REDIS_URL', 'redis://localhost:6379/0')
        _cache = RedisCache(redis_url)
        _cache.connect()
    return _cache


def compute_led_index(score: float, thresholds: Dict[str, Any]) -> int:
    """
    Mappt einen Sentiment-Score auf LED-Index 0–4 basierend auf Perzentil-Schwellwerten.

    Args:
        score: Sentiment-Score (-1.0 bis +1.0)
        thresholds: Dict aus get_score_percentiles() mit Schlüsseln p20, p40, p60, p80

    Returns:
        LED-Index 0 (sehr negativ) bis 4 (sehr positiv)
    """
    if score < thresholds.get('p20', -0.5):
        return 0
    if score < thresholds.get('p40', -0.2):
        return 1
    if score < thresholds.get('p60', 0.1):
        return 2
    if score < thresholds.get('p80', 0.4):
        return 3
    return 4
