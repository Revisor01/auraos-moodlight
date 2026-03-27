# Phase 26-01: Firmware-Stabilitätsfixes

## Änderungen

### app_state.h
- `wifiConnectedSince` — Zeitstempel der letzten WiFi-Verbindung für Stabilitäts-Hysterese
- `mqttRefreshPending` — Flag für verzögerten HA-Button-Refresh (statt HTTP im Callback)
- `mqttInitialStatesPending` — Zentrale Variable statt dreifach deklariertem `static bool`

### moodlight.cpp
- WiFi-Stabilitäts-Hysterese: 3s Pause nach Reconnect bevor `getSentiment()` und `readAndPublishDHT()` aufgerufen werden
- `mqttRefreshPending` Flag-Verarbeitung in `loop()` statt HTTP im MQTT-Callback

### mqtt_handler.cpp
- `onRefreshButtonPressed()`: HTTP-Request entfernt, setzt nur Flag
- `sendInitialStates()`: `pixels.Color()` ersetzt durch direkten `customColors[]`-Zugriff (Crash-Fix bei uninitialisiertem NeoPixel)
- `checkAndReconnectMQTT()`: `watchdog.feed()` in Reconnect-Schleife
- `connectMQTTOnStartup()`: `watchdog.feed()` in Startup-Schleife
- Dreifache `static bool initialStatesPending` → einheitlich `appState.mqttInitialStatesPending`

### sensor_manager.cpp
- `initDHT()`: Interner Pull-Up aktiviert (`INPUT_PULLUP`)
- `handleSentiment()`: LED-Steuerung entfernt, nur noch MQTT/HA-Werte; Kategorie aus API-Response
- `safeHttpGet()` und `fetchBackendStatistics()`: `watchdog.feed()` nach `http.GET()`
- `getSentiment()`: LED-Index direkt aus API, Kategorie aus API-Response

### wifi_manager.cpp
- `safeWiFiConnect()`: `watchdog.feed()` alle 500ms
- `checkAndReconnectWifi()`: `watchdog.feed()` alle 500ms in Reconnect-Schleife
- `wifiConnectedSince = millis()` an 3 Stellen bei erfolgreicher Verbindung

## Requirements erfüllt

- [x] FW-01: Watchdog während aller blockierenden Operationen gefüttert
- [x] FW-02: MQTT Refresh nur Flag, HTTP in loop()
- [x] FW-03: initialStatesPending als einzelne AppState-Variable
- [x] FW-04: WiFi-Hysterese 3s nach Reconnect
- [x] FW-05: WiFi.disconnect() Race Condition entschärft
