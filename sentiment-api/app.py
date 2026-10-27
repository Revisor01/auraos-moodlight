# -*- coding: utf-8 -*-
from flask import Flask, jsonify, request, render_template, session, redirect, url_for
from werkzeug.security import generate_password_hash, check_password_hash
from functools import wraps
from datetime import timedelta
from typing import Optional
import feedparser
import requests
import logging
from collections import Counter
import os
import re
import math
import time
import threading
import anthropic
from anthropic import Anthropic, APIConnectionError, RateLimitError, APIStatusError
from shared_config import get_sentiment_category

app = Flask(__name__)

# SECRET_KEY ist Pflicht, sonst wären Sessions mit einem bekannten Schlüssel signiert.
_secret_key = os.environ.get('SECRET_KEY', '').strip()
if not _secret_key:
    raise RuntimeError("SECRET_KEY muss gesetzt sein")
app.secret_key = _secret_key
app.permanent_session_lifetime = timedelta(hours=24)
# Kein SESSION_COOKIE_SECURE, die Seite läuft über http.
app.config['SESSION_COOKIE_SAMESITE'] = 'Lax'

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)


def load_settings_from_db():
    """Lade Einstellungen aus PostgreSQL; fällt auf Env-Variablen zurück wenn DB-Wert fehlt oder leer."""
    from database import get_database
    try:
        db = get_database()
        result = {}

        # analysis_interval: DB-Wert in Sekunden, Env-Fallback: 1800
        val = db.get_setting('analysis_interval')
        try:
            result['analysis_interval'] = int(val) if val and int(val) > 0 else 1800
        except (ValueError, TypeError):
            result['analysis_interval'] = 1800

        # headlines_per_source: DB-Wert, Env-Fallback: DEFAULT_HEADLINES_PER_SOURCE
        val = db.get_setting('headlines_per_source')
        try:
            env_val = int(os.environ.get('DEFAULT_HEADLINES_PER_SOURCE', '1'))
            result['headlines_per_source'] = int(val) if val and int(val) > 0 else env_val
        except (ValueError, TypeError):
            result['headlines_per_source'] = 1

        # anthropic_api_key: DB-Wert, Env-Fallback: ANTHROPIC_API_KEY
        val = db.get_setting('anthropic_api_key')
        env_key = os.environ.get('ANTHROPIC_API_KEY', '')
        result['anthropic_api_key'] = val.strip() if val and val.strip() else env_key

        # admin_password_hash: DB-Wert, Env-Fallback: Hash von ADMIN_PASSWORD
        val = db.get_setting('admin_password_hash')
        if val and val.strip():
            result['admin_password_hash'] = val.strip()
        else:
            env_pwd = os.environ.get('ADMIN_PASSWORD', '')
            result['admin_password_hash'] = generate_password_hash(env_pwd) if env_pwd else None

        logging.info(f"Einstellungen aus DB geladen: interval={result['analysis_interval']}s, "
                     f"headlines={result['headlines_per_source']}, "
                     f"api_key={'gesetzt' if result['anthropic_api_key'] else 'FEHLT'}")
        return result

    except Exception as e:
        logging.warning(f"DB-Einstellungen konnten nicht geladen werden, nutze Env-Variablen: {e}")
        env_pwd = os.environ.get('ADMIN_PASSWORD', '')
        return {
            'analysis_interval': 1800,
            'headlines_per_source': int(os.environ.get('DEFAULT_HEADLINES_PER_SOURCE', '1')),
            'anthropic_api_key': os.environ.get('ANTHROPIC_API_KEY', ''),
            'admin_password_hash': generate_password_hash(env_pwd) if env_pwd else None,
        }


# --- Admin-Passwort und API-Keys aus DB laden (Env-Variablen als Fallback) ---
_startup_settings = load_settings_from_db()
_admin_password_hash = _startup_settings['admin_password_hash']
if not _admin_password_hash:
    logging.warning("ADMIN_PASSWORD nicht gesetzt (weder DB noch Env) — Login deaktiviert!")

DEFAULT_HEADLINES_PER_SOURCE_MAIN = 2
DEFAULT_HEADLINES_PER_SOURCE_TOTAL = 1
# DEFAULT_HEADLINES_FROM_ENV aus DB oder Env
DEFAULT_HEADLINES_FROM_ENV = _startup_settings['headlines_per_source']

# --- Anthropic Client initialisieren ---
anthropic_api_key = _startup_settings['anthropic_api_key']
anthropic_client = None
if anthropic_api_key:
    try:
        anthropic_client = Anthropic(api_key=anthropic_api_key, timeout=45.0)
        logging.info("Anthropic Client erfolgreich initialisiert.")
    except Exception as e:
        logging.error(f"Fehler bei der Initialisierung des Anthropic Clients: {e}")
else:
    logging.error("############################################################")
    logging.error("FEHLER: Anthropic API Key nicht verfügbar (weder DB noch Env)!")
    logging.error("Sentiment-Analyse wird nicht funktionieren.")
    logging.error("############################################################")


# --- Authentifizierungs-Decorator ---
def login_required(f):
    """Decorator: Leitet auf /login weiter wenn keine aktive Session vorhanden."""
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if not session.get('authenticated'):
            return redirect(url_for('login_page'))
        return f(*args, **kwargs)
    return decorated_function


# --- Funktion zur Sentiment-Analyse mit Anthropic API ---
def analyze_sentiment_claude(headlines_batch: list) -> Optional[list]:
    """Analysiert Headlines per Anthropic API."""
    if not anthropic_client:
        logging.error("Anthropic Client nicht verfügbar in analyze_sentiment_claude.")
        return None
    if not headlines_batch:
        return []

    # Kalibrierter Prompt mit Ankerpunkten und Anti-Bias-Anweisung
    prompt_lines = [
        "Analysiere das Sentiment jeder der folgenden deutschen Nachrichtenschlagzeilen.",
        "Gib für jede Schlagzeile eine Fließkommazahl zwischen -1.0 und +1.0 zurück.",
        "",
        "KALIBRIERUNG — diese Beispiele zeigen die erwartete Skala:",
        "- 'Flugzeugabsturz mit 200 Toten' → -0.9",
        "- 'Wirtschaftskrise verschärft sich' → -0.6",
        "- 'Regierung schließt neue Haushaltslücke' → -0.3",
        "- 'Wetterbericht für die Woche' → 0.0",
        "- 'Forschungsergebnisse veröffentlicht' → 0.0",
        "- 'Wirtschaftswachstum über Erwartungen' → +0.5",
        "- 'Friedensverhandlungen beginnen' → +0.7",
        "- 'Historischer Durchbruch bei Krebstherapie' → +0.9",
        "",
        "WICHTIG:",
        "- Bewerte die TONALITÄT der Schlagzeile, nicht das allgemeine Thema.",
        "- Sachliche Berichte über Probleme sind NICHT automatisch -0.5 oder schlechter.",
        "- Regulierung, Verbraucherschutz, neue Gesetze zum Schutz der Bürger → POSITIV (+0.2 bis +0.4).",
        "- Tarifeinigungen, Kompromisse, gelöste Konflikte → POSITIV (+0.3 bis +0.6).",
        "- Nutze den vollen Bereich: positive Nachrichten sollen positive Werte bekommen.",
        "- Im Zweifel: ausgewogene Einschätzung, kein negativer Bias.",
        "",
        "Format: Nur \"Nummer: Score\" pro Zeile (z.B. \"1: -0.3\"). Keine Erklärungen.",
        "",
        "Schlagzeilen:"
    ]
    for i, headline in enumerate(headlines_batch):
        prompt_lines.append(f"{i+1}: {headline}")
    full_prompt = "\n".join(prompt_lines)

    scores = [0.0] * len(headlines_batch)

    try:
        logging.info(f"Sende {len(headlines_batch)} Headlines zur Analyse an Anthropic API (claude-haiku-4-5-20251001)...")
        response = anthropic_client.messages.create(
            model="claude-haiku-4-5-20251001",
            # Zu knappe max_tokens schneiden Scores ab und verzerren zu neutral.
            max_tokens=max(1024, len(headlines_batch) * 25),
            # temperature=0: deterministisches Scoring statt zufälliger Variation
            temperature=0,
            messages=[{"role": "user", "content": full_prompt}]
        )
        raw_response_content = response.content[0].text.strip()
        logging.info("Antwort von Anthropic API erhalten.")
        logging.debug(f"Anthropic Raw Response:\n{raw_response_content}")

        lines = raw_response_content.split('\n')
        score_pattern = re.compile(r"^\s*(\d+):\s*([-+]?\d*\.?\d+)\s*$")
        parsed_count = 0
        for line in lines:
            match = score_pattern.match(line)
            if match:
                try:
                    index = int(match.group(1)) - 1
                    score = float(match.group(2))
                    if 0 <= index < len(scores):
                        scores[index] = max(-1.0, min(1.0, score))
                        parsed_count += 1
                        logging.debug(f"  Geparster Score für Index {index}: {scores[index]}")
                    else:
                        logging.warning(f"Geparster Index {index+1} außerhalb des Bereichs ({len(scores)} Headlines). Zeile: '{line}'")
                except (ValueError, IndexError) as parse_error:
                    logging.warning(f"Fehler beim Parsen der Score-Zeile '{line}': {parse_error}")
            elif line.strip():
                logging.warning(f"Unerwartetes Format in Antwortzeile von Anthropic: '{line}'")

        logging.info(f"Erfolgreich {parsed_count} Scores aus der Anthropic-Antwort geparst.")
        if parsed_count != len(headlines_batch):
            # Teilparse verwerfen, statt fehlende Scores still auf 0.0 zu lassen.
            logging.warning(
                f"Nicht alle Scores geparst! Erwartet: {len(headlines_batch)}, "
                f"Geparsed: {parsed_count}. Analyse-Zyklus wird verworfen."
            )
            return None

        return scores

    except APIConnectionError as e:
        logging.error(f"Anthropic API nicht erreichbar: {e}")
        return None
    except RateLimitError as e:
        logging.error(f"Anthropic Rate-Limit erreicht: {e}")
        return None
    except APIStatusError as e:
        # Body mitloggen, sonst bleibt die Fehlerursache unsichtbar.
        try:
            response_body = e.response.text
        except Exception:
            response_body = str(e.response)
        logging.error(f"Anthropic API Fehler {e.status_code}: {response_body}")
        return None
    except Exception as e:
        logging.error(f"Unerwarteter Fehler bei der Anthropic Analyse: {e}")
        return None

# --- Haupt-Analysefunktion ---
def analyze_headlines_batch(headlines: list) -> Optional[dict]:
    """Analysiert Headlines mit Anthropic (Claude Haiku), berechnet Statistiken und gibt den gewichteten Mood-Score als 'total_sentiment' zurück."""
    results = []
    sentiment_distribution = Counter()
    total_numeric_sentiment_sum = 0.0 # Wird nur noch für Logging gebraucht
    analyzed_count = 0

    if not headlines:
        logging.warning("analyze_headlines_batch aufgerufen mit leerer Headline-Liste.")
        return {
            "results": [], "total_sentiment": 0.0, # Default Wert für total_sentiment
            "statistics": {"analyzed_count": 0, "sentiment_distribution": {}}
        }

    headline_texts = []
    original_indices = []
    for i, h in enumerate(headlines):
        if h and h.get('headline') and h.get('headline').strip():
            headline_texts.append(h['headline'].strip())
            original_indices.append(i)

    if not headline_texts:
        logging.warning("Keine gültigen (nicht-leeren) Headline-Texte zum Analysieren gefunden.")
        return {
            "results": [], "total_sentiment": 0.0, # Default Wert für total_sentiment
            "statistics": {"analyzed_count": 0, "sentiment_distribution": {}}
        }

    sentiment_scores = analyze_sentiment_claude(headline_texts)

    if sentiment_scores is None:
        # Zyklus verwerfen, kein Ergebnis mit Score 0.0.
        logging.error("Sentiment-Analyse fehlgeschlagen — kein verwertbares Ergebnis (siehe vorherige Fehlermeldung).")
        return None

    for i, score in enumerate(sentiment_scores):
        if i < len(original_indices):
            original_idx = original_indices[i]
            original_headline_obj = headlines[original_idx]

            total_numeric_sentiment_sum += score # Für Debugging/Logging
            analyzed_count += 1

            strength = get_sentiment_category(score)

            sentiment_distribution[strength] += 1

            results.append({
                "headline": original_headline_obj['headline'].strip(),
                "source": original_headline_obj.get('source', 'unknown'),
                "feed_id": original_headline_obj.get('feed_id'),  # durchgereicht für DB-Persistenz
                "sentiment": score, # Der einzelne Score bleibt erhalten
                "strength": strength
            })
        else:
             logging.error(f"Interner Fehler: Score-Index {i} ohne korrespondierenden Original-Index.")

    average_sentiment_for_logging = total_numeric_sentiment_sum / analyzed_count if analyzed_count > 0 else 0.0
    logging.info(f"Analyse abgeschlossen. Verarbeitet: {analyzed_count}. (Durchschnittl. Sentiment für Logging: {average_sentiment_for_logging:.4f})")

    # Verstärkt für mehr Varianz in der LED-Darstellung
    weighted_mood_score = 0.0
    if analyzed_count > 0:
      all_scores = [item['sentiment'] for item in results]

      raw_average = sum(all_scores) / len(all_scores)

      # tanh-S-Kurve mit Faktor 2: -0.25 wird ca. -0.47, -0.50 ca. -0.76.
      amplification_factor = 2.0
      stretched = raw_average * amplification_factor

      # tanh begrenzt weich auf [-1, 1].
      weighted_mood_score = math.tanh(stretched)

      logging.info(f"Sentiment Berechnung: Roh-Durchschnitt={raw_average:.4f}, Verstärkt (x{amplification_factor})={weighted_mood_score:.4f}")

    else:
        logging.warning("Keine Headlines analysiert, total_sentiment bleibt 0.0")

    return {
        "results": results, # Detaillierte Ergebnisse pro Headline
        "total_sentiment": weighted_mood_score, # !!! HIER DIE ÄNDERUNG: Gewichteter Score ist jetzt total_sentiment !!!
        "statistics": {
            "analyzed_count": analyzed_count,
            "sentiment_distribution": dict(sentiment_distribution)
        }
    }


# --- Hilfsfunktion zur Bestimmung der Headlines pro Quelle ---
def get_headlines_per_source(route_default: int) -> int:
    """Ermittelt die Anzahl der zu holenden Headlines pro Quelle."""
    try:
        headlines_param = request.args.get('headlines_per_source', type=int)
        if headlines_param is not None and headlines_param > 0:
            # Deckel gegen unbegrenzte API-Kosten durch einen Request.
            capped = min(headlines_param, 10)
            if capped != headlines_param:
                logging.warning(f"headlines_per_source={headlines_param} auf Maximum {capped} gedeckelt.")
            logging.debug(f"Verwende 'headlines_per_source={capped}' aus URL-Parameter.")
            return capped
        elif headlines_param is not None: # Parameter da, aber <= 0
             logging.warning(f"Ungültiger Wert '{request.args.get('headlines_per_source')}' für URL-Parameter 'headlines_per_source'. Ignoriere.")
    except (ValueError, TypeError):
        logging.warning(f"Konnte URL-Parameter 'headlines_per_source' ('{request.args.get('headlines_per_source')}') nicht als Zahl lesen. Ignoriere.")
        pass # Ignorieren und mit nächster Priorität weitermachen

    if DEFAULT_HEADLINES_FROM_ENV is not None:
        logging.debug(f"Verwende {DEFAULT_HEADLINES_FROM_ENV} aus Umgebungsvariable DEFAULT_HEADLINES_PER_SOURCE.")
        return DEFAULT_HEADLINES_FROM_ENV

    logging.debug(f"Verwende Routen-Standardwert: {route_default} Headlines pro Quelle.")
    return route_default


@app.route('/')
def index():
    return redirect(url_for('dashboard'))

@app.route('/api/health')
def health_check():
  """Health-Check Endpoint mit Datenbank-Verbindungstest."""
  from database import get_database

  health = {
    "status": "healthy",
    "service": "AuraOS Moodlight API",
    "checks": {}
  }

  try:
    db = get_database()
    db_health = db.check_connection_health()
    health["checks"]["database"] = {
      "status": "healthy" if db_health.get('connected') else "unhealthy",
      "connected": db_health.get('connected', False),
      "pool_available": db_health.get('pool_available', False),
      "total_records": db_health.get('total_records', 0)
    }
    if not db_health.get('connected'):
      health["status"] = "unhealthy"
  except Exception as e:
    # Fehlertext nur ins Log, psycopg2-Fehler enthalten Hostnamen und Ports.
    logging.error(f"Health-Check: Datenbank nicht erreichbar: {e}", exc_info=True)
    health["checks"]["database"] = {"status": "unhealthy", "error": "Datenbank nicht erreichbar"}
    health["status"] = "unhealthy"

  health["checks"]["anthropic"] = {
    "status": "healthy" if anthropic_client else "unhealthy",
    "available": anthropic_client is not None
  }
  if not anthropic_client:
    health["status"] = "degraded"

  status_code = 200 if health["status"] == "healthy" else 503 if health["status"] == "unhealthy" else 200
  return jsonify(health), status_code

def _fetch_single_feed_for_news(source: str, url: str, num_headlines_per_source: int) -> tuple:
    """Holt Headlines von einem einzelnen Feed, ausgelagert für den ThreadPoolExecutor."""
    entries = []
    try:
        try:
            response = requests.get(url, timeout=10, headers={'User-Agent': 'WorldMoodAnalyzer/1.0'})
            response.raise_for_status()
            feed = feedparser.parse(response.content)
        except requests.exceptions.Timeout:
            logging.warning(f"Überspringe Feed von {source} wegen Timeout (10s).")
            return (source, entries, "Timeout", None)
        except requests.exceptions.RequestException as e:
            logging.warning(f"Fehler beim Abrufen von {source}: {e}")
            return (source, entries, "Abruffehler", None)

        if feed.bozo and isinstance(feed.bozo_exception, Exception):
            logging.warning(f"Überspringe fehlerhaften Feed (bozo) von {source}: {feed.bozo_exception}")
            return (source, entries, "Formatfehler", None)

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

        return (source, entries, None, None)

    except Exception as e:
        logging.error(f"Fehler beim Abrufen/Parsen des Feeds von {source}: {e}", exc_info=True)
        return (source, entries, None, f"Fehler bei {source}")


# --- Flask-Routen ---
@app.route('/api/news', methods=['GET'])
def get_news():
    if not anthropic_client: return jsonify({"status": "error", "message": "Sentiment-Analyse-Dienst nicht bereit (Anthropic Problem)."}), 503

    # RSS-Feeds aus der DB
    from database import get_database
    from concurrent.futures import ThreadPoolExecutor
    rss_feeds = {f['name']: f['url'] for f in get_database().get_active_feeds()}

    num_headlines_per_source = get_headlines_per_source(DEFAULT_HEADLINES_PER_SOURCE_MAIN)

    headlines = []; errors = []; processed_links = set(); skipped_feeds = []

    logging.info(f"Starte Feed-Abruf für /api/news (max. {num_headlines_per_source} Headlines/Quelle)...")
    total_headlines_found_before_filtering = 0

    # executor.map() hält die Reihenfolge der Feeds.
    feed_items = list(rss_feeds.items())
    with ThreadPoolExecutor(max_workers=6) as executor:
        results = executor.map(
            lambda item: _fetch_single_feed_for_news(item[0], item[1], num_headlines_per_source),
            feed_items
        )

        for source, entries, skip_reason, error in results:
            if error:
                errors.append(error)
                continue
            if skip_reason:
                skipped_feeds.append(f"{source} ({skip_reason})")
                continue

            headlines_from_source = 0
            for headline_text, link in entries:
                unique_key = link if link else headline_text
                if unique_key not in processed_links:
                    headlines.append({ "headline": headline_text, "source": source, "link": link })
                    total_headlines_found_before_filtering += 1
                    processed_links.add(unique_key)
                    headlines_from_source += 1
            logging.debug(f"  Quelle {source}: {headlines_from_source} Headlines übernommen.")

    if not headlines:
        logging.error("Keine Schlagzeilen nach Feed-Abruf gefunden.")
        return jsonify({
            "status": "error", "message": "Keine Schlagzeilen verfügbar",
            "errors": errors if errors else None,
            "skipped_feeds": skipped_feeds if skipped_feeds else None,
            "total_sentiment": 0.0, # Default Wert
            "statistics": {"analyzed_count": 0, "sentiment_distribution": {}}
        }), 500

    logging.info(f"Feed-Abruf abgeschlossen. Gefunden: {total_headlines_found_before_filtering}. Eindeutige zur Analyse: {len(headlines)}. Übersprungen: {len(skipped_feeds)}")
    analysis_result = analyze_headlines_batch(headlines)

    if analysis_result is None:
        # Kein Ersatzergebnis mit Score 0.0.
        return jsonify({
            "status": "error",
            "message": "Sentiment-Analyse fehlgeschlagen (Anthropic API nicht verfügbar oder Antwort nicht auswertbar)",
            "errors": errors if errors else None,
            "skipped_feeds": skipped_feeds if skipped_feeds else None,
            "total_sentiment": 0.0,
            "statistics": {"analyzed_count": 0, "sentiment_distribution": {}}
        }), 502

    return jsonify({
        "status": "success",
        "total_sentiment": analysis_result['total_sentiment'], # Hier steht jetzt der gewichtete Score
        "statistics": analysis_result['statistics'],
        "detailed_analysis": analysis_result['results'],
        "errors": errors if errors else None,
        "skipped_feeds": skipped_feeds if skipped_feeds else None
    })


# --- /api/news/total_sentiment ---
@app.route('/api/news/total_sentiment', methods=['GET'])
def get_total_sentiment():
    """Legacy-Endpoint für Moodlight-Geräte, liefert die gecachten Daten des Workers."""
    from database import get_database

    try:
        db = get_database()
        latest = db.get_latest_sentiment()

        if not latest:
            return jsonify({
                "status": "error",
                "message": "Keine Sentiment-Daten verfügbar",
                "total_sentiment": 0.0,
                "headlines_analyzed_count": 0
            }), 503

        # Kompatibles Format für AuraOS (total_sentiment statt sentiment)
        return jsonify({
            "status": "success",
            "total_sentiment": latest['sentiment_score'],
            "headlines_analyzed_count": latest.get('headlines_analyzed', 0)
        })

    except Exception as e:
        logging.error(f"Fehler in /api/news/total_sentiment: {e}")
        return jsonify({
            "status": "error",
            "message": "Interner Server-Fehler",
            "total_sentiment": 0.0,
            "headlines_analyzed_count": 0
        }), 500


# ===== MOODLIGHT EXTENSIONS =====
from moodlight_extensions import register_moodlight_endpoints
from firmware_mirror import register_firmware_endpoints
from background_worker import start_background_worker

register_moodlight_endpoints(app)
register_firmware_endpoints(app)

# Login-Sperre im Speicher: 5 Fehlversuche, 60 s Sperre, höchstens ~1000 IPs gegen verteilte Versuche.
_login_attempts_lock = threading.Lock()
_login_attempts = {}  # ip -> (fail_count, locked_until_epoch)
_LOGIN_MAX_ATTEMPTS = 5
_LOGIN_LOCKOUT_SECONDS = 60
_LOGIN_MAX_TRACKED_IPS = 1000


def _login_rate_limit_check(ip: str):
    """Gibt (allowed: bool, retry_after_seconds: int) zurück."""
    now = time.time()
    with _login_attempts_lock:
        entry = _login_attempts.get(ip)
        if entry:
            fail_count, locked_until = entry
            if locked_until and now < locked_until:
                return False, int(locked_until - now)
        return True, 0


def _login_rate_limit_record_failure(ip: str):
    now = time.time()
    with _login_attempts_lock:
        fail_count, locked_until = _login_attempts.get(ip, (0, 0))
        fail_count += 1
        if fail_count >= _LOGIN_MAX_ATTEMPTS:
            locked_until = now + _LOGIN_LOCKOUT_SECONDS
        _login_attempts[ip] = (fail_count, locked_until)

        # Speicher begrenzen — aelteste Eintraege entfernen wenn Limit ueberschritten
        if len(_login_attempts) > _LOGIN_MAX_TRACKED_IPS:
            oldest_ip = next(iter(_login_attempts))
            _login_attempts.pop(oldest_ip, None)


def _login_rate_limit_reset(ip: str):
    with _login_attempts_lock:
        _login_attempts.pop(ip, None)


# ===== LOGIN / LOGOUT =====
@app.route('/login', methods=['GET', 'POST'])
def login_page():
    """Login-Seite für das Backend-Interface."""
    error = None
    if request.method == 'POST':
        client_ip = request.remote_addr or 'unknown'
        allowed, retry_after = _login_rate_limit_check(client_ip)
        if not allowed:
            logging.warning(f"Login-Versuch von {client_ip} waehrend aktiver Sperre abgelehnt.")
            error = f'Zu viele Fehlversuche. Bitte in {retry_after}s erneut versuchen.'
            return render_template('login.html', error=error), 429

        password = request.form.get('password', '')
        if _admin_password_hash and check_password_hash(_admin_password_hash, password):
            _login_rate_limit_reset(client_ip)
            session['authenticated'] = True
            session.permanent = True
            # Nur relative Pfade: "//evil.com" wäre für Browser eine externe URL.
            next_param = request.args.get('next')
            if next_param and next_param.startswith('/') and not next_param.startswith('//'):
                next_url = next_param
            else:
                next_url = url_for('dashboard')
            logging.info("Admin-Login erfolgreich.")
            return redirect(next_url)
        else:
            _login_rate_limit_record_failure(client_ip)
            error = 'Falsches Passwort.'
            logging.warning("Fehlgeschlagener Login-Versuch.")
            return render_template('login.html', error=error), 401
    return render_template('login.html', error=error)


@app.route('/logout')
def logout():
    """Session beenden und auf Login-Seite weiterleiten."""
    session.clear()
    logging.info("Admin-Logout.")
    return redirect(url_for('login_page'))


# ===== DASHBOARD =====
@app.route('/dashboard')
@login_required
def dashboard():
    """Haupt-Dashboard — Übersicht, Headlines, Feeds in einem Interface."""
    return render_template('dashboard.html')


# ===== FEED-MANAGEMENT WEB-INTERFACE =====
@app.route('/feeds')
@login_required
def feed_management():
    """Feed-Verwaltungsseite — UI fuer /api/moodlight/feeds CRUD"""
    return render_template('feeds.html')

# Background Worker starten — Intervall und Headlines aus DB-Settings (Fallback: Defaults)
_worker_interval = int(_startup_settings.get('analysis_interval', 1800)) if _startup_settings else 1800
_worker_headlines = int(_startup_settings.get('headlines_per_source', 1)) if _startup_settings else 1
start_background_worker(
    app=app,
    analyze_function=analyze_headlines_batch,
    interval_seconds=_worker_interval
)
from background_worker import get_background_worker
_worker = get_background_worker()
if _worker:
    _worker.headlines_per_source = _worker_headlines
    logging.info(f"Worker gestartet: Intervall={_worker_interval}s, Headlines/Quelle={_worker_headlines}")

if __name__ == '__main__':
    logging.getLogger().setLevel(logging.DEBUG) # DEBUG für mehr Infos zur Konfiguration etc.

    is_debug_mode = os.environ.get('FLASK_ENV') == 'development'
    logging.info(f"Starte Flask App im {'Debug' if is_debug_mode else 'Production'} Modus (Claude Haiku + PostgreSQL + Redis)...")
    if not anthropic_client:
        logging.warning("!!! Anthropic Client konnte nicht initialisiert werden (fehlender API Key?). API wird Fehler zurückgeben. !!!")

    app.run(host='0.0.0.0', port=6237, debug=is_debug_mode)