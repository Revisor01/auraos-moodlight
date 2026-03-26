# -*- coding: utf-8 -*-
from flask import Flask, jsonify, request
import feedparser
import logging
from collections import Counter
import os
import re
import math
from openai import OpenAI, OpenAIError
from shared_config import get_sentiment_category

app = Flask(__name__)

# --- Konfiguration & Standardwerte ---
# Standardanzahl Headlines pro Quelle, wenn nichts anderes angegeben
DEFAULT_HEADLINES_PER_SOURCE_MAIN = 2
DEFAULT_HEADLINES_PER_SOURCE_TOTAL = 1
# Umgebungsvariable für Standard (optional)
try:
    # Versuche, den Wert aus der Umgebungsvariable zu lesen und in eine Zahl umzuwandeln
    DEFAULT_HEADLINES_FROM_ENV = int(os.environ.get('DEFAULT_HEADLINES_PER_SOURCE', ''))
    # Stelle sicher, dass der Wert sinnvoll ist (mindestens 1)
    if DEFAULT_HEADLINES_FROM_ENV < 1:
        DEFAULT_HEADLINES_FROM_ENV = None # Ungültigen Wert ignorieren
    else:
         logging.info(f"Umgebungsvariable DEFAULT_HEADLINES_PER_SOURCE gefunden: {DEFAULT_HEADLINES_FROM_ENV}")
except (ValueError, TypeError):
    DEFAULT_HEADLINES_FROM_ENV = None # Variable nicht gesetzt oder keine Zahl

# Logging-Konfiguration
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)

# --- OpenAI Client initialisieren ---
# (Code unverändert)
openai_api_key = os.environ.get("OPENAI_API_KEY")
openai_client = None
if openai_api_key:
    try:
        openai_client = OpenAI(api_key=openai_api_key)
        logging.info("OpenAI Client erfolgreich initialisiert.")
    except Exception as e:
        logging.error(f"Fehler bei der Initialisierung des OpenAI Clients: {e}")
else:
    logging.error("############################################################")
    logging.error("FEHLER: OPENAI_API_KEY Umgebungsvariable nicht gesetzt!")
    logging.error("Sentiment-Analyse über OpenAI API wird nicht funktionieren.")
    logging.error("############################################################")


# --- Funktion zur Sentiment-Analyse mit OpenAI API ---
# (Code unverändert, gibt Liste von Scores zurück)
def analyze_sentiment_openai(headlines_batch: list) -> list:
    if not openai_client:
        logging.error("OpenAI Client nicht verfügbar in analyze_sentiment_openai.")
        return [0.0] * len(headlines_batch)
    if not headlines_batch:
        return []

    prompt_lines = [
      "Analysiere das Sentiment jeder der folgenden deutschen Nachrichtenschlagzeilen.",
      "Gib für jede Schlagzeile eine Fließkommazahl zwischen -1.0 und +1.0 zurück.",
      "",
      "WICHTIG - Ausgewogene Bewertung:",
      "- Nachrichten sind oft neutral formuliert, auch wenn das Thema ernst ist. Bewerte die NACHRICHT, nicht das Thema.",
      "- Eine sachliche Meldung über ein Problem ist NICHT automatisch stark negativ.",
      "- Reserviere extreme Werte (-1.0 oder +1.0) nur für wirklich außergewöhnliche Ereignisse.",
      "",
      "Skala:",
      "-1.0: Katastrophe mit vielen Toten, Weltuntergang",
      "-0.7: Schwere Krise, großes Unglück",
      "-0.4: Probleme, Konflikte, schlechte Entwicklungen",
      "-0.2: Leicht negative oder besorgniserregende Nachrichten",
      " 0.0: Neutral, sachliche Berichterstattung, gemischte Nachrichten",
      "+0.2: Leicht positive Entwicklungen, kleine Fortschritte",
      "+0.4: Gute Nachrichten, Erfolge, positive Entwicklungen",
      "+0.7: Sehr gute Nachrichten, bedeutende Erfolge",
      "+1.0: Herausragende Durchbrüche, historisch positive Ereignisse",
      "",
      "Die meisten alltäglichen Nachrichten sollten zwischen -0.4 und +0.4 liegen!",
      "",
      "Format: Nur \"Nummer: Score\" pro Zeile (z.B. \"1: -0.3\"). Keine Erklärungen.",
      "",
      "Schlagzeilen:"
    ]
    for i, headline in enumerate(headlines_batch):
        prompt_lines.append(f"{i+1}: {headline}")
    prompt_lines.append("\nSentiment-Scores:")
    full_prompt = "\n".join(prompt_lines)

    scores = [0.0] * len(headlines_batch)

    try:
        logging.info(f"Sende {len(headlines_batch)} Headlines zur Analyse an OpenAI API (gpt-4o-mini)...")
        response = openai_client.chat.completions.create(
            model="gpt-4o-mini",
            messages=[{"role": "user", "content": full_prompt}],
            temperature=0.1,
            max_tokens=len(headlines_batch) * 15,
            timeout=45.0
        )
        raw_response_content = response.choices[0].message.content.strip()
        logging.info("Antwort von OpenAI API erhalten.")
        logging.debug(f"OpenAI Raw Response:\n{raw_response_content}")

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
                        logging.warning(f"Geparster Index {index+1} aus Antwort ist außerhalb des Bereichs ({len(scores)} Headlines). Zeile: '{line}'")
                except (ValueError, IndexError) as parse_error:
                    logging.warning(f"Fehler beim Parsen der Score-Zeile '{line}': {parse_error}")
            elif line.strip():
                 logging.warning(f"Unerwartetes Format in Antwortzeile von OpenAI: '{line}'")

        logging.info(f"Erfolgreich {parsed_count} Scores aus der OpenAI-Antwort geparst.")
        if parsed_count != len(headlines_batch):
             logging.warning(f"Nicht alle Scores konnten geparst werden! Erwartet: {len(headlines_batch)}, Geparsed: {parsed_count}. Fehlende Scores bleiben 0.0.")

        return scores

    except OpenAIError as e:
        logging.error(f"OpenAI API Fehler: {e}")
        return [0.0] * len(headlines_batch)
    except Exception as e:
        logging.error(f"Unerwarteter Fehler bei der OpenAI Analyse: {e}")
        return [0.0] * len(headlines_batch)

# --- Haupt-Analysefunktion (gibt jetzt gewichteten Score als 'total_sentiment' zurück) ---
def analyze_headlines_openai_batch(headlines: list):
    """
    Analysiert Headlines mit OpenAI, berechnet Statistiken und gibt den
    gewichteten Mood-Score als 'total_sentiment' zurück.
    """
    results = []
    sentiment_distribution = Counter()
    total_numeric_sentiment_sum = 0.0 # Wird nur noch für Logging gebraucht
    analyzed_count = 0

    # --- Vorbereitung ---
    # (Code unverändert)
    if not headlines:
        logging.warning("analyze_headlines_openai_batch aufgerufen mit leerer Headline-Liste.")
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
    sentiment_scores = analyze_sentiment_openai(headline_texts)

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
  return jsonify({
    "service": "AuraOS Moodlight API v9.1",
    "status": "running",
    "endpoints": {
      "current": "/api/moodlight/current",
      "history": "/api/moodlight/history?hours=168",
      "health": "/api/health"
    }
  })

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

  # OpenAI-Check
  health["checks"]["openai"] = {
    "status": "healthy" if openai_client else "unhealthy",
    "available": openai_client is not None
  }
  if not openai_client:
    health["status"] = "degraded"

  status_code = 200 if health["status"] == "healthy" else 503 if health["status"] == "unhealthy" else 200
  return jsonify(health), status_code

# --- Flask Routen (angepasst für Konfiguration und neuen Score) ---
@app.route('/api/news', methods=['GET'])
def get_news():
    if not openai_client: return jsonify({"status": "error", "message": "Sentiment-Analyse-Dienst nicht bereit (OpenAI Problem)."}), 503

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
    analysis_result = analyze_headlines_openai_batch(headlines)

    # Gib den gewichteten Score unter 'total_sentiment' zurück
    return jsonify({
        "status": "success",
        "total_sentiment": analysis_result['total_sentiment'], # Hier steht jetzt der gewichtete Score
        "statistics": analysis_result['statistics'],
        "detailed_analysis": analysis_result['results'],
        "errors": errors if errors else None,
        "skipped_feeds": skipped_feeds if skipped_feeds else None
    })

@app.route('/api/feedconfig', methods=['POST'])
def configure_feeds():
  global rss_feeds
  
  if not request.is_json:
    return jsonify({"status": "error", "message": "JSON erwartet"}), 400
  
  feed_data = request.get_json()
  
  if not feed_data or not isinstance(feed_data, dict) or 'feeds' not in feed_data:
    return jsonify({"status": "error", "message": "Ungültiges Format"}), 400
  
  # Feeds aktualisieren
  custom_feeds = feed_data['feeds']
  if not custom_feeds or not isinstance(custom_feeds, dict):
    return jsonify({"status": "error", "message": "Keine gültigen Feeds gefunden"}), 400
  
  # Optional: Ändere die globale Variable
  # rss_feeds = custom_feeds
  
  # Oder: Erstelle eine neue Kopie
  new_feeds = {}
  for name, url in custom_feeds.items():
    new_feeds[name] = url
    
  # Validiere die URLs
  invalid_feeds = []
  for name, url in new_feeds.items():
    if not url.startswith('http'):
      invalid_feeds.append(name)
      
  # Entferne ungültige Feeds
  for name in invalid_feeds:
    del new_feeds[name]
    
  # Speichere nur, wenn wir noch gültige Feeds haben
  if new_feeds:
    rss_feeds = new_feeds
    logging.info(f"RSS-Feeds aktualisiert: {len(rss_feeds)} Feeds")
    return jsonify({
      "status": "success",
      "message": f"{len(rss_feeds)} RSS-Feeds aktualisiert",
      "feeds": list(rss_feeds.keys())
    })
  else:
    return jsonify({
      "status": "error", 
      "message": "Keine gültigen Feeds gefunden"
    }), 400

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

# Background Worker starten - muss ausserhalb __main__ sein damit Gunicorn ihn startet
start_background_worker(
    app=app,
    analyze_function=analyze_headlines_openai_batch,
    interval_seconds=1800
)

# Hauptausführungspunkt
if __name__ == '__main__':
    logging.getLogger().setLevel(logging.DEBUG) # DEBUG für mehr Infos zur Konfiguration etc.

    is_debug_mode = os.environ.get('FLASK_ENV') == 'development'
    logging.info(f"Starte Flask App im {'Debug' if is_debug_mode else 'Production'} Modus (GPT-4o-mini + PostgreSQL + Redis)...")
    if not openai_client:
         logging.warning("!!! OpenAI Client konnte nicht initialisiert werden (fehlender API Key?). API wird Fehler zurückgeben. !!!")

    app.run(host='0.0.0.0', port=6237, debug=is_debug_mode)