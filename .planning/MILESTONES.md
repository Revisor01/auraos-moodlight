# Milestones

## v9.0 Sentiment-Trend pro Feed (Shipped: 2026-03-27)

**Phases completed:** 2 phases, 3 plans, 4 tasks

**Key accomplishments:**

- PostgreSQL-Aggregation per Feed mit get_feed_trends(days) in database.py und öffentlichem GET /api/moodlight/feeds/trends Endpoint als sortiertes Feed-Ranking
- One-liner:
- Farbkodiertes Feed-Ranking mit 7/30-Tage-Umschalter via IntersectionObserver in docs/index.html eingebaut

---

## v8.0 ESP32 UI-Redesign (Shipped: 2026-03-27)

**Phases completed:** 2 phases, 5 plans, 3 tasks

**Key accomplishments:**

- One-liner:
- index.html von Inline-Styles auf CSS-Klassen umgestellt: Score-Farbkodierung (.score-neutral), 4 .card-Karten, Icons in h2, alle JS-Hooks erhalten
- setup.html bereinigt: 3 Inline-Styles entfernt, .update-step und .update-progress-wrapper Klassen in style.css ergaenzt
- One-liner:
- diagnostics.html bereinigt: style-Block fuer details/summary entfernt, flex-direction:column Inline-Style aus .buttons-Div entfernt — alle JS-Funktionen und IDs unveraendert

---

## v7.0 Dashboard-Einstellungen (Shipped: 2026-03-27)

**Phases completed:** 3 phases, 6 plans, 8 tasks

**Key accomplishments:**

- 1. [Rule 3 - Blocking] Reihenfolge: logging.basicConfig vor load_settings_from_db()
- SentimentUpdateWorker.reconfigure() für Runtime-Reload + GET/PUT /api/moodlight/settings mit sofortiger Worker-Synchronisation
- POST /api/moodlight/analyze/trigger Endpoint mit SentimentUpdateWorker.trigger() — synchroner manueller Analyse-Auslöser mit vollständiger DB-Persistenz, Cache-Invalidierung und strukturierten Fehler-Responses
- 'Jetzt analysieren'-Button im Dashboard-Übersichts-Tab mit Spinner-Feedback, Erfolgs-/Fehlermeldung und automatischem Sentiment-Refresh nach Abschluss
- Vierter Dashboard-Tab mit drei Formularsektionen (Analyse, API Key, Passwort) direkt verdrahtet auf PUT /api/moodlight/settings

---

## v6.0 Dynamische Bewertungsskala (Shipped: 2026-03-27)

**Phases completed:** 3 phases, 6 plans, 7 tasks

**Key accomplishments:**

- One-liner:
- One-liner:
- PostgreSQL percentile_cont()-Abfrage in Database-Klasse + module-level compute_led_index() mit Fallback bei weniger als 3 historischen Datenpunkten
- `/api/moodlight/current` liefert jetzt raw_score, led_index via compute_led_index(), percentile via linearer Interpolation, thresholds (p20/p40/p60/p80/fallback) und historical (min/max/median/count) mit Redis-Cache auf Key moodlight:current:v2
- ESP32 liest led_index direkt aus /api/moodlight/current Response und ueberschreibt die lokale mapSentimentToLED()-Berechnung; Fallback fuer aeltere Backends ohne led_index-Feld bleibt erhalten.
- One-liner:

---

## v5.0 Schlagzeilen-Transparenz & Dashboard (Shipped: 2026-03-26)

**Phases completed:** 4 phases, 7 plans, 11 tasks

**Key accomplishments:**

- PostgreSQL headlines-Tabelle mit save_headlines() Bulk-INSERT und Background Worker Integration — Einzelscores werden nach jeder automatischen Sentiment-Analyse dauerhaft gespeichert
- One-liner:
- One-liner:
- Headlines-Datenbankabfrage und API-Route: get_recent_headlines() mit LEFT JOIN auf feeds + öffentlicher GET /api/moodlight/headlines Endpoint mit ?limit-Parameter
- Vollständiges SPA-Dashboard: /dashboard Flask-Route + 934-zeiliges dashboard.html mit drei Tabs (Übersicht, Schlagzeilen, Feeds), Score-Farbverlauf-Visualisierung und integrierter Feed-Verwaltung
- mood.html erhält farbkodierte Headlines-Sektion mit Einzel-Scores und Feed-Namen via fetch() gegen den öffentlichen /api/moodlight/headlines Endpoint
- One-liner:

---

## v4.0 Konfigurierbare RSS-Feeds (Shipped: 2026-03-26)

**Phases completed:** 3 phases, 4 plans, 4 tasks

**Key accomplishments:**

- Feed-CRUD REST-API: GET/POST/DELETE /api/moodlight/feeds mit PostgreSQL-Backend und URL-Erreichbarkeitspruefung (5s Timeout)
- Flask-Route /feeds + standalone feeds.html mit Tabelle, Add-Formular und DELETE per fetch() — Milestone v4.0 vollstaendig

---
