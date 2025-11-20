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
import time

logger = logging.getLogger(__name__)


# Farben werden auf dem Moodlight selbst konfiguriert (Teil der User-Config)
# Wir liefern nur den reinen Sentiment-Score


def get_category_from_score(score: float) -> str:
    """Bestimmt Kategorie aus Sentiment-Score"""
    if score >= 0.30:
        return "sehr positiv"
    elif score >= 0.10:
        return "positiv"
    elif score >= -0.20:
        return "neutral"
    elif score >= -0.50:
        return "negativ"
    else:
        return "sehr negativ"


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
            hours: Anzahl Stunden zurück (einfacher Modus)
            from: ISO8601 timestamp (default: -24h)
            to: ISO8601 timestamp (default: now)
            limit: Max. Anzahl Einträge (default: 1000)
        """
        try:
            # Parameter parsen
            hours_param = request.args.get('hours', type=int)
            from_param = request.args.get('from')
            to_param = request.args.get('to')
            limit = request.args.get('limit', 1000, type=int)

            # Zeitstempel konvertieren
            from_time = None
            to_time = None

            # Wenn 'hours' Parameter vorhanden, berechne from_time
            if hours_param:
                from_time = datetime.utcnow() - timedelta(hours=hours_param)
                to_time = datetime.utcnow()
            else:
                if from_param:
                    try:
                        from_time = datetime.fromisoformat(from_param.replace('Z', '+00:00'))
                    except ValueError:
                        return jsonify({"error": "Ungültiges 'from' Format (ISO8601 erwartet)"}), 400

                if to_param:
                    try:
                        to_time = datetime.fromisoformat(to_param.replace('Z', '+00:00'))
                    except ValueError:
                        return jsonify({"error": "Ungültiges 'to' Format (ISO8601 erwartet)"}), 400

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

    logger.info("Moodlight API-Endpunkte registriert")
