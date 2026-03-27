# Phase 27-01: Perzentil-Visualisierung auf ESP32 mood.html

## Änderungen

### firmware/data/mood.html
- Neue CSS-Sektion für Perzentil-Kontext (scale-section, hist-bar, threshold-ticks, led-explain)
- HTML-Sektion "Einordnung des Scores" mit:
  - Erklärender Subtitle: warum die LED-Farbe relativ statt absolut ist
  - Perzentil-Badge ("48. Pzt.")
  - Historischer Bereich-Balken mit P20/P40/P60/P80 Schwellwert-Ticks
  - Min/Median/Max Werte (7 Tage)
  - Datenpunkt-Zähler
  - LED-Farb-Erklärung mit farbigem Punkt und Klartext
  - Fallback-Hinweis bei wenigen Datenpunkten
- JavaScript: Fetch von /api/moodlight/current, Balken-Positionierung, Schwellwert-Rendering

## Requirements erfüllt

- [x] PZ-01: Perzentil-Badge mit erklärendem Text
- [x] PZ-02: Historischer Bereich-Balken mit Schwellwerten
- [x] PZ-03: Verständliche Erklärung der Perzentil-Einordnung
- [x] PZ-05: Fallback-Hinweis bei wenigen Datenpunkten
- [x] PZ-06: Attraktives Layout mit CSS-Variablen, Dark/Light Mode
