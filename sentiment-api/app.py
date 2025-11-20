# -*- coding: utf-8 -*-
from flask import Flask, render_template, jsonify, request
import feedparser
import logging
from collections import Counter
import os
import re
import math
import socket
from openai import OpenAI, OpenAIError

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


# Liste der RSS-Feeds
# (Code unverändert)
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
      "Gib für jede Schlagzeile ausschließlich eine einzige Fließkommazahl zwischen -1.0 (extrem negativ) und +1.0 (extrem positiv) zurück, wobei 0.0 neutral ist.",
      "WICHTIG: Nutze unbedingt die VOLLE Bandbreite von -1.0 bis +1.0. Vermeide Werte nahe bei 0, außer bei wirklich neutralen Nachrichten.",
      "Als Orientierung: -1.0 bis -0.7: Katastrophen, Kriege; -0.7 bis -0.4: Schwere Probleme, Skandale; -0.4 bis -0.1: Leicht negative Nachrichten",
      "0.1 bis 0.4: Leicht positive Nachrichten; 0.4 bis 0.7: Gute Nachrichten; 0.7 bis 1.0: Herausragende Erfolge, wissenschaftliche Durchbrüche",
      "Da positive Nachrichten seltener sind, gib ihnen einen etwas stärkeren Ausschlag, wenn sie klar positiv sind.",
      "Formatiere die Ausgabe als nummerierte Liste, wobei jede Zeile nur die Nummer der Schlagzeile gefolgt von einem Doppelpunkt und dem Sentiment-Score enthält (z.B. \"1: -0.5\"). Gib KEINEN zusätzlichen Text, keine Erklärungen, keine Einheiten und keine Kopf- oder Fußzeilen aus. Starte direkt mit \"1:\".",
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

            if score > 0.85: strength = "sehr_positiv"
            elif score > 0.2: strength = "positiv"
            elif score < -0.85: strength = "sehr_negativ"
            elif score < -0.2: strength = "negativ"
            else: strength = "neutral"

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
    weighted_mood_score = 0.0
    if analyzed_count > 0:
      # Alle individuellen Sentiment-Scores sammeln
      all_scores = [item['sentiment'] for item in results]
      
      # Basis-Durchschnitt berechnen
      base_avg = sum(all_scores) / len(all_scores)
      
      # Extremwerte identifizieren (noch niedrigere Schwelle)
      extreme_negative_count = sum(1 for s in all_scores if s <= -0.5)  # War -0.6
      extreme_positive_count = sum(1 for s in all_scores if s >= 0.5)   # War 0.6
      
      # Verstärkter Skalierungsfaktor
      scale_factor = 3.5  # War 3.5
      
      # Stärkerer Extremwert-Boost
      extreme_boost_factor = 0.6  # War 0.6
      
      # Quadratische Transformation mit höherem Multiplikator
      if abs(base_avg) > 0.05:
        transformed_avg = math.copysign(base_avg**2 * 8.0, base_avg)  # War 5.0
      else:
        transformed_avg = 0
        
      # Üblicher linearer Faktor
      scaled_avg = base_avg * scale_factor if abs(base_avg) <= 0.05 else transformed_avg
      
      # Verstärkter Extremwert-Boost
      extreme_factor = 0.0
      if extreme_negative_count > 0:
        extreme_factor -= (extreme_negative_count / len(all_scores)) * extreme_boost_factor
        # Extra-Boost für sehr hohen Anteil negativer Nachrichten
        if extreme_negative_count / len(all_scores) > 0.25:  # War 0.3
          extreme_factor -= 0.3  # War 0.2
        
      if extreme_positive_count > 0:
        extreme_factor += (extreme_positive_count / len(all_scores)) * extreme_boost_factor
        # Extra-Boost für sehr hohen Anteil positiver Nachrichten
        if extreme_positive_count / len(all_scores) > 0.25:  # War 0.3
          extreme_factor += 0.3  # War 0.2
        
      # NEU: Balance-Faktor (verstärkt das Signal wenn eine Richtung überwiegt)
      neg_count = sum(1 for s in all_scores if s < -0.1)
      pos_count = sum(1 for s in all_scores if s > 0.1)
      if abs(neg_count - pos_count) >= 3:  # Wenn mind. 3 mehr in einer Richtung
        imbalance_factor = 0.2 * math.copysign(1, neg_count - pos_count)
        extreme_factor += imbalance_factor
        logging.info(f"Balance-Boost angewendet: {imbalance_factor:.2f} (Neg: {neg_count}, Pos: {pos_count})")
      
      # Kombinieren und im gültigen Bereich halten
      weighted_mood_score = max(-1.0, min(1.0, scaled_avg + extreme_factor))
      
      logging.info(f"Sentiment Berechnung: Basis-Durchschnitt={base_avg:.4f}, Skaliert={scaled_avg:.4f}, Extremwert-Boost={extreme_factor:.4f}")
      logging.info(f"Gewichteter Score (als total_sentiment): {weighted_mood_score:.4f} (Extremwerte: {extreme_negative_count} neg, {extreme_positive_count} pos)")

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
  return render_template('index.html')

@app.route('/api/dashboard')
def get_dashboard_data():
  # Hier Daten für das Dashboard generieren
  return jsonify({...})

@app.route('/api/logs')
def get_logs():
  # Hier Logs bereitstellen
  return jsonify({...})

# --- Flask Routen (angepasst für Konfiguration und neuen Score) ---
@app.route('/api/news', methods=['GET'])
def get_news():
    if not openai_client: return jsonify({"status": "error", "message": "Sentiment-Analyse-Dienst nicht bereit (OpenAI Problem)."}), 503

    # Dynamische Anzahl der Headlines pro Quelle ermitteln
    num_headlines_per_source = get_headlines_per_source(DEFAULT_HEADLINES_PER_SOURCE_MAIN)

    headlines = []; errors = []; processed_links = set(); skipped_feeds = []
    socket_timeout = 10.0

    # Temporäres Timeout setzen
    original_timeout = socket.getdefaulttimeout()
    timeout_changed = False
    # ... (Timeout Code unverändert) ...
    if original_timeout is None or original_timeout != socket_timeout:
        try:
            socket.setdefaulttimeout(socket_timeout)
            logging.info(f"Socket-Timeout temporär auf {socket_timeout}s gesetzt.")
            timeout_changed = True
        except Exception as e:
             logging.warning(f"Konnte globalen Socket-Timeout nicht setzen: {e}")


    logging.info(f"Starte Feed-Abruf für /api/news (max. {num_headlines_per_source} Headlines/Quelle)...")
    total_headlines_found_before_filtering = 0

    for source, url in rss_feeds.items():
        logging.debug(f"--- Verarbeite Quelle: {source} ({url}) ---")
        try:
            feed = feedparser.parse(url, request_headers={'User-Agent': 'WorldMoodAnalyzer/1.0'})
            # ... (Bozo-Prüfung und Timeout-Handling unverändert) ...
            if feed.bozo and isinstance(feed.bozo_exception, Exception):
                 if isinstance(feed.bozo_exception, socket.timeout):
                      logging.warning(f"Überspringe Feed von {source} wegen Timeout ({socket_timeout}s).")
                      skipped_feeds.append(f"{source} (Timeout)")
                 else:
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

        except socket.timeout:
             logging.warning(f"Überspringe Feed von {source} wegen explizitem Socket-Timeout ({socket_timeout}s).")
             skipped_feeds.append(f"{source} (Timeout)")
             continue
        except Exception as e:
            logging.error(f"Fehler beim Abrufen/Parsen des Feeds von {source}: {e}", exc_info=True)
            errors.append(f"Fehler bei {source}")

    # Timeout zurücksetzen
    # ... (Code unverändert) ...
    if timeout_changed and original_timeout is not None:
        try:
            socket.setdefaulttimeout(original_timeout)
            logging.info(f"Socket-Timeout auf ursprünglichen Wert ({original_timeout}) zurückgesetzt.")
        except Exception as e:
             logging.warning(f"Konnte Socket-Timeout nicht zurücksetzen: {e}")


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


# Hauptausführungspunkt
if __name__ == '__main__':
    logging.getLogger().setLevel(logging.DEBUG) # DEBUG für mehr Infos zur Konfiguration etc.

    is_debug_mode = os.environ.get('FLASK_ENV') == 'development'
    logging.info(f"Starte Flask App im {'Debug' if is_debug_mode else 'Production'} Modus (GPT-4o-mini + PostgreSQL + Redis)...")
    if not openai_client:
         logging.warning("!!! OpenAI Client konnte nicht initialisiert werden (fehlender API Key?). API wird Fehler zurückgeben. !!!")

    # Background Worker starten (alle 30 Minuten)
    start_background_worker(
        app=app,
        analyze_function=analyze_headlines_openai_batch,
        interval_seconds=1800
    )

    app.run(host='0.0.0.0', port=6237, debug=is_debug_mode)