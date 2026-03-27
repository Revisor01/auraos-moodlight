# Phase 17: Dynamische Skalierung - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Score-Mapping basiert auf historischen Perzentilen der letzten 7 Tage statt fester Schwellwerte. API liefert vollständigen Skalierungs-Kontext (Rohwert, Perzentil, historischer Min/Max/Median). Die LED-Farbe repräsentiert wo der aktuelle Score im historischen Vergleich steht.

</domain>

<decisions>
## Implementation Decisions

### Perzentil-Ansatz
- 7-Tage-Fenster für historische Basis (ausreichend Datenpunkte, reagiert auf Trends)
- 5 Perzentil-Grenzen für 5 LED-Farben: P20, P40, P60, P80
- Aktueller Score wird relativ zur historischen Verteilung gemappt
- Beispiel: Wenn P20=-0.5, P40=-0.3, P60=-0.15, P80=+0.1 → Score -0.2 wäre "neutral" (zwischen P40 und P60)

### API-Erweiterung
- /api/moodlight/current erweitern um: raw_score, percentile, thresholds[], historical_min, historical_max, historical_median
- Neues Feld "led_index" (0-4) basierend auf Perzentil-Position
- Bestehende Felder (sentiment_score, category) bleiben erhalten

### Claude's Discretion
- Genaue DB-Query für Perzentil-Berechnung
- Caching-Strategie (Perzentile müssen nicht bei jedem Request neu berechnet werden)
- Fallback wenn weniger als 7 Tage Daten vorhanden

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- sentiment-api/database.py — Database-Klasse, get_recent_headlines()
- sentiment-api/moodlight_extensions.py — /api/moodlight/current Endpoint
- sentiment-api/init.sql — sentiment_history Tabelle mit scores

### Established Patterns
- Redis-Cache mit 5 Minuten TTL für /api/moodlight/current
- Database-Klasse mit execute(), fetchone(), fetchall()
- JSON-Response Format mit status, data

### Integration Points
- database.py: Neue Methode get_score_percentiles(days=7)
- moodlight_extensions.py: /api/moodlight/current Response erweitern
- background_worker.py: Nach save_sentiment() auch Perzentile aktualisieren (oder lazy berechnen)

</code_context>

<specifics>
## Specific Ideas

Keine spezifischen Vorgaben — Standard-Perzentil-Berechnung.

</specifics>

<deferred>
## Deferred Ideas

Keine.

</deferred>
