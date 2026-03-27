# -*- coding: utf-8 -*-
from flask import Flask, jsonify, request, render_template, session, redirect, url_for
from werkzeug.security import generate_password_hash, check_password_hash
from functools import wraps
from datetime import timedelta
import feedparser
import logging
from collections import Counter
import os
import re
import math
import anthropic
from anthropic import Anthropic, APIConnectionError, RateLimitError, APIStatusError
from shared_config import get_sentiment_category

app = Flask(__name__)

# --- Session-Konfiguration ---
app.secret_key = os.environ.get('SECRET_KEY', 'dev-secret-schluessel-aendern')
app.permanent_session_lifetime = timedelta(hours=24)

# Logging-Konfiguration
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)


def load_settings_from_db():
    """
    Lade Einstellungen aus PostgreSQL; fällt auf Env-Variablen zurück wenn DB-Wert fehlt oder leer.
    Gibt Dict mit geladenen Werten zurück.
    """
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

# --- Konfiguration & Standardwerte ---
# Standardanzahl Headlines pro Quelle, wenn nichts anderes angegeben
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
def analyze_sentiment_claude(headlines_batch: list) -> list:
    if not anthropic_client:
        logging.error("Anthropic Client nicht verfügbar in analyze_sentiment_claude.")
        return [0.0] * len(headlines_batch)
    if not headlines_batch:
        return []

    # Kalibrierter Prompt — Ankerpunkte und Anti-Bias-Anweisung (Plan 02)
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
            max_tokens=len(headlines_batch) * 15,
            temperature=1.0,
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
            logging.warning(f"Nicht alle Scores geparst! Erwartet: {len(headlines_batch)}, Geparsed: {parsed_count}. Fehlende Scores bleiben 0.0.")

        return scores

    except APIConnectionError as e:
        logging.error(f"Anthropic API nicht erreichbar: {e}")
        return [0.0] * len(headlines_batch)
    except RateLimitError as e:
        logging.error(f"Anthropic Rate-Limit erreicht: {e}")
        return [0.0] * len(headlines_batch)
    except APIStatusError as e:
        logging.error(f"Anthropic API Fehler {e.status_code}: {e.response}")
        return [0.0] * len(headlines_batch)
    except Exception as e:
        logging.error(f"Unerwarteter Fehler bei der Anthropic Analyse: {e}")
        return [0.0] * len(headlines_batch)

# --- Haupt-Analysefunktion (gibt jetzt gewichteten Score als 'total_sentiment' zurück) ---
def analyze_headlines_batch(headlines: list):
    """
    Analysiert Headlines mit Anthropic (Claude Haiku), berechnet Statistiken und gibt den
    gewichteten Mood-Score als 'total_sentiment' zurück.
    """
    results = []
    sentiment_distribution = Counter()
    total_numeric_sentiment_sum = 0.0 # Wird nur noch für Logging gebraucht
    analyzed_count = 0

    # --- Vorbereitung ---
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

    # --- Sentiment Analyse ---
    sentiment_scores = analyze_sentiment_claude(headline_texts)

    # --- Verarbeitung der Scores und Kategorisierung ---
    # (Code unverändert)
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

    # --- Berechnungen nach der Analyse ---
    # 1. (Optional) Normaler Durchschnitt für Logging
    average_sentiment_for_logging = total_numeric_sentiment_sum / analyzed_count if analyzed_count > 0 else 0.0
    logging.info(f"Analyse abgeschlossen. Verarbeitet: {analyzed_count}. (Durchschnittl. Sentiment für Logging: {average_sentiment_for_logging:.4f})")

    # 2. Gewichteter Mood Score (dies wird der neue 'total_sentiment')
    # Mit Verstärkung für mehr Varianz auf der LED-Darstellung
    weighted_mood_score = 0.0
    if analyzed_count > 0:
      # Alle individuellen Sentiment-Scores sammeln
      all_scores = [item['sentiment'] for item in results]

      # Basis: Durchschnitt der Einzelwerte
      raw_average = sum(all_scores) / len(all_scores)

      # Verstärkungsfunktion: Streckt moderate Werte für mehr Varianz
      # Verwendet eine sanfte S-Kurve (tanh-ähnlich) mit Faktor 2.0
      # Bei raw_average = -0.25 -> ca. -0.47
      # Bei raw_average = -0.35 -> ca. -0.62
      # Bei raw_average = -0.50 -> ca. -0.76
      amplification_factor = 2.0
      stretched = raw_average * amplification_factor

      # Sanfte Begrenzung mit tanh-ähnlicher Funktion (verhindert harte Clipping)
      # math.tanh begrenzt natürlich auf [-1, 1]
      weighted_mood_score = math.tanh(stretched)

      logging.info(f"Sentiment Berechnung: Roh-Durchschnitt={raw_average:.4f}, Verstärkt (x{amplification_factor})={weighted_mood_score:.4f}")

    else:
        logging.warning("Keine Headlines analysiert, total_sentiment bleibt 0.0")

    # --- Rückgabe der Ergebnisse ---
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
    """
    Ermittelt die Anzahl der zu holenden Headlines pro Quelle.
    Priorität: URL-Parameter > Umgebungsvariable > Routen-Standard.
    """
    # 1. URL-Parameter prüfen
    try:
        # Versuche, den Parameter aus der URL zu lesen und in eine Zahl umzuwandeln
        headlines_param = request.args.get('headlines_per_source', type=int)
        if headlines_param is not None and headlines_param > 0:
            logging.debug(f"Verwende 'headlines_per_source={headlines_param}' aus URL-Parameter.")
            return headlines_param
        elif headlines_param is not None: # Parameter da, aber <= 0
             logging.warning(f"Ungültiger Wert '{request.args.get('headlines_per_source')}' für URL-Parameter 'headlines_per_source'. Ignoriere.")
    except (ValueError, TypeError):
        logging.warning(f"Konnte URL-Parameter 'headlines_per_source' ('{request.args.get('headlines_per_source')}') nicht als Zahl lesen. Ignoriere.")
        pass # Ignorieren und mit nächster Priorität weitermachen

    # 2. Umgebungsvariable prüfen (wenn vorhanden und gültig)
    if DEFAULT_HEADLINES_FROM_ENV is not None:
        logging.debug(f"Verwende {DEFAULT_HEADLINES_FROM_ENV} aus Umgebungsvariable DEFAULT_HEADLINES_PER_SOURCE.")
        return DEFAULT_HEADLINES_FROM_ENV

    # 3. Routen-spezifischen Standardwert verwenden
    logging.debug(f"Verwende Routen-Standardwert: {route_default} Headlines pro Quelle.")
    return route_default


@app.route('/')
def index():
    return redirect(url_for('dashboard'))

@app.route('/api/health')
def health_check():
  """
  Health-Check Endpoint mit Datenbank-Verbindungstest
  """
  from database import get_database

  health = {
    "status": "healthy",
    "service": "AuraOS Moodlight API",
    "checks": {}
  }

  # Datenbank-Check
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
    health["checks"]["database"] = {"status": "unhealthy", "error": str(e)}
    health["status"] = "unhealthy"

  # Anthropic-Check
  health["checks"]["anthropic"] = {
    "status": "healthy" if anthropic_client else "unhealthy",
    "available": anthropic_client is not None
  }
  if not anthropic_client:
    health["status"] = "degraded"

  status_code = 200 if health["status"] == "healthy" else 503 if health["status"] == "unhealthy" else 200
  return jsonify(health), status_code

# --- Flask Routen (angepasst für Konfiguration und neuen Score) ---
@app.route('/api/news', methods=['GET'])
def get_news():
    if not anthropic_client: return jsonify({"status": "error", "message": "Sentiment-Analyse-Dienst nicht bereit (Anthropic Problem)."}), 503

    # RSS-Feeds dynamisch aus DB laden (FEED-01)
    from database import get_database
    rss_feeds = {f['name']: f['url'] for f in get_database().get_active_feeds()}

    # Dynamische Anzahl der Headlines pro Quelle ermitteln
    num_headlines_per_source = get_headlines_per_source(DEFAULT_HEADLINES_PER_SOURCE_MAIN)

    headlines = []; errors = []; processed_links = set(); skipped_feeds = []

    logging.info(f"Starte Feed-Abruf für /api/news (max. {num_headlines_per_source} Headlines/Quelle)...")
    total_headlines_found_before_filtering = 0

    for source, url in rss_feeds.items():
        logging.debug(f"--- Verarbeite Quelle: {source} ({url}) ---")
        try:
            try:
                response = requests.get(url, timeout=15, headers={'User-Agent': 'WorldMoodAnalyzer/1.0'})
                response.raise_for_status()
                feed = feedparser.parse(response.content)
            except requests.exceptions.Timeout:
                logging.warning(f"Überspringe Feed von {source} wegen Timeout (15s).")
                skipped_feeds.append(f"{source} (Timeout)")
                continue
            except requests.exceptions.RequestException as e:
                logging.warning(f"Fehler beim Abrufen von {source}: {e}")
                skipped_feeds.append(f"{source} (Abruffehler)")
                continue

            if feed.bozo and isinstance(feed.bozo_exception, Exception):
                logging.warning(f"Überspringe fehlerhaften Feed (bozo) von {source}: {feed.bozo_exception}")
                skipped_feeds.append(f"{source} (Formatfehler)")
                continue

            headlines_from_source = 0
            if feed.entries:
                logging.debug(f"  Quelle {source}: {len(feed.entries)} Einträge gefunden.")
                for entry in feed.entries:
                    # HIER wird die dynamische Anzahl verwendet
                    if headlines_from_source >= num_headlines_per_source: break
                    link = getattr(entry, 'link', None)
                    title = getattr(entry, 'title', None)

                    if title and title.strip():
                        headline_text = title.strip()
                        unique_key = link if link else headline_text
                        if unique_key not in processed_links:
                            headlines.append({ "headline": headline_text, "source": source, "link": link })
                            total_headlines_found_before_filtering += 1
                            processed_links.add(unique_key)
                            headlines_from_source += 1
                logging.debug(f"  Quelle {source}: {headlines_from_source} Headlines übernommen.")
            # else: logging.info(f"Keine Einträge im Feed von {source} gefunden.")

        except Exception as e:
            logging.error(f"Fehler beim Abrufen/Parsen des Feeds von {source}: {e}", exc_info=True)
            errors.append(f"Fehler bei {source}")


    # --- Analyse und Rückgabe ---
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

    # Gib den gewichteten Score unter 'total_sentiment' zurück
    return jsonify({
        "status": "success",
        "total_sentiment": analysis_result['total_sentiment'], # Hier steht jetzt der gewichtete Score
        "statistics": analysis_result['statistics'],
        "detailed_analysis": analysis_result['results'],
        "errors": errors if errors else None,
        "skipped_feeds": skipped_feeds if skipped_feeds else None
    })


# --- /api/news/total_sentiment Route (konfigurierbar, gibt gewichteten Score zurück) ---
@app.route('/api/news/total_sentiment', methods=['GET'])
def get_total_sentiment():
    """
    Legacy-Endpoint für AuraOS Moodlight-Geräte
    Nutzt jetzt gecachte Daten vom Background Worker (statt Live-Analyse)

    Returns:
        JSON mit 'total_sentiment' (float) und 'headlines_analyzed_count' (int)
    """
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
from background_worker import start_background_worker

# Neue Endpunkte registrieren
register_moodlight_endpoints(app)

# ===== LOGIN / LOGOUT =====
@app.route('/login', methods=['GET', 'POST'])
def login_page():
    """Login-Seite für das Backend-Interface."""
    error = None
    if request.method == 'POST':
        password = request.form.get('password', '')
        if _admin_password_hash and check_password_hash(_admin_password_hash, password):
            session['authenticated'] = True
            session.permanent = True
            next_url = request.args.get('next') or url_for('dashboard')
            logging.info("Admin-Login erfolgreich.")
            return redirect(next_url)
        else:
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
# Headlines-Anzahl auf den Worker übertragen
from background_worker import get_background_worker
_worker = get_background_worker()
if _worker:
    _worker.headlines_per_source = _worker_headlines
    logging.info(f"Worker gestartet: Intervall={_worker_interval}s, Headlines/Quelle={_worker_headlines}")

# Hauptausführungspunkt
if __name__ == '__main__':
    logging.getLogger().setLevel(logging.DEBUG) # DEBUG für mehr Infos zur Konfiguration etc.

    is_debug_mode = os.environ.get('FLASK_ENV') == 'development'
    logging.info(f"Starte Flask App im {'Debug' if is_debug_mode else 'Production'} Modus (Claude Haiku + PostgreSQL + Redis)...")
    if not anthropic_client:
        logging.warning("!!! Anthropic Client konnte nicht initialisiert werden (fehlender API Key?). API wird Fehler zurückgeben. !!!")

    app.run(host='0.0.0.0', port=6237, debug=is_debug_mode)