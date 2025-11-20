# -*- coding: utf-8 -*-
"""
Database Layer für Moodlight System
PostgreSQL + Redis Integration
"""

import psycopg2
from psycopg2.extras import RealDictCursor
import redis
import json
import logging
import os
from datetime import datetime, timedelta
from typing import Optional, Dict, List, Any

logger = logging.getLogger(__name__)


class Database:
    """PostgreSQL Datenbank-Wrapper"""

    def __init__(self, database_url: str):
        """
        Initialisiere Datenbankverbindung

        Args:
            database_url: PostgreSQL Connection String
                Format: postgresql://user:password@host:port/database
        """
        self.database_url = database_url
        self.conn = None

    def connect(self):
        """Stelle Verbindung zur Datenbank her"""
        try:
            self.conn = psycopg2.connect(self.database_url)
            logger.info("PostgreSQL Verbindung erfolgreich hergestellt")
        except Exception as e:
            logger.error(f"Fehler bei PostgreSQL Verbindung: {e}")
            raise

    def disconnect(self):
        """Schließe Datenbankverbindung"""
        if self.conn:
            self.conn.close()
            logger.info("PostgreSQL Verbindung geschlossen")

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
            with self.conn.cursor() as cur:
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
            self.conn.rollback()
            logger.error(f"Fehler beim Speichern der Sentiment-Daten: {e}")
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
            with self.conn.cursor(cursor_factory=RealDictCursor) as cur:
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
        limit: int = 1000
    ) -> List[Dict[str, Any]]:
        """
        Hole Sentiment-Historie aus Datenbank

        Args:
            from_time: Start-Zeitpunkt (default: -24h)
            to_time: End-Zeitpunkt (default: now)
            limit: Maximale Anzahl Einträge

        Returns:
            Liste von Sentiment-Daten
        """
        if from_time is None:
            from_time = datetime.now() - timedelta(hours=24)
        if to_time is None:
            to_time = datetime.now()

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

        try:
            with self.conn.cursor(cursor_factory=RealDictCursor) as cur:
                cur.execute(query, (from_time, to_time, limit))
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
            with self.conn.cursor() as cur:
                cur.execute(query, (device_id, device_name, mac_address, firmware_version, ip_address, location))
                self.conn.commit()
                logger.debug(f"Gerät registriert/aktualisiert: {device_id}")
                return True
        except Exception as e:
            self.conn.rollback()
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
            with self.conn.cursor(cursor_factory=RealDictCursor) as cur:
                cur.execute(query, (hours,))
                results = cur.fetchall()
                return [dict(row) for row in results]
        except Exception as e:
            logger.error(f"Fehler beim Abrufen aktiver Geräte: {e}")
            return []

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
            with self.conn.cursor() as cur:
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
