# Milestones

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
