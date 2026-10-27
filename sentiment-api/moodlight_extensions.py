# -*- coding: utf-8 -*-
"""Moodlight-API-Endpunkte mit Datenbank und Redis-Cache."""

import ipaddress
import logging
import socket
from urllib.parse import urlparse, urljoin
from flask import jsonify, request, session
from functools import wraps
from datetime import datetime, timedelta, timezone
from database import get_database, get_cache, compute_led_index
from shared_config import get_sentiment_category as get_category_from_score
from shared_config import CACHE_KEY_CURRENT, CACHE_KEY_CURRENT_LEGACY
from background_worker import get_background_worker
import time
import requests as http_requests
import anthropic as anthropic_sdk

logger = logging.getLogger(__name__)


def _validate_feed_url_ssrf(url: str) -> str | None:
    """SSRF-Guard fuer neu angelegte Feed-URLs."""
    try:
        parsed = urlparse(url)
    except Exception:
        return "Feed-URL konnte nicht geparst werden"

    if parsed.scheme not in ('http', 'https'):
        return "Nur http/https-URLs sind erlaubt"

    hostname = parsed.hostname
    if not hostname:
        return "Feed-URL enthaelt keinen gueltigen Hostnamen"

    try:
        addr_infos = socket.getaddrinfo(hostname, None)
    except socket.gaierror:
        return f"Hostname konnte nicht aufgeloest werden: {hostname}"

    for info in addr_infos:
        ip_str = info[4][0]
        try:
            ip = ipaddress.ip_address(ip_str)
        except ValueError:
            continue
        if (ip.is_private or ip.is_loopback or ip.is_link_local
                or ip.is_reserved or ip.is_multicast or ip.is_unspecified):
            return f"Feed-URL zeigt auf nicht erlaubte interne Adresse: {ip_str}"

    return None


def _safe_get_following_redirects(url: str, timeout: int = 5, max_redirects: int = 5):
    """Holt eine URL und prueft dabei JEDES Redirect-Ziel erneut gegen den SSRF-Guard."""
    current = url
    for _ in range(max_redirects + 1):
        ssrf_error = _validate_feed_url_ssrf(current)
        if ssrf_error:
            return None, ssrf_error

        resp = http_requests.get(current, timeout=timeout, allow_redirects=False, stream=True)

        if resp.is_redirect or resp.is_permanent_redirect:
            location = resp.headers.get('Location', '')
            resp.close()
            if not location:
                return None, "Feed-URL antwortet mit Redirect ohne Ziel"
            # Relative Redirects gegen die aktuelle URL aufloesen
            current = urljoin(current, location)
            continue

        return resp, None

    return None, f"Feed-URL hat mehr als {max_redirects} Weiterleitungen"


def api_login_required(f):
    """Decorator für API-Endpoints: gibt 401 JSON zurück statt Redirect."""
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if not session.get('authenticated'):
            return jsonify({"status": "error", "message": "Nicht authentifiziert"}), 401
        return f(*args, **kwargs)
    return decorated_function


# Nur der Score, die Farben konfiguriert die Lampe selbst.


CURRENT_CACHE_KEY = CACHE_KEY_CURRENT  # Alias fuer Rueckwaertskompatibilitaet innerhalb dieses Moduls


def _get_next_update_minutes() -> int:
    """Ermittelt das tatsächliche Worker-Intervall in Minuten, da reconfigure() es zur Laufzeit ändert."""
    worker = get_background_worker()
    if worker is not None:
        return max(1, worker.interval_seconds // 60)
    return 30


def register_moodlight_endpoints(app):
    """Registriere alle neuen Moodlight-API-Endpunkte."""

    # ===== MOODLIGHT CURRENT ENDPOINT (Hauptendpunkt für Geräte) =====
    @app.route('/api/moodlight/current', methods=['GET'])
    def get_moodlight_current():
        """Endpunkt für Moodlight-Geräte mit Redis-Cache (5 Min TTL) und Geräte-Tracking."""
        start_time = time.time()

        # Device-ID aus Header holen (für Tracking)
        device_id = request.headers.get('X-Device-ID', 'unknown')
        device_name = request.headers.get('X-Device-Name')
        firmware_version = request.headers.get('X-Firmware-Version')
        ip_address = request.remote_addr

        try:
            cache = get_cache()

            cached_data = cache.get(CURRENT_CACHE_KEY)
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

            logger.debug(f"Cache MISS für /api/moodlight/current (Device: {device_id})")

            db = get_database()
            latest = db.get_latest_sentiment()

            if not latest:
                return jsonify({
                    "status": "error",
                    "message": "Keine Sentiment-Daten verfügbar"
                }), 503

            sentiment_score = latest['sentiment_score']

            # Dynamische Schwellwerte aus DB (7-Tage-Fenster)
            thresholds = db.get_score_percentiles(days=7)
            led_index = compute_led_index(sentiment_score, thresholds)

            # Perzentil-Position 0.0-1.0, linear zwischen min und max
            score_range = thresholds['max'] - thresholds['min']
            if score_range > 0:
                percentile = round((sentiment_score - thresholds['min']) / score_range, 3)
                percentile = max(0.0, min(1.0, percentile))
            else:
                percentile = 0.5

            response = {
                "status": "success",
                "timestamp": latest['timestamp'].isoformat(),
                "sentiment": round(sentiment_score, 4),
                "raw_score": round(sentiment_score, 4),
                "category": latest['category'],
                "led_index": led_index,
                "percentile": percentile,
                "thresholds": {
                    "p20": thresholds['p20'],
                    "p40": thresholds['p40'],
                    "p60": thresholds['p60'],
                    "p80": thresholds['p80'],
                    "fallback": thresholds['fallback']
                },
                "historical": {
                    "min": thresholds['min'],
                    "max": thresholds['max'],
                    "median": thresholds['median'],
                    "count": thresholds['count'],
                    "window_days": 7
                },
                "headlines_analyzed": latest.get('headlines_analyzed', 0),
                "next_update_minutes": _get_next_update_minutes(),
                "cached": False
            }

            # 5 Minuten im Redis-Cache
            cache.set(CURRENT_CACHE_KEY, response, ttl=300)

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
        """Sentiment-Historie abrufen."""
        try:
            all_param = request.args.get('all', '').lower() == 'true'
            hours_param = request.args.get('hours', type=int)
            from_param = request.args.get('from')
            to_param = request.args.get('to')
            # limit deckeln — verhindert übermäßig große Result-Sets/Speicherverbrauch
            limit = min(request.args.get('limit', 50000, type=int), 50000)

            # timezone-aware UTC statt naiver datetime.utcnow()
            from_time = None
            to_time = None

            if all_param:
                to_time = datetime.now(timezone.utc)
                logger.info("History: Hole ALLE Daten (kein Zeitlimit)")
            elif hours_param:
                from_time = datetime.now(timezone.utc) - timedelta(hours=hours_param)
                to_time = datetime.now(timezone.utc)
            elif from_param:
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
                from_time = datetime.now(timezone.utc) - timedelta(days=7)
                to_time = datetime.now(timezone.utc)

            # Key enthält alle Parameter, damit verschiedene Anfragen nicht kollidieren.
            cache_key = f"moodlight:history:{all_param}:{hours_param}:{from_param}:{to_param}:{limit}"
            cache = get_cache()
            cached = cache.get(cache_key)
            if cached is not None:
                return jsonify(cached)

            db = get_database()
            history = db.get_sentiment_history(from_time, to_time, limit)

            for entry in history:
                if isinstance(entry.get('timestamp'), datetime):
                    entry['timestamp'] = entry['timestamp'].isoformat()

            response = {
                "count": len(history),
                "data": history
            }
            cache.set(cache_key, response, ttl=120)
            return jsonify(response)

        except Exception as e:
            logger.error(f"Fehler in /api/moodlight/history: {e}", exc_info=True)
            return jsonify({"error": "Interner Serverfehler"}), 500

    # ===== TREND ENDPOINT =====
    @app.route('/api/moodlight/trend', methods=['GET'])
    def get_moodlight_trend():
        """Trend-Analyse: aktuell gegen 24h- und 7-Tage-Durchschnitt."""
        try:
            # Redis-Cache mit 120 s TTL
            cache_key = "moodlight:trend"
            cache = get_cache()
            cached = cache.get(cache_key)
            if cached is not None:
                return jsonify(cached)

            db = get_database()

            latest = db.get_latest_sentiment()
            if not latest:
                return jsonify({"error": "Keine Daten verfügbar"}), 503

            current_score = latest['sentiment_score']

            stats = db.get_statistics()

            avg_24h = float(stats.get('avg_sentiment_24h', 0))
            avg_7d = float(stats.get('avg_sentiment_7d', 0))

            change_24h = current_score - avg_24h

            if abs(change_24h) < 0.05:
                trend = "stabil"
            elif change_24h > 0:
                trend = "verbessernd"
            else:
                trend = "verschlechternd"

            response = {
                "status": "success",
                "current": round(current_score, 2),
                "avg_24h": round(avg_24h, 2),
                "avg_7d": round(avg_7d, 2),
                "change_24h": round(change_24h, 2),
                "trend": trend,
                "category": latest['category'],
                "timestamp": latest['timestamp'].isoformat()
            }
            cache.set(cache_key, response, ttl=120)
            return jsonify(response)

        except Exception as e:
            logger.error(f"Fehler in /api/moodlight/trend: {e}", exc_info=True)
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500

    # ===== STATISTICS ENDPOINT =====
    @app.route('/api/moodlight/stats', methods=['GET'])
    def get_moodlight_stats():
        """System-Statistiken: Geräte, Analysen, Uptime."""
        try:
            # Redis-Cache mit 120 s TTL
            cache_key = "moodlight:stats"
            cache = get_cache()
            cached = cache.get(cache_key)
            if cached is not None:
                return jsonify(cached)

            db = get_database()
            stats = db.get_statistics()

            response = {
                "status": "success",
                "total_devices": stats.get('total_devices', 0),
                "active_devices_24h": stats.get('active_devices_24h', 0),
                "total_analyses": stats.get('total_analyses', 0),
                "last_update": stats.get('last_update'),
                "avg_sentiment_24h": float(stats.get('avg_sentiment_24h', 0)),
                "avg_sentiment_7d": float(stats.get('avg_sentiment_7d', 0))
            }
            cache.set(cache_key, response, ttl=120)
            return jsonify(response)

        except Exception as e:
            logger.error(f"Fehler in /api/moodlight/stats: {e}", exc_info=True)
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500

    # ===== DEVICES ENDPOINT =====
    @app.route('/api/moodlight/devices', methods=['GET'])
    def get_moodlight_devices():
        """Liste aller aktiven Geräte (last_seen < 2h)."""
        try:
            hours = request.args.get('hours', 2, type=int)

            db = get_database()
            devices = db.get_active_devices(hours=hours)

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
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500

    # ===== CACHE CLEAR ENDPOINT (Admin) =====
    @app.route('/api/moodlight/cache/clear', methods=['POST'])
    @api_login_required
    def clear_moodlight_cache():
        """Lösche Redis-Cache (für manuelle Aktualisierung)."""
        try:
            cache = get_cache()
            cache.delete(CURRENT_CACHE_KEY)
            cache.delete(CACHE_KEY_CURRENT_LEGACY)  # Alten Key beim nächsten Clear-Aufruf bereinigen
            # Auch die Caches für history, trend, stats und feeds-trends löschen
            cache.delete_pattern("moodlight:history:*")
            cache.delete_pattern("moodlight:trend")
            cache.delete_pattern("moodlight:stats")
            cache.delete_pattern("moodlight:feeds:trends:*")

            logger.info("Moodlight Cache manuell gelöscht")

            return jsonify({
                "status": "success",
                "message": "Cache gelöscht"
            })

        except Exception as e:
            logger.error(f"Fehler beim Cache-Löschen: {e}", exc_info=True)
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500

    # ===== MANUELLER ANALYSE-TRIGGER =====
    @app.route('/api/moodlight/analyze/trigger', methods=['POST'])
    @api_login_required
    def trigger_moodlight_analysis():
        """Startet sofortige Sentiment-Analyse (manueller Trigger vom Dashboard)."""
        try:
            worker = get_background_worker()
            if worker is None:
                return jsonify({
                    "status": "error",
                    "message": "Background Worker nicht verfügbar"
                }), 503

            with worker.app.app_context():
                result = worker.trigger()

            logger.info(f"Manueller Trigger abgeschlossen: score={result['sentiment_score']:.3f}")

            return jsonify({
                "status": "success",
                "sentiment_score": result['sentiment_score'],
                "category": result['category'],
                "headlines_analyzed": result['headlines_analyzed'],
                "source_count": result['source_count'],
                "duration_seconds": result['duration_seconds']
            })

        except RuntimeError as e:
            logger.error(f"Manueller Trigger fehlgeschlagen (erwartet): {e}")
            return jsonify({"status": "error", "message": str(e)}), 422
        except Exception as e:
            logger.error(f"Fehler in POST /api/moodlight/analyze/trigger: {e}", exc_info=True)
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500

    # ===== FEED-VERWALTUNG ENDPOINTS =====

    @app.route('/api/moodlight/feeds', methods=['GET'])
    def get_moodlight_feeds():
        """Alle RSS-Feeds mit Status zurückgeben."""
        try:
            db = get_database()
            feeds = db.get_all_feeds()

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
    @api_login_required
    def add_moodlight_feed():
        """Neuen RSS-Feed hinzufügen mit URL-Validierung."""
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

            # SSRF-Schutz: keine Feeds auf interne Ziele; Redirects prüft _safe_get_following_redirects je Hop.
            ssrf_error = _validate_feed_url_ssrf(url)
            if ssrf_error:
                return jsonify({"status": "error", "message": ssrf_error}), 422

            # Redirects von Hand verfolgen, damit jeder Hop den SSRF-Schutz durchläuft.
            try:
                resp, redirect_error = _safe_get_following_redirects(url, timeout=5)
                if redirect_error:
                    return jsonify({"status": "error", "message": redirect_error}), 422
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
                # Fehlertext nur ins Log, er enthält Hostnamen und Ports.
                logger.warning(f"Feed-URL nicht abrufbar ({url}): {e}")
                return jsonify({
                    "status": "error",
                    "message": "Feed-URL ungültig oder nicht abrufbar"
                }), 422

            db = get_database()
            try:
                new_feed = db.add_feed(name=name, url=url)
            except ValueError as e:
                # Duplikat (UniqueViolation)
                return jsonify({"status": "error", "message": str(e)}), 409

            if isinstance(new_feed.get('created_at'), datetime):
                new_feed['created_at'] = new_feed['created_at'].isoformat()

            logger.info(f"Feed hinzugefügt via API: {name} ({url})")
            return jsonify({"status": "success", "feed": new_feed}), 201

        except Exception as e:
            logger.error(f"Fehler in POST /api/moodlight/feeds: {e}", exc_info=True)
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500


    @app.route('/api/moodlight/feeds/<int:feed_id>', methods=['DELETE'])
    @api_login_required
    def delete_moodlight_feed(feed_id):
        """RSS-Feed entfernen."""
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

    # ===== HEADLINES ENDPOINT =====
    @app.route('/api/moodlight/headlines', methods=['GET'])
    def get_moodlight_headlines():
        """Letzte analysierte Headlines mit Einzel-Scores und Feed-Zuordnung."""
        try:
            limit = min(request.args.get('limit', 50, type=int), 500)
            if limit < 1:
                limit = 50

            db = get_database()
            headlines = db.get_recent_headlines(limit=limit)

            return jsonify({
                "status": "success",
                "count": len(headlines),
                "headlines": headlines
            })

        except Exception as e:
            logger.error(f"Fehler in /api/moodlight/headlines: {e}", exc_info=True)
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500

    # ===== SETTINGS ENDPOINTS =====

    @app.route('/api/moodlight/settings', methods=['GET'])
    @api_login_required
    def get_moodlight_settings():
        """Aktuelle Backend-Einstellungen zurückgeben."""
        try:
            db = get_database()
            settings = db.get_all_settings()

            # analysis_interval: Sekunden für Anzeige
            interval_seconds = int(settings.get('analysis_interval', '1800'))

            # anthropic_api_key maskieren
            raw_key = settings.get('anthropic_api_key', '')
            if raw_key and len(raw_key) > 10:
                masked_key = raw_key[:10] + '****'
            elif raw_key:
                masked_key = '****'
            else:
                masked_key = ''

            # admin_password_hash nie zurückgeben — nur ob gesetzt
            has_password = bool(settings.get('admin_password_hash', '').strip())

            return jsonify({
                "status": "success",
                "settings": {
                    "analysis_interval_seconds": interval_seconds,
                    "analysis_interval_minutes": round(interval_seconds / 60, 1),
                    "headlines_per_source": int(settings.get('headlines_per_source', '1')),
                    "anthropic_api_key": masked_key,
                    "has_admin_password": has_password
                }
            })

        except Exception as e:
            logger.error(f"Fehler in GET /api/moodlight/settings: {e}", exc_info=True)
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500

    @app.route('/api/moodlight/settings', methods=['PUT'])
    @api_login_required
    def update_moodlight_settings():
        """Einstellungen aktualisieren und sofort wirksam machen."""
        try:
            data = request.get_json(silent=True)
            if not data:
                return jsonify({"status": "error", "message": "JSON-Body erwartet"}), 400

            db = get_database()
            worker = get_background_worker()
            changes = []

            # --- analysis_interval ---
            if 'analysis_interval_minutes' in data:
                try:
                    minutes = float(data['analysis_interval_minutes'])
                    if minutes < 1:
                        return jsonify({"status": "error", "message": "analysis_interval_minutes muss >= 1 sein"}), 400
                    seconds = int(minutes * 60)
                    db.set_setting('analysis_interval', str(seconds))
                    if worker:
                        worker.reconfigure(interval_seconds=seconds)
                    changes.append(f"analysis_interval={seconds}s")
                except (ValueError, TypeError):
                    return jsonify({"status": "error", "message": "analysis_interval_minutes muss eine Zahl sein"}), 400

            # --- headlines_per_source ---
            if 'headlines_per_source' in data:
                try:
                    count = int(data['headlines_per_source'])
                    if count < 1:
                        return jsonify({"status": "error", "message": "headlines_per_source muss >= 1 sein"}), 400
                    db.set_setting('headlines_per_source', str(count))
                    if worker:
                        worker.reconfigure(headlines_per_source=count)
                    changes.append(f"headlines_per_source={count}")
                except (ValueError, TypeError):
                    return jsonify({"status": "error", "message": "headlines_per_source muss eine ganze Zahl sein"}), 400

            # --- anthropic_api_key ---
            if 'anthropic_api_key' in data:
                new_key = str(data['anthropic_api_key']).strip()
                if new_key:  # Leer = unverändert
                    db.set_setting('anthropic_api_key', new_key)
                    # anthropic_client in app.py neu initialisieren
                    try:
                        import app as main_app
                        main_app.anthropic_client = anthropic_sdk.Anthropic(api_key=new_key, timeout=45.0)
                        logger.info("Anthropic Client mit neuem API Key neu initialisiert")
                    except Exception as e:
                        logger.error(f"Fehler beim Neu-Initialisieren des Anthropic Client: {e}")
                    changes.append("anthropic_api_key=****")

            # --- admin_password ---
            if 'admin_password' in data:
                new_password = str(data.get('admin_password', '')).strip()
                current_password = str(data.get('current_password', '')).strip()

                if not new_password:
                    return jsonify({"status": "error", "message": "admin_password darf nicht leer sein"}), 400
                if not current_password:
                    return jsonify({"status": "error", "message": "current_password ist Pflichtfeld beim Passwort-Ändern"}), 400

                try:
                    import app as main_app
                    from werkzeug.security import check_password_hash, generate_password_hash
                    if not main_app._admin_password_hash or not check_password_hash(main_app._admin_password_hash, current_password):
                        return jsonify({"status": "error", "message": "Aktuelles Passwort falsch"}), 403
                    new_hash = generate_password_hash(new_password)
                    db.set_setting('admin_password_hash', new_hash)
                    main_app._admin_password_hash = new_hash
                    changes.append("admin_password=****")
                except Exception as e:
                    logger.error(f"Fehler beim Passwort-Ändern: {e}")
                    return jsonify({"status": "error", "message": "Fehler beim Passwort-Ändern"}), 500

            if not changes:
                return jsonify({"status": "ok", "message": "Keine Änderungen angegeben"}), 200

            logger.info(f"Einstellungen aktualisiert: {', '.join(changes)}")
            return jsonify({
                "status": "success",
                "message": f"Einstellungen gespeichert: {', '.join(changes)}",
                "changes": changes
            })

        except Exception as e:
            logger.error(f"Fehler in PUT /api/moodlight/settings: {e}", exc_info=True)
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500

    # ===== FEED-TRENDS ENDPOINT =====
    @app.route('/api/moodlight/feeds/trends', methods=['GET'])
    def get_moodlight_feed_trends():
        """Durchschnitts-Score pro Feed für ein Zeitfenster."""
        try:
            days_param = request.args.get('days', 7, type=int)
            if days_param not in (7, 30):
                days_param = 7

            # Redis-Cache mit 120 s TTL, Key mit days-Parameter
            cache_key = f"moodlight:feeds:trends:{days_param}"
            cache = get_cache()
            cached = cache.get(cache_key)
            if cached is not None:
                return jsonify(cached)

            db = get_database()
            feeds = db.get_feed_trends(days=days_param)

            response = {
                "status": "success",
                "days": days_param,
                "count": len(feeds),
                "feeds": feeds
            }
            cache.set(cache_key, response, ttl=120)
            return jsonify(response)

        except Exception as e:
            logger.error(f"Fehler in GET /api/moodlight/feeds/trends: {e}", exc_info=True)
            return jsonify({"status": "error", "message": "Interner Serverfehler"}), 500

    logger.info("Moodlight API-Endpunkte registriert")
