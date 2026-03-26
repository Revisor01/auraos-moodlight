# -*- coding: utf-8 -*-
"""
Moodlight API Extensions
Neue Endpunkte für 100+ Geräte mit Database + Redis Caching

Integration in app.py:
    from moodlight_extensions import register_moodlight_endpoints
    register_moodlight_endpoints(app)
"""

import logging
from flask import jsonify, request
from datetime import datetime, timedelta
from database import get_database, get_cache
from shared_config import get_sentiment_category as get_category_from_score
import time
import requests as http_requests

logger = logging.getLogger(__name__)


# Farben werden auf dem Moodlight selbst konfiguriert (Teil der User-Config)
# Wir liefern nur den reinen Sentiment-Score


def register_moodlight_endpoints(app):
    """
    Registriere alle neuen Moodlight-API-Endpunkte

    Args:
        app: Flask App-Instanz
    """

    # ===== MOODLIGHT CURRENT ENDPOINT (Hauptendpunkt für Geräte) =====
    @app.route('/api/moodlight/current', methods=['GET'])
    def get_moodlight_current():
        """
        Optimierter Endpunkt für Moodlight-Geräte
        - Nutzt Redis-Cache (5 Min TTL)
        - Liefert RGB/HEX-Farben direkt
        - Tracking von Geräten
        """
        start_time = time.time()

        # Device-ID aus Header holen (für Tracking)
        device_id = request.headers.get('X-Device-ID', 'unknown')
        device_name = request.headers.get('X-Device-Name')
        firmware_version = request.headers.get('X-Firmware-Version')
        ip_address = request.remote_addr

        try:
            cache = get_cache()

            # 1. Redis-Cache prüfen
            cached_data = cache.get('moodlight:current')
            if cached_data:
                logger.debug(f"Cache HIT für /api/moodlight/current (Device: {device_id})")

                # Device-Statistik asynchron aktualisieren
                if device_id != 'unknown':
                    try:
                        db = get_database()
                        db.register_device(
                            device_id=device_id,
                            device_name=device_name,
                            firmware_version=firmware_version,
                            ip_address=ip_address
                        )
                    except Exception as e:
                        logger.error(f"Fehler beim Device-Tracking: {e}")

                return jsonify(cached_data)

            # 2. Cache MISS - Daten aus Datenbank holen
            logger.debug(f"Cache MISS für /api/moodlight/current (Device: {device_id})")

            db = get_database()
            latest = db.get_latest_sentiment()

            if not latest:
                return jsonify({
                    "status": "error",
                    "message": "Keine Sentiment-Daten verfügbar"
                }), 503

            # 3. Response-Daten vorbereiten
            sentiment_score = latest['sentiment_score']

            response = {
                "status": "success",
                "timestamp": latest['timestamp'].isoformat(),
                "sentiment": round(sentiment_score, 2),
                "category": latest['category'],
                "headlines_analyzed": latest.get('headlines_analyzed', 0),
                "next_update_minutes": 30,
                "cached": False
            }

            # 4. In Redis cachen (5 Minuten)
            cache.set('moodlight:current', response, ttl=300)

            # 5. Device-Tracking
            if device_id != 'unknown':
                try:
                    db.register_device(
                        device_id=device_id,
                        device_name=device_name,
                        firmware_version=firmware_version,
                        ip_address=ip_address
                    )
                except Exception as e:
                    logger.error(f"Fehler beim Device-Tracking: {e}")

            # 6. Response-Zeit loggen
            response_time = int((time.time() - start_time) * 1000)
            logger.info(f"Moodlight Request: Device={device_id}, Response-Time={response_time}ms")

            return jsonify(response)

        except Exception as e:
            logger.error(f"Fehler in /api/moodlight/current: {e}", exc_info=True)
            return jsonify({
                "status": "error",
                "message": "Interner Serverfehler"
            }), 500

    # ===== HISTORY ENDPOINT =====
    @app.route('/api/moodlight/history', methods=['GET'])
    def get_moodlight_history():
        """
        Sentiment-Historie abrufen

        Query-Parameter:
            all: Wenn 'true', hole alle Daten (kein Zeitlimit)
            hours: Anzahl Stunden zurück (einfacher Modus, default: 168 = 7 Tage)
            from: ISO8601 timestamp
            to: ISO8601 timestamp (default: now)
            limit: Max. Anzahl Einträge (default: 50000)
        """
        try:
            # Parameter parsen
            all_param = request.args.get('all', '').lower() == 'true'
            hours_param = request.args.get('hours', type=int)
            from_param = request.args.get('from')
            to_param = request.args.get('to')
            limit = request.args.get('limit', 50000, type=int)

            # Zeitstempel konvertieren
            from_time = None
            to_time = None

            if all_param:
                # Alle Daten: Kein Zeitfilter (from_time bleibt None)
                to_time = datetime.utcnow()
                logger.info("History: Hole ALLE Daten (kein Zeitlimit)")
            elif hours_param:
                # Wenn 'hours' Parameter vorhanden, berechne from_time
                from_time = datetime.utcnow() - timedelta(hours=hours_param)
                to_time = datetime.utcnow()
            elif from_param:
                # Expliziter from-Parameter
                try:
                    from_time = datetime.fromisoformat(from_param.replace('Z', '+00:00'))
                except ValueError:
                    return jsonify({"error": "Ungültiges 'from' Format (ISO8601 erwartet)"}), 400

                if to_param:
                    try:
                        to_time = datetime.fromisoformat(to_param.replace('Z', '+00:00'))
                    except ValueError:
                        return jsonify({"error": "Ungültiges 'to' Format (ISO8601 erwartet)"}), 400
            else:
                # Keine Parameter: Default 7 Tage
                from_time = datetime.utcnow() - timedelta(days=7)
                to_time = datetime.utcnow()

            # Daten aus DB holen
            db = get_database()
            history = db.get_sentiment_history(from_time, to_time, limit)

            # Timestamps zu ISO-Format konvertieren
            for entry in history:
                if isinstance(entry.get('timestamp'), datetime):
                    entry['timestamp'] = entry['timestamp'].isoformat()

            return jsonify({
                "count": len(history),
                "data": history
            })

        except Exception as e:
            logger.error(f"Fehler in /api/moodlight/history: {e}", exc_info=True)
            return jsonify({"error": str(e)}), 500

    # ===== TREND ENDPOINT =====
    @app.route('/api/moodlight/trend', methods=['GET'])
    def get_moodlight_trend():
        """
        Trend-Analyse: Aktuell vs. 24h vs. 7d Durchschnitt
        """
        try:
            db = get_database()

            # Aktuelle Daten
            latest = db.get_latest_sentiment()
            if not latest:
                return jsonify({"error": "Keine Daten verfügbar"}), 503

            current_score = latest['sentiment_score']

            # Statistiken
            stats = db.get_statistics()

            avg_24h = float(stats.get('avg_sentiment_24h', 0))
            avg_7d = float(stats.get('avg_sentiment_7d', 0))

            # Trend berechnen
            change_24h = current_score - avg_24h

            if abs(change_24h) < 0.05:
                trend = "stabil"
            elif change_24h > 0:
                trend = "verbessernd"
            else:
                trend = "verschlechternd"

            return jsonify({
                "status": "success",
                "current": round(current_score, 2),
                "avg_24h": round(avg_24h, 2),
                "avg_7d": round(avg_7d, 2),
                "change_24h": round(change_24h, 2),
                "trend": trend,
                "category": latest['category'],
                "timestamp": latest['timestamp'].isoformat()
            })

        except Exception as e:
            logger.error(f"Fehler in /api/moodlight/trend: {e}", exc_info=True)
            return jsonify({"status": "error", "message": str(e)}), 500

    # ===== STATISTICS ENDPOINT =====
    @app.route('/api/moodlight/stats', methods=['GET'])
    def get_moodlight_stats():
        """
        System-Statistiken: Geräte, Analysen, Uptime
        """
        try:
            db = get_database()
            stats = db.get_statistics()

            return jsonify({
                "status": "success",
                "total_devices": stats.get('total_devices', 0),
                "active_devices_24h": stats.get('active_devices_24h', 0),
                "total_analyses": stats.get('total_analyses', 0),
                "last_update": stats.get('last_update'),
                "avg_sentiment_24h": float(stats.get('avg_sentiment_24h', 0)),
                "avg_sentiment_7d": float(stats.get('avg_sentiment_7d', 0))
            })

        except Exception as e:
            logger.error(f"Fehler in /api/moodlight/stats: {e}", exc_info=True)
            return jsonify({"status": "error", "message": str(e)}), 500

    # ===== DEVICES ENDPOINT =====
    @app.route('/api/moodlight/devices', methods=['GET'])
    def get_moodlight_devices():
        """
        Liste aller aktiven Geräte (last_seen < 2h)

        Query-Parameter:
            hours: Zeitfenster in Stunden (default: 2)
        """
        try:
            hours = request.args.get('hours', 2, type=int)

            db = get_database()
            devices = db.get_active_devices(hours=hours)

            # Timestamps konvertieren
            for device in devices:
                if isinstance(device.get('last_seen'), datetime):
                    device['last_seen'] = device['last_seen'].isoformat()
                device['online'] = device.get('minutes_since_last_seen', 999) < 60

            return jsonify({
                "status": "success",
                "count": len(devices),
                "devices": devices
            })

        except Exception as e:
            logger.error(f"Fehler in /api/moodlight/devices: {e}", exc_info=True)
            return jsonify({"status": "error", "message": str(e)}), 500

    # ===== CACHE CLEAR ENDPOINT (Admin) =====
    @app.route('/api/moodlight/cache/clear', methods=['POST'])
    def clear_moodlight_cache():
        """
        Lösche Redis-Cache (für manuelle Aktualisierung)
        """
        try:
            cache = get_cache()
            cache.delete('moodlight:current')

            logger.info("Moodlight Cache manuell gelöscht")

            return jsonify({
                "status": "success",
                "message": "Cache gelöscht"
            })

        except Exception as e:
            logger.error(f"Fehler beim Cache-Löschen: {e}", exc_info=True)
            return jsonify({"status": "error", "message": str(e)}), 500

    # ===== FEED-VERWALTUNG ENDPOINTS =====

    @app.route('/api/moodlight/feeds', methods=['GET'])
    def get_moodlight_feeds():
        """
        Alle RSS-Feeds mit Status zurückgeben (FEED-02)
        """
        try:
            db = get_database()
            feeds = db.get_all_feeds()

            # Timestamps zu ISO-Format konvertieren
            for feed in feeds:
                if isinstance(feed.get('last_fetched_at'), datetime):
                    feed['last_fetched_at'] = feed['last_fetched_at'].isoformat()
                elif feed.get('last_fetched_at') is None:
                    feed['last_fetched_at'] = None
                if isinstance(feed.get('created_at'), datetime):
                    feed['created_at'] = feed['created_at'].isoformat()

            return jsonify({
                "status": "success",
                "count": len(feeds),
                "feeds": feeds
            })

        except Exception as e:
            logger.error(f"Fehler in GET /api/moodlight/feeds: {e}", exc_info=True)
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500


    @app.route('/api/moodlight/feeds', methods=['POST'])
    def add_moodlight_feed():
        """
        Neuen RSS-Feed hinzufügen mit URL-Validierung (FEED-03, FEED-05)

        Body (JSON): {"url": "https://...", "name": "Feed-Name"}
        Gibt 201 bei Erfolg, 400 bei fehlenden Feldern, 409 bei Duplikat, 422 bei nicht erreichbarer URL.
        """
        try:
            data = request.get_json(silent=True)
            if not data:
                return jsonify({"status": "error", "message": "JSON-Body erwartet"}), 400

            url = data.get('url', '').strip()
            name = data.get('name', '').strip()

            if not url:
                return jsonify({"status": "error", "message": "Feld 'url' fehlt oder leer"}), 400
            if not name:
                return jsonify({"status": "error", "message": "Feld 'name' fehlt oder leer"}), 400

            # URL-Validierung: Erreichbarkeit prüfen (FEED-05)
            try:
                resp = http_requests.get(url, timeout=5, allow_redirects=True, stream=True)
                resp.close()
                if resp.status_code >= 400:
                    return jsonify({
                        "status": "error",
                        "message": f"Feed-URL nicht erreichbar: HTTP {resp.status_code}"
                    }), 422
            except http_requests.exceptions.Timeout:
                return jsonify({
                    "status": "error",
                    "message": "Feed-URL nicht erreichbar: Timeout (5s)"
                }), 422
            except http_requests.exceptions.ConnectionError:
                return jsonify({
                    "status": "error",
                    "message": "Feed-URL nicht erreichbar: Verbindungsfehler"
                }), 422
            except http_requests.exceptions.RequestException as e:
                return jsonify({
                    "status": "error",
                    "message": f"Feed-URL ungültig: {str(e)}"
                }), 422

            # In DB anlegen
            db = get_database()
            try:
                new_feed = db.add_feed(name=name, url=url)
            except ValueError as e:
                # Duplikat (UniqueViolation)
                return jsonify({"status": "error", "message": str(e)}), 409

            # Timestamps konvertieren
            if isinstance(new_feed.get('created_at'), datetime):
                new_feed['created_at'] = new_feed['created_at'].isoformat()

            logger.info(f"Feed hinzugefügt via API: {name} ({url})")
            return jsonify({"status": "success", "feed": new_feed}), 201

        except Exception as e:
            logger.error(f"Fehler in POST /api/moodlight/feeds: {e}", exc_info=True)
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500


    @app.route('/api/moodlight/feeds/<int:feed_id>', methods=['DELETE'])
    def delete_moodlight_feed(feed_id):
        """
        RSS-Feed entfernen (FEED-04)

        Gibt 204 bei Erfolg, 404 wenn ID nicht gefunden.
        """
        try:
            db = get_database()
            deleted = db.delete_feed(feed_id)

            if not deleted:
                return jsonify({"status": "error", "message": f"Feed nicht gefunden: ID={feed_id}"}), 404

            logger.info(f"Feed entfernt via API: ID={feed_id}")
            return '', 204

        except Exception as e:
            logger.error(f"Fehler in DELETE /api/moodlight/feeds/{feed_id}: {e}", exc_info=True)
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500

    logger.info("Moodlight API-Endpunkte registriert")
