---
status: awaiting_human_verify
trigger: "ESP32 crashes 467 times/day despite previous fixes. Weak WiFi environment."
created: 2026-03-27T00:00:00Z
updated: 2026-04-04T01:00:00Z
---

## Current Focus

hypothesis: Mehrere schwerwiegende Probleme, die zusammenwirken und 467 Reboots/Tag verursachen
test: Code-Analyse aller Module abgeschlossen, Root Causes identifiziert
expecting: Systematische Instabilitaet durch (1) MQTT-Callbacks die saveSettings() direkt aufrufen und NVS+LittleFS-I/O im Callback-Kontext ausfuehren, (2) pixels.show() direkt in Callbacks ohne Mutex, (3) WiFi.setSleep(false) wird bei jedem Reconnect-Versuch NICHT gesetzt, (4) String-Fragmentierung durch debug()-Aufrufe mit String-Konkatenation, (5) Fehlender Schutz bei HTTP-Timeout wenn WiFi waehrend Request abbricht
next_action: Alle identifizierten Root Causes fixen

## Symptoms

expected: ESP32 runs stable for hours/days without rebooting, even with weak WiFi signal.
actual: Device reboots very frequently — 467 Home Assistant state changes in one day indicate constant crash/reboot cycles.
errors: Unknown — no serial log available. Previous crashes showed Guru Meditation Error (LoadProhibited) and TG1WDT_SYS_RESET.
reproduction: Continuous operation with weak WiFi signal causes repeated crashes.
timeline: Problem persists after two rounds of fixes. Hardware is confirmed good.

## Eliminated

- hypothesis: WDT nicht gefuettert in blocking loops
  evidence: watchdog.feed() wurde in allen blockierenden while-Schleifen hinzugefuegt (WiFi reconnect, MQTT reconnect, MQTT startup). Problem besteht weiter.
  timestamp: 2026-03-27

- hypothesis: MQTT refresh im Callback-Kontext fuehrt HTTP aus
  evidence: onRefreshButtonPressed setzt nur noch Flag (mqttRefreshPending). Problem besteht weiter.
  timestamp: 2026-03-27

- hypothesis: WiFi-Hysterese fehlt (HTTP zu frueh nach Reconnect)
  evidence: 3s Hysterese wurde implementiert (wifiConnectedSince). Problem besteht weiter mit 467 Reboots.
  timestamp: 2026-03-27

- hypothesis: initialStatesPending Logikbug
  evidence: Wurde zu AppState-Flag konsolidiert (mqttInitialStatesPending). Problem besteht weiter.
  timestamp: 2026-03-27

## Evidence

- timestamp: 2026-04-04T00:00:00Z
  checked: Vollstaendige Code-Analyse aller Firmware-Module (moodlight.cpp, wifi_manager.cpp, mqtt_handler.cpp, sensor_manager.cpp, led_controller.cpp, web_server.cpp, MoodlightUtils.cpp, settings_manager.cpp, debug.cpp, app_state.h, config.h)
  found: |
    SECHS kritische Root Causes identifiziert:

    ROOT CAUSE 1 — MQTT-Callbacks rufen saveSettings() DIREKT auf (SCHWERWIEGEND):
    In mqtt_handler.cpp rufen onStateCommand(), onBrightnessCommand(), onRGBColorCommand(),
    onModeCommand(), onUpdateIntervalCommand(), onDHTIntervalCommand() alle DIREKT saveSettings() auf.
    saveSettings() schreibt erst in LittleFS (JSON-Datei) und dann in NVS (Preferences).
    Diese Callbacks werden aus mqtt.loop() heraus aufgerufen. Waehrend saveSettings() laeuft:
    - LittleFS-I/O kann blockieren (Flash-Write)
    - Preferences.begin()/end() ist NVS-I/O
    - Kein watchdog.feed() waehrend saveSettings()
    - Wenn WiFi waehrend des Flash-Write abbricht, kann der WiFi-Stack inkonsistent werden
    Bei SCHWACHEM WiFi: HA sendet retained-Messages bei jedem Reconnect. Das bedeutet:
    JEDER MQTT-Reconnect triggert Callbacks → saveSettings() → Flash-Write → potentieller Crash.
    Bei 467 Reboots/Tag = 467 MQTT-Reconnects = 467x saveSettings() aus Callback-Kontext.

    ROOT CAUSE 2 — onStateCommand() ruft pixels.clear()+pixels.show() DIREKT auf (SCHWERWIEGEND):
    mqtt_handler.cpp Zeile 54-55: pixels.clear(); pixels.show();
    pixels.show() deaktiviert Interrupts fuer die NeoPixel-Uebertragung (~300us pro LED, 12 LEDs = ~3.6ms).
    Dies geschieht im mqtt.loop()-Callback-Kontext ohne Mutex und ohne WiFi-Schutz.
    Gleichzeitig kann processLEDUpdates() im Hauptloop ebenfalls pixels.show() aufrufen.
    Doppeltes pixels.show() = Interrupt-Konflikt = Crash.

    ROOT CAUSE 3 — WiFi Power Save wird beim VERSUCH nicht deaktiviert (MITTEL):
    In checkAndReconnectWifi(): esp_wifi_set_ps(WIFI_PS_NONE) wird nur aufgerufen wenn
    Reconnect ERFOLGREICH ist. Aber ESP32-Arduino setzt intern WIFI_PS_MIN_MODEM nach WiFi.begin().
    Bei schwachem Signal scheitert Reconnect haeufer → Power Save bleibt aktiv → noch schlechteres Signal.
    ZUSAETZLICH: processLEDUpdates() setzt esp_wifi_set_ps(WIFI_PS_MIN_MODEM) vor JEDEM pixels.show()
    und dann WIFI_PS_NONE danach. Bei haeufigen LED-Updates kann Power Save hin-und-her-schalten.

    ROOT CAUSE 4 — Massive String-Heap-Fragmentierung durch debug() (MITTEL):
    debug() in debug.cpp erstellt bei JEDEM Aufruf einen neuen String (logEntry) und speichert ihn
    im Ringpuffer (appState.logBuffer[20]). Das sind 20 String-Objekte die staendig realloced werden.
    Bei jedem debug()-Aufruf wird ein alter String freigegeben und ein neuer allokiert.
    ESP32 hat ~320KB RAM. Die String-Operationen fragmentieren den Heap.
    ZUSAETZLICH: Viele debug()-Aufrufe verwenden String-Konkatenation mit +, was temporaere Strings
    auf dem Heap erzeugt (z.B. debug(String(F("text")) + variable + F(" more text"))).
    Bei 5+ debug()-Aufrufen pro Loop-Iteration = 100+ String-Allokationen pro Sekunde.

    ROOT CAUSE 5 — pulseCurrentColor() ruft pixels.setBrightness()+pixels.show() DIREKT auf (MITTEL):
    led_controller.cpp Zeile 253-255 und 268-269: Direkter pixels.show()-Aufruf
    OHNE Mutex und OHNE WiFi-Schutz (esp_wifi_set_ps()).
    Diese Funktion ist allerdings NICHT mehr in loop() aufgerufen — updatePulse() verwendet stattdessen
    den Mutex-geschuetzten Pfad. Aber pulseCurrentColor() existiert noch und koennte theoretisch
    von einem Web-Handler aufgerufen werden. (Risiko: niedrig da nicht aufgerufen)

    ROOT CAUSE 6 — testapi und refresh Web-Handler blockieren ohne WDT-Feed (MITTEL):
    web_server.cpp /testapi (Zeile 1370ff) und /refresh (Zeile 1600ff) fuehren HTTP-Requests
    mit 10s Timeout aus, ohne watchdog.feed() nach dem blockierenden http.GET().
    Der /refresh-Handler fuehrt sogar erst server.send() aus und DANN den HTTP-Request —
    aber der Client hat schon seine Antwort, der Server blockiert aber weiter.

  implication: |
    Die Kombination aus ROOT CAUSE 1 + ROOT CAUSE 2 erklaert die 467 Reboots/Tag:
    Schwaches WiFi → haeufige Disconnects → MQTT disconnects → MQTT reconnects →
    HA sendet retained Messages → Callbacks feuern → saveSettings() (Flash-I/O) +
    pixels.show() (Interrupt-deaktivierung) → Crash/WDT-Timeout → Reboot → Zyklus wiederholt sich.

## Resolution

root_cause: |
  Primaer: MQTT-Callbacks fuehren schwere I/O-Operationen (saveSettings, pixels.show) direkt im
  mqtt.loop()-Callback-Kontext aus. Bei schwachem WiFi reconnectet MQTT haeufig, und jeder
  Reconnect triggert retained-Message-Callbacks. Die Flash-I/O in saveSettings() plus
  Interrupt-Deaktivierung durch pixels.show() im Callback-Kontext fuehren zu Crashes.

  Sekundaer: String-Heap-Fragmentierung durch debug()-Ringpuffer und WiFi-Power-Save-Flapping
  durch LED-Updates verschaerfen die Instabilitaet.

fix: |
  Runde 3 — Systematische Stabilisierung:

  1. mqtt_handler.cpp: ALLE 6 HA-Callbacks (onStateCommand, onBrightnessCommand, onRGBColorCommand,
     onModeCommand, onUpdateIntervalCommand, onDHTIntervalCommand) von saveSettings() auf
     verzoegerte Speicherung umgestellt (settingsNeedSaving=true). Flash-I/O findet jetzt
     NUR im Hauptloop statt, nie im mqtt.loop()-Callback-Kontext.

  2. mqtt_handler.cpp: onStateCommand() ruft nicht mehr pixels.clear()+pixels.show() direkt auf,
     sondern updateLEDs() (Mutex-geschuetzter Pfad). Verhindert Interrupt-Konflikt mit
     processLEDUpdates().

  3. mqtt_handler.cpp: onRGBColorCommand() berechnet Farbe mit Bit-Shift statt pixels.Color()
     (vermeidet Abhaengigkeit von NeoPixel-Bibliothek im Callback-Kontext).

  4. mqtt_handler.cpp: sendInitialStates() liest DHT-Sensor nicht mehr direkt, sondern
     sendet letzte bekannte Werte. Vermeidet I/O im Reconnect-Kontext.

  5. led_controller.cpp: WiFi Power Save Flapping entfernt. processLEDUpdates() toggelt
     nicht mehr zwischen WIFI_PS_MIN_MODEM und WIFI_PS_NONE bei jedem pixels.show().
     Power Save bleibt stabil auf WIFI_PS_NONE.

  6. led_controller.cpp: initFirstLEDUpdate() verwendet 100ms-Timeout statt portMAX_DELAY
     fuer Semaphore-Take. Verhindert potentielle Deadlocks.

  7. app_state.h + debug.cpp: Log-Ringpuffer von String[20] auf char[20][192] umgestellt.
     Eliminiert 20 String-Objekte die bei jedem debug()-Aufruf realloced wurden.
     Reduziert Heap-Fragmentierung signifikant.

  8. web_server.cpp: /refresh-Handler blockiert nicht mehr mit eigenem HTTP-Request,
     sondern setzt mqttRefreshPending-Flag (wie HA-Button). Eliminiert blockierenden
     HTTP-Call im Web-Server-Kontext.

  9. web_server.cpp: toggle-light verwendet updateLEDs() statt pixels.clear()+pixels.show().

  10. moodlight.cpp: watchdog.autoFeed() ersetzt durch watchdog.feed() — WDT wird jetzt
      bei JEDEM Loop-Durchlauf gefuettert statt nur alle 15s. Zusaetzlich watchdog.feed()
      nach getSentiment() und readAndPublishDHT().

  Build: SUCCESS — Flash 81.6%, RAM 29.2%

verification: ausstehend — Geraet muss geflasht werden
files_changed:
  - firmware/src/mqtt_handler.cpp
  - firmware/src/led_controller.cpp
  - firmware/src/app_state.h
  - firmware/src/debug.cpp
  - firmware/src/web_server.cpp
  - firmware/src/moodlight.cpp
