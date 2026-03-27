# Requirements — v10.0 Perzentil-Transparenz & Firmware-Stabilität

## Milestone Goal

Die Perzentil-basierte LED-Zuordnung wird auf ESP32 mood.html und GitHub Page sichtbar und verständlich gemacht — identisch zum Backend-Dashboard. Parallel werden die Firmware-Stabilitätsfixes (Watchdog, MQTT Race Condition, WiFi-Reconnect) committet und deployed.

## Requirements

### FW — Firmware-Stabilität

| ID | Requirement | Priority | Notes |
|----|------------|----------|-------|
| FW-01 | Watchdog wird während aller blockierenden Operationen (HTTP, WiFi-Reconnect, MQTT-Reconnect) gefüttert | must | Verhindert TG1WDT_SYS_RESET |
| FW-02 | MQTT Refresh-Button setzt nur Flag, HTTP-Request wird in loop() ausgeführt | must | Verhindert LoadProhibited im ArduinoHA-Callback |
| FW-03 | initialStatesPending als einzelne Variable in AppState statt dreifach static bool | must | sendInitialStates() wird nach MQTT-Reconnect aufgerufen |
| FW-04 | WiFi-Stabilitäts-Hysterese: 3s Pause nach Reconnect bevor HTTP-Requests starten | must | Verhindert LoadProhibited auf instabilem TCP-Stack |
| FW-05 | WiFi.disconnect() nicht während laufender HTTP-Operation | must | Verhindert TCP-Stack-Korruption |

### PZ — Perzentil-Visualisierung

| ID | Requirement | Priority | Notes |
|----|------------|----------|-------|
| PZ-01 | ESP32 mood.html zeigt Perzentil-Badge ("48. Pzt.") mit erklärendem Text | must | Wie Backend-Dashboard |
| PZ-02 | ESP32 mood.html zeigt historischen Bereich-Balken (Min/Median/Max, Score-Nadel, P20/P40/P60/P80 Schwellwert-Ticks) | must | Wie Backend-Dashboard |
| PZ-03 | ESP32 mood.html erklärt verständlich was die Perzentil-Einordnung bedeutet | must | "Dein Score liegt über X% der Werte der letzten 7 Tage" |
| PZ-04 | GitHub Page (docs/index.html) zeigt Perzentil-Kontext identisch zu mood.html | must | Gleiche Visualisierung |
| PZ-05 | Fallback-Hinweis wenn weniger als 3 historische Datenpunkte vorhanden | should | Backend liefert fallback:true |
| PZ-06 | Layout ist attraktiv, responsiv und passt zum bestehenden Design | must | CSS-Variablen, Dark/Light Mode |

## Acceptance Criteria

- [ ] ESP32 rebooted nach 30 Minuten nicht (Firmware stabil)
- [ ] mood.html zeigt Perzentil-Badge + Balken mit Schwellwerten
- [ ] GitHub Page zeigt identische Perzentil-Darstellung
- [ ] Score "-0.23" mit Perzentil 0.48 zeigt korrekt "48. Pzt." und erklärt warum LED-Index 3
- [ ] Fallback-Hinweis erscheint bei wenigen Datenpunkten
- [ ] Dark Mode und Light Mode funktionieren
