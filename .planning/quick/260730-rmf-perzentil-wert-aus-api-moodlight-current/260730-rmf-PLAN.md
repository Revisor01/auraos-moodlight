---
phase: quick-260730-rmf
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - firmware/src/mqtt_handler.h
  - firmware/src/mqtt_handler.cpp
  - firmware/src/sensor_manager.cpp
autonomous: false
requirements: [QUICK-RMF-01]

must_haves:
  truths:
    - "Home Assistant zeigt eine neue Sensor-Entität 'Weltlage Perzentil' mit einem Wert von 0 bis 100 und Einheit %"
    - "Der Perzentil-Wert wird bei jedem erfolgreichen Sentiment-Update aus /api/moodlight/current mitpubliziert"
    - "Fehlt das Feld percentile in der API-Antwort, wird nichts publiziert und der letzte bekannte Wert in HA bleibt stehen"
    - "Nach MQTT-(Re-)Connect erhält HA den letzten bekannten Perzentil-Wert über sendInitialStates()"
    - "firmware/ kompiliert weiterhin fehlerfrei mit pio run"
  artifacts:
    - path: "firmware/src/mqtt_handler.cpp"
      provides: "HASensor haSentimentPercentile Definition + Konfiguration in setupHA() + Publish in sendInitialStates()"
      contains: "haSentimentPercentile"
    - path: "firmware/src/mqtt_handler.h"
      provides: "extern-Deklaration von haSentimentPercentile"
      contains: "extern HASensor haSentimentPercentile"
    - path: "firmware/src/sensor_manager.cpp"
      provides: "Publish des Perzentils beim Sentiment-Update"
      contains: "haSentimentPercentile"
  key_links:
    - from: "firmware/src/sensor_manager.cpp"
      to: "haSentimentPercentile"
      via: "setValue() nach dem Parsen von doc[\"percentile\"]"
      pattern: "haSentimentPercentile\\.setValue"
    - from: "firmware/src/mqtt_handler.cpp"
      to: "appState.percentile"
      via: "sendInitialStates() publiziert letzten bekannten Wert"
      pattern: "appState\\.percentile"
---

<objective>
Der Perzentil-Wert aus `/api/moodlight/current` (Feld `percentile`, float 0.0–1.0) wird als
zusätzlicher MQTT-Sensor an Home Assistant gesendet.

Purpose: In HA sichtbar machen, wo der aktuelle Sentiment-Score relativ zum 7-Tage-Fenster
liegt. Der Wert steuert im Backend bereits die LED-Farbe (dynamische Skalierung), war in HA
bisher aber nicht abrufbar.

Output: Neue HA-Entität `sentiment_percentile` ("Weltlage Perzentil", Einheit `%`, 0–100),
publiziert bei jedem erfolgreichen Sentiment-Update und bei jedem MQTT-(Re-)Connect.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@CLAUDE.md

@firmware/src/mqtt_handler.h
@firmware/src/mqtt_handler.cpp
@firmware/src/sensor_manager.cpp
@firmware/src/app_state.h

<interfaces>
<!-- Bestehende Verträge — direkt verwenden, kein Codebase-Scan nötig. -->

Aus firmware/src/app_state.h (Feld existiert BEREITS, muss nicht angelegt werden):
```cpp
// Perzentil-Daten vom Backend (für Dashboard-Visualisierung)
float percentile = 0.0;   // 0.0 – 1.0
```

Aus firmware/src/debug.h:
```cpp
String floatToString(float value, int decimalPlaces);
```

Aus firmware/src/mqtt_handler.cpp (Muster der bestehenden Sensor-Definitionen, Zeilen 14–26):
```cpp
HASensor haSentimentScore("sentiment_score", HASensor::PrecisionP2);
HASensor haSentimentCategory("sentiment_category");
```

Aus firmware/src/sensor_manager.cpp (Zeilen 14–17, extern-Block für HA-Entities):
```cpp
extern HASensor haSentimentScore;
extern HASensor haSentimentCategory;
extern HASensor haTemperature;
extern HASensor haHumidity;
```

Aus firmware/src/sensor_manager.cpp Zeile 318 (Parse-Stelle, bereits vorhanden):
```cpp
if (doc["percentile"].is<float>()) appState.percentile = doc["percentile"].as<float>();
```

ArduinoJson 7 Semantik (verifiziert in VariantData.hpp:372): `is<float>()` prüft das
NumberBit und liefert daher auch für JSON-Integer (`0`, `1`) true. Die vorhandene
Prüfung ist damit robust gegen `"percentile": 0` bzw. `"percentile": 1` aus dem
Redis-Cache-Roundtrip — es braucht KEINE zusätzliche `is<int>()`-Prüfung.
</interfaces>

<decisions>
**Einheit: Prozent (0–100) statt Rohwert (0.0–1.0), Precision P0.**
Begründung: Der Backend-Wert `percentile` ist per Definition eine relative Position im
7-Tage-Fenster (`(score - min) / (max - min)`, gerundet auf 3 Stellen, geklemmt auf 0.0–1.0).
In HA ist `%` mit 0–100 die idiomatische Darstellung: Gauge- und History-Karten skalieren
automatisch korrekt, `unit_of_measurement: "%"` ist ein bekannter HA-Typ. `state_class`
wird NICHT gesetzt (ArduinoHA `HASensorNumber` wäre dafür nötig; hier reicht der
Text-basierte `HASensor` analog zu den bestehenden Sensoren). Umrechnung:
`appState.percentile * 100.0`, Ausgabe mit 0 Nachkommastellen.

**Speicherung bleibt bei 0.0–1.0 in `appState.percentile`.**
Begründung: `web_server.cpp:677` (`doc["percentile"] = appState.percentile;`) liefert den
Rohwert an das Web-Dashboard, das bereits auf 0.0–1.0 rechnet. Die *100-Umrechnung passiert
ausschließlich am Publish-Punkt, damit keine bestehende Konsumentin bricht.
</decisions>
</context>

<tasks>

<task type="auto">
  <name>Task 1: HA-Sensor haSentimentPercentile definieren und konfigurieren</name>
  <files>firmware/src/mqtt_handler.h, firmware/src/mqtt_handler.cpp</files>
  <action>
In `firmware/src/mqtt_handler.h` direkt nach `extern HASensor haSentimentCategory;` (Zeile 11)
die Zeile `extern HASensor haSentimentPercentile;` ergänzen — gleiche Gruppierung, kein neuer
Block.

In `firmware/src/mqtt_handler.cpp` im Globals-Block (Zeilen 14–26) direkt nach
`HASensor haSentimentCategory("sentiment_category");` definieren:
`HASensor haSentimentPercentile("sentiment_percentile", HASensor::PrecisionP0);`
Die uniqueId `sentiment_percentile` folgt dem snake_case-Muster der übrigen Sensoren.
PrecisionP0, weil der Wert als ganzzahliger Prozentwert publiziert wird (siehe decisions).

In `setupHA()` unmittelbar nach dem Block "Sentiment Kategorie (Text-Sensor)" (nach Zeile 271)
konfigurieren, im Stil der Nachbarblöcke inkl. deutschem Kommentar:
- `haSentimentPercentile.setName("Weltlage Perzentil");`
- `haSentimentPercentile.setIcon("mdi:chart-bell-curve-cumulative");`
- `haSentimentPercentile.setUnitOfMeasurement("%");`
Kein `setDeviceClass()` — es gibt keine passende HA-Device-Class für eine
Perzentil-Position; ohne Device-Class bleibt das gesetzte Icon erhalten.

In `sendInitialStates()` im Block "Sentiment (letzter bekannter Wert)" (nach Zeile 385)
den letzten bekannten Wert mitsenden:
`haSentimentPercentile.setValue(floatToString(appState.percentile * 100.0, 0).c_str());`
Damit erhält HA nach jedem Reconnect sofort einen Wert statt "unavailable". `floatToString()`
ist über `MoodlightUtils.h`/`debug.h` bereits in dieser Übersetzungseinheit verfügbar
(wird in Zeile 379/381/384 schon benutzt) — kein neuer Include nötig.

Keine weiteren Dateien anfassen. Kein Version-Bump in `config.h`.
  </action>
  <verify>
    <automated>cd firmware &amp;&amp; pio run 2>&amp;1 | tail -20 &amp;&amp; grep -v '^\s*//' src/mqtt_handler.cpp | grep -c "haSentimentPercentile" | grep -qx "5" &amp;&amp; grep -q "extern HASensor haSentimentPercentile" src/mqtt_handler.h</automated>
  </verify>
  <done>
`pio run` läuft fehlerfrei durch (SUCCESS). `haSentimentPercentile` erscheint in
`mqtt_handler.cpp` (ohne Kommentarzeilen) genau 5x: 1x Definition im Globals-Block,
3x Konfiguration in `setupHA()` (setName / setIcon / setUnitOfMeasurement) und
1x `setValue()` in `sendInitialStates()`. `mqtt_handler.h` enthält die extern-Deklaration.
  </done>
</task>

<task type="auto">
  <name>Task 2: Perzentil bei jedem Sentiment-Update an HA publizieren</name>
  <files>firmware/src/sensor_manager.cpp</files>
  <action>
In `firmware/src/sensor_manager.cpp` im extern-Block (Zeilen 14–17) nach
`extern HASensor haSentimentCategory;` ergänzen: `extern HASensor haSentimentPercentile;`

Erfolgsfall — in `getSentiment()` die vorhandene Parse-Zeile 318 erweitern. Aus
```
if (doc["percentile"].is<float>()) appState.percentile = doc["percentile"].as<float>();
```
wird ein Block, der zusätzlich publiziert:
- Feld prüfen mit `doc["percentile"].is<float>()` (deckt laut ArduinoJson-Semantik auch
  Integer-Werte ab, siehe interfaces).
- Rohwert in `appState.percentile` schreiben (unverändert, 0.0–1.0).
- Danach: nur wenn `appState.mqttEnabled && mqtt.isConnected()`, publizieren mit
  `haSentimentPercentile.setValue(floatToString(appState.percentile * 100.0, 0).c_str());`
- Fehlt das Feld, wird weder `appState.percentile` überschrieben noch publiziert — HA behält
  den letzten Wert. Kein zusätzliches Debug-Log im Fehlend-Fall (Log-Ringpuffer ist mit
  20 Einträgen knapp; die Perzentil-Info steht bereits im bestehenden Log aus Zeile 299–300).

Fehlerfall — im else-Zweig von `getSentiment()` (Zeilen 386–389, Block "Update HA values with
last known value anyway") analog zu `haSentimentScore`/`haSentimentCategory` ergänzen:
`haSentimentPercentile.setValue(floatToString(appState.percentile * 100.0, 0).c_str());`
Damit bleibt die Entität in HA auch bei API-Ausfall verfügbar statt auf "unavailable" zu fallen.

ESP32-Constraints beachten: keine zusätzlichen `String`-Zwischenvariablen anlegen —
`floatToString(...).c_str()` inline im Aufruf verwenden (temporäres String-Objekt lebt bis
zum Ende des Vollausdrucks, `setValue()` kopiert bzw. publiziert synchron; identisches Muster
wie Zeile 388/437/460). Keine neuen Includes nötig. Deutsche Kommentare mit korrekten Umlauten,
englische Bezeichner.
  </action>
  <verify>
    <automated>cd firmware &amp;&amp; pio run 2>&amp;1 | tail -20 &amp;&amp; grep -v '^\s*//' src/sensor_manager.cpp | grep -c "haSentimentPercentile.setValue" | grep -qx "2"</automated>
  </verify>
  <done>
`pio run` endet mit SUCCESS. `haSentimentPercentile.setValue(...)` steht an genau 2 Stellen in
`sensor_manager.cpp` (Erfolgsfall nach dem Parsen, Fehlerfall im else-Zweig), jeweils
mqtt-connected-geschützt bzw. innerhalb des bestehenden `mqttEnabled && mqtt.isConnected()`-Blocks.
`extern HASensor haSentimentPercentile;` ist deklariert.
  </done>
</task>

<task type="checkpoint:human-verify" gate="blocking">
  <name>Task 3: Perzentil-Entität in Home Assistant prüfen</name>
  <action>Nach dem Flashen der Firmware in Home Assistant verifizieren, dass die neue Entität existiert und einen plausiblen Prozentwert zeigt (Schritte siehe how-to-verify).</action>
  <what-built>
Neuer HA-Sensor `sentiment_percentile` ("Weltlage Perzentil", Einheit %, 0–100). Publiziert bei
jedem erfolgreichen Sentiment-Update, bei API-Fehlern (letzter bekannter Wert) und nach jedem
MQTT-(Re-)Connect via `sendInitialStates()`.
  </what-built>
  <how-to-verify>
1. Firmware auf das Gerät flashen (USB `pio run -t upload` oder OTA über den Update-Tab in
   setup.html auf http://192.168.0.37/setup.html) — Release/Build übernimmt der Orchestrator.
2. In Home Assistant unter Einstellungen → Geräte → "Moodlight" prüfen, ob die neue Entität
   `sensor.moodlight_weltlage_perzentil` (oder ähnlich benannt) erscheint.
3. Erwarteter Wert: ganze Zahl zwischen 0 und 100 mit Einheit "%".
4. Plausibilitätscheck: Wert gegen das Web-Dashboard des Geräts abgleichen
   (http://192.168.0.37/ bzw. mood.html zeigt das Perzentil als 0.0–1.0) — HA-Wert soll
   dem Hundertfachen entsprechen.
5. Optional: In HA auf "Weltlage aktualisieren" (Button-Entität) drücken und prüfen, ob sich
   der Perzentil-Wert nach dem Refresh aktualisiert.
  </how-to-verify>
  <resume-signal>Mit "approved" bestätigen oder Abweichungen beschreiben</resume-signal>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| Backend (`analyse.godsapp.de`) → ESP32 | JSON-Antwort aus dem Netz wird auf dem Gerät geparst |
| ESP32 → MQTT-Broker/Home Assistant | Sensorwert wird ins Heimnetz publiziert |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-RMF-01 | Tampering | `doc["percentile"]` in `getSentiment()` | mitigate | Typprüfung via `is<float>()` vor Zugriff; fehlendes/nicht-numerisches Feld führt zu No-Op statt undefiniertem Wert |
| T-RMF-02 | Denial of Service | Publish-Pfad in `sensor_manager.cpp` | mitigate | Publish nur bei `mqttEnabled && mqtt.isConnected()`; keine neue Heap-Allokation über das bestehende `floatToString()`-Muster hinaus |
| T-RMF-03 | Information Disclosure | MQTT-Topic mit Perzentilwert | accept | Wert ist ein aggregierter, öffentlich abrufbarer Backend-Kennwert ohne Personenbezug; MQTT liegt im privaten Heimnetz |
| T-RMF-SC | Tampering | Package-Installs (npm/pip/cargo) | n/a | Dieser Plan installiert keine Pakete — PlatformIO-Abhängigkeiten bleiben unverändert |
</threat_model>

<verification>
1. `cd firmware && pio run` → SUCCESS, keine neuen Warnungen zu `haSentimentPercentile`.
2. `grep -n "haSentimentPercentile" firmware/src/*.h firmware/src/*.cpp` → Treffer in
   `mqtt_handler.h` (1x extern), `mqtt_handler.cpp` (Definition + setName/setIcon/setUnit +
   setValue in sendInitialStates), `sensor_manager.cpp` (1x extern + 2x setValue).
3. `git diff --stat` → genau drei geänderte Dateien, keine Änderung an `config.h`
   (kein Version-Bump in diesem Plan).
4. Human-Verify-Checkpoint: Entität in Home Assistant sichtbar mit plausiblem Prozentwert.
</verification>

<success_criteria>
- `pio run` kompiliert die Firmware fehlerfrei.
- HA erhält bei jedem Sentiment-Update einen Perzentilwert von 0–100 mit Einheit `%`.
- Fehlt `percentile` in der API-Antwort, bleibt `appState.percentile` unverändert und es wird
  nichts publiziert (kein Sprung auf 0).
- Nach MQTT-Reconnect ist die Entität sofort mit dem letzten bekannten Wert versorgt.
- `appState.percentile` bleibt intern bei 0.0–1.0; das Web-Dashboard (`web_server.cpp:677`)
  bleibt unverändert funktionsfähig.
- Genau drei Dateien geändert, kein Version-Bump.
</success_criteria>

<output>
Create `.planning/quick/260730-rmf-perzentil-wert-aus-api-moodlight-current/260730-rmf-SUMMARY.md` when done
</output>
