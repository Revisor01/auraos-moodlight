---
status: awaiting_human_verify
trigger: "Die Moodlight-Lampe fängt manchmal an rhythmisch zu blinken — ein leichtes Auf- und Anschwellen der Helligkeit in einem klaren Rhythmus. Das Gerät bleibt erreichbar (192.168.0.37). Kein klarer Auslöser erkennbar, passiert zufällig."
created: 2026-04-05T00:00:00Z
updated: 2026-04-05T00:00:00Z
---

## Current Focus
<!-- OVERWRITE on each update - reflects NOW -->

hypothesis: BESTÄTIGT — isPulsing wird bei jedem Sentiment-Abruf auf true gesetzt (absichtlicher Breathing-Effekt als Fortschrittsindikator)
test: Code-Analyse komplett
expecting: Fix: isPulsing beim Sentiment-Abruf nicht setzen (oder Feature entfernen)
next_action: Fix implementieren — isPulsing = true in getSentiment() entfernen, da der blocking HTTP-Request sowieso den Loop blockiert und das Pulsing während des Requests nie sichtbar ist

## Symptoms
<!-- Written during gathering, then IMMUTABLE -->

expected: LEDs zeigen stabile Farbe ohne Helligkeitsschwankungen
actual: Rhythmisches Auf- und Abschwellen der LED-Helligkeit (Pulsieren/Breathing-Effekt), zufällig auftretend
errors: Keine Fehlermeldungen bekannt, Gerät bleibt erreichbar
reproduction: Zufällig, kein klarer Trigger. Passiert manchmal.
started: Schon immer so gewesen, seit erstem Einsatz der Firmware

## Eliminated
<!-- APPEND only - prevents re-investigating -->

- hypothesis: Hardware-Problem mit NeoPixel oder Pin
  evidence: Code zeigt klare Software-Ursache (isPulsing-Flag)
  timestamp: 2026-04-05

- hypothesis: Race condition zwischen Tasks
  evidence: ESP32 Arduino läuft single-threaded im Loop, kein paralleles Pulsing
  timestamp: 2026-04-05

- hypothesis: Status-LED beeinflusst Haupt-LEDs
  evidence: Status-LED ist nur der letzte Pixel (statusLedIndex = numLeds-1), Pulsing betrifft Gesamt-Helligkeit via setBrightness()
  timestamp: 2026-04-05

## Evidence
<!-- APPEND only - facts discovered -->

- timestamp: 2026-04-05
  checked: firmware/src/led_controller.cpp — updatePulse() Funktion
  found: Sinuswellen-Breathing-Animation via setBrightness() mit DEFAULT_WAVE_DURATION=10000ms, min=20, max=255. Auto-disable nach 3 Zyklen (30s).
  implication: Das ist ein bewusst implementierter Breathing-Effekt

- timestamp: 2026-04-05
  checked: firmware/src/sensor_manager.cpp — getSentiment() Zeile 279
  found: appState.isPulsing = true wird bei JEDEM Sentiment-Abruf gesetzt (alle 30 Minuten oder Force-Refresh)
  implication: Das ist der einzige nicht-manuelle Auslöser des Pulsens

- timestamp: 2026-04-05
  checked: firmware/src/mqtt_handler.cpp:183 und web_server.cpp:1608
  found: isPulsing = true auch bei manuellem Force-Refresh über HA-Button oder /refresh Endpunkt
  implication: Jeder Refresh-Trigger löst Pulsieren aus

- timestamp: 2026-04-05
  checked: sensor_manager.cpp Zeile 385-387 (Ende von getSentiment())
  found: isPulsing = false wird nach dem blocking HTTP-Request gesetzt, danach updateLEDs()
  implication: Da safeHttpGet() synchron/blocking ist, läuft updatePulse() während des HTTP-Requests NICHT. Das Pulsieren beginnt also NACH dem Request — weil isPulsing=false nach dem Request gesetzt wird, aber updatePulse() vorher möglicherweise noch läuft.

- timestamp: 2026-04-05
  checked: Ablauf-Sequenz in loop() nach getSentiment()-Rückkehr
  found: getSentiment() → isPulsing=false → updateLEDs() → [zurück in loop()] → updateStatusLED() → updatePulse() (prüft isPulsing=false → tut nichts)
  implication: Kein Race. Aber: isPulsing=true wird VOR dem HTTP-Request gesetzt. Da der Loop blockiert während HTTP läuft, kann updatePulse() während des Requests nicht ausgeführt werden. Das Pulsieren ist daher NACH dem Request sichtbar — aber nur für einen Bruchteil eines Loop-Zyklus bis isPulsing=false gesetzt wird. WARUM ist es dann sichtbar?

- timestamp: 2026-04-05
  checked: Echter Ablauf bei schnellem HTTP-Response (< 50ms)
  found: isPulsing=true → safeHttpGet() kehrt schnell zurück → isPulsing=false → updateLEDs() setzt ledUpdatePending=true → processLEDUpdates() schreibt Brightness X → updatePulse() tut nichts. Kein Pulsing sichtbar.
  implication: Pulsing ist nur bei LANGSAMEM HTTP-Request sichtbar (z.B. Server antwortet nach 2-5s)

- timestamp: 2026-04-05
  checked: Timing-Problem bei langsamem HTTP-Response
  found: Loop blockiert für die gesamte HTTP-Dauer (bis 10s Timeout). Während dieser Zeit: keine LED-Updates. Nach Rückkehr: isPulsing=false sofort, LEDs auf Zielfarbe. Kein Pulsing.
  implication: Das Pulsieren kann auf diesem Pfad NICHT entstehen — der HTTP-Block verhindert jeden Loop-Aufruf.

- timestamp: 2026-04-05
  checked: MQTT Force-Refresh Pfad (mqtt_handler.cpp:183)
  found: onRefreshButtonPressed() setzt isPulsing=true und mqttRefreshPending=true. Der eigentliche Abruf erfolgt im NÄCHSTEN loop()-Durchlauf durch getSentiment() (wenn lastMoodUpdate=0). getSentiment() setzt isPulsing=true ERNEUT und pulseStartTime neu. Aber zwischen dem Setzen in onRefreshButtonPressed() und dem nächsten getSentiment()-Aufruf: updatePulse() wird in loop() aufgerufen und pulsiert aktiv! Das ist der Pfad wo Pulsing sichtbar wird.
  implication: Bei manuellem HA-Refresh: isPulsing=true wird in MQTT-Callback gesetzt, danach kehrt mqtt.loop() zurück, dann läuft updatePulse() aktiv bis getSentiment() antwortet und isPulsing=false setzt.

- timestamp: 2026-04-05
  checked: Normaler 30-Minuten-Abruf ohne Force-Refresh
  found: getSentiment() setzt isPulsing=true direkt VOR safeHttpGet(). Loop ist blockiert während HTTP. Nach Rückkehr: isPulsing=false. updatePulse() läuft nie mit isPulsing=true. KEIN Pulsing auf diesem Pfad.
  implication: Normaler Abruf verursacht kein Pulsing

- timestamp: 2026-04-05
  checked: /refresh Web-Endpoint (web_server.cpp:1608)
  found: Setzt mqttRefreshPending=true und isPulsing=true im HTTP-Handler-Kontext. Nach server.handleClient() kehrt die Routine zurück, dann läuft updatePulse() aktiv — gleicher Mechanismus wie MQTT-Callback.
  implication: Web-UI Force-Refresh löst Pulsing aus (und HA-Button auch)

- timestamp: 2026-04-05
  checked: pulseCurrentColor() Funktion
  found: Nirgendwo aufgerufen — toter Code
  implication: Kein Einfluss

## Resolution

root_cause: Das Pulsieren (Breathing-Effekt) ist absichtlich implementiert als visuelles Feedback für Sentiment-Abrufe. isPulsing=true wird an 3 Stellen gesetzt: (1) In getSentiment() direkt vor dem blocking HTTP-Request — hier ist Pulsing nie sichtbar, da der Loop während HTTP blockiert. (2) In onRefreshButtonPressed() (MQTT-Callback) und im /refresh Web-Handler — hier ist Pulsing zwischen dem Setzen des Flags und dem tatsächlichen getSentiment()-Aufruf sichtbar, weil updatePulse() in den dazwischenliegenden Loop-Zyklen ausgeführt wird. Das "zufällige" Auftreten entspricht exakt den Force-Refresh-Triggern über HA-Button oder Web-UI.

fix: isPulsing=true in getSentiment() (sensor_manager.cpp:279-280) und pulseStartTime entfernen, da der blocking HTTP-Request den Loop sowieso blockiert und das Pulsing auf diesem Pfad nie sichtbar ist. Für die MQTT/Web-Refresh-Pfade: isPulsing=true ebenfalls entfernen, da das Feature nicht erwünscht ist (User sieht stabiles Licht erwartet). pulseCurrentColor() als toten Code entfernen.

verification:
files_changed: [firmware/src/sensor_manager.cpp, firmware/src/mqtt_handler.cpp, firmware/src/web_server.cpp, firmware/src/led_controller.cpp, firmware/src/led_controller.h]
