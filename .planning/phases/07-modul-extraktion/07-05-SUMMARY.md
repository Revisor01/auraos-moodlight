---
phase: 07-modul-extraktion
plan: "05"
subsystem: firmware/mqtt
tags: [mqtt, home-assistant, arduinoha, extraction, modularization]
dependency_graph:
  requires: ["07-04"]
  provides: ["mqtt_handler.h", "mqtt_handler.cpp"]
  affects: ["firmware/src/moodlight.cpp", "firmware/src/sensor_manager.cpp"]
tech_stack:
  added: []
  patterns: ["extern const for cross-TU constant visibility", "static-local ArduinoHA globals in mqtt_handler.cpp"]
key_files:
  created:
    - firmware/src/mqtt_handler.h
    - firmware/src/mqtt_handler.cpp
  modified:
    - firmware/src/moodlight.cpp
decisions:
  - "wifiClientHA, mac, HADevice als static in mqtt_handler.cpp — kein extern nötig da nur intern benutzt"
  - "MQTT_HEARTBEAT_INTERVAL als extern const in moodlight.cpp — C++ const hat interne Verlinkung, extern macht sie cross-TU sichtbar"
  - "wifiClientHTTP bleibt in moodlight.cpp — wird von sensor_manager über extern deklariert, nicht von mqtt_handler"
  - "sensor_manager.cpp extern-Deklarationen bleiben unverändert — Linker findet Definitionen jetzt in mqtt_handler.cpp"
metrics:
  duration: "8min"
  completed: "2026-03-26T13:31:16Z"
  tasks: 1
  files: 3
---

# Phase 07 Plan 05: MQTT Handler Extraktion Summary

**One-liner:** ArduinoHA entity globals (HAMqtt, HALight, HASensor etc.) und alle MQTT/HA-Funktionen aus moodlight.cpp in mqtt_handler.h/.cpp extrahiert — 11 Funktionen, 13 HA-Entities, pio run gruen.

## Tasks Completed

| Task | Description | Commit | Files |
|------|-------------|--------|-------|
| 1 | mqtt_handler.h/.cpp erstellen und MQTT/HA-Code verschieben | f45c943 | mqtt_handler.h, mqtt_handler.cpp, moodlight.cpp |

## What Was Built

**mqtt_handler.h** — Header mit extern-Deklarationen fuer alle HA-Entities (HAMqtt, HASensor, HALight, HASelect, HAButton, HANumber) und Funktionsdeklarationen fuer setupHA, sendInitialStates, sendHeartbeat, checkAndReconnectMQTT.

**mqtt_handler.cpp** — Implementierung:
- HA-Globals: wifiClientHA (static), mac (static), device (static), HAMqtt mqtt, HASensor (5x), HALight, HASelect, HAButton, HANumber (2x)
- Callbacks: onStateCommand, onBrightnessCommand, onRGBColorCommand, onModeCommand, onUpdateIntervalCommand, onDHTIntervalCommand, onRefreshButtonPressed
- Funktionen: sendHeartbeat, setupHA, sendInitialStates, checkAndReconnectMQTT
- extern-Deklarationen fuer appState, pixels, dht, wifiClientHTTP, debug(), floatToString(), updateLEDs(), setStatusLED(), saveSettings(), handleSentiment(), MQTT_HEARTBEAT_INTERVAL

**moodlight.cpp** — HA-Globals entfernt, nur wifiClientHTTP verbleibt (benutzt von sensor_manager). #include "mqtt_handler.h" hinzugefuegt. MQTT_HEARTBEAT_INTERVAL zu extern const gemacht.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] MQTT_HEARTBEAT_INTERVAL nicht cross-TU sichtbar**
- **Found during:** Task 1 (Linkerfehler)
- **Issue:** `MQTT_HEARTBEAT_INTERVAL` war als plain `const unsigned long` in moodlight.cpp definiert — C++ const hat interne Verlinkung, mqtt_handler.cpp konnte sie per `extern` nicht finden
- **Fix:** `const unsigned long MQTT_HEARTBEAT_INTERVAL` zu `extern const unsigned long MQTT_HEARTBEAT_INTERVAL` in moodlight.cpp
- **Files modified:** firmware/src/moodlight.cpp
- **Commit:** f45c943 (im selben Commit enthalten)

## Decisions Made

- `wifiClientHA`, `mac`, `HADevice` als `static` in mqtt_handler.cpp — werden nur intern von mqtt_handler benutzt, kein extern nötig
- `wifiClientHTTP` bleibt in moodlight.cpp weil sensor_manager.cpp es per extern referenziert und der Linker die Definition dort erwartet
- sensor_manager.cpp extern-Deklarationen fuer `HAMqtt mqtt`, `HASensor haSentimentScore` etc. bleiben unveraendert — Linker findet jetzt die Definitionen in mqtt_handler.cpp (korrekt)
- `MQTT_HEARTBEAT_INTERVAL` als `extern const` — konsistent mit anderen cross-TU-Konstanten (MAX_RECONNECT_DELAY, STATUS_LED_GRACE_MS etc.)

## Verification

```
grep -c "void setupHA\|void sendInitialStates\|void sendHeartbeat\|void checkAndReconnectMQTT\|void onStateCommand\|void onBrightnessCommand" firmware/src/mqtt_handler.cpp
=> 6

grep "HAMqtt mqtt" firmware/src/mqtt_handler.cpp
=> HAMqtt mqtt(wifiClientHA, device);

grep "HAMqtt" firmware/src/moodlight.cpp
=> (kein Treffer — Definition entfernt)

pio run
=> SUCCESS
```

## Self-Check: PASSED

- [x] firmware/src/mqtt_handler.h exists
- [x] firmware/src/mqtt_handler.cpp exists
- [x] commit f45c943 exists
- [x] pio run succeeds
