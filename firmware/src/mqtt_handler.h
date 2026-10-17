#pragma once

#include "app_state.h"
#include <ArduinoHA.h>

// === Externe HA-Entity-Deklarationen ===
// Definitionen liegen in mqtt_handler.cpp

extern HAMqtt mqtt;
extern HASensor haSentimentScore;
extern HASensor haSentimentCategory;
extern HASensor haSentimentPercentile;
extern HASensor haTemperature;
extern HASensor haHumidity;
extern HALight haLight;
extern HASelect haMode;
extern HAButton haRefreshSentiment;
extern HANumber haUpdateInterval;
extern HANumber haDhtInterval;
extern HASensor haUptime;
extern HASensor haWifiSignal;
extern HASensor haSystemStatus;
extern HASensor haSentimentRelative;
extern HASensor haSentimentLevel;

// === MQTT/HA-Funktionen ===

void setupHA();
void sendInitialStates();
void sendHeartbeat();
void checkAndReconnectMQTT();

// Geaenderte Farbe/Helligkeit (appState.haLightStateDirty) an HA melden — nur aus loop()
void syncHaLightState();

// Geaenderte Stufe "Weltlage relativ" (appState.relativeLevel) an HA melden — nur aus loop()
void syncHaRelativeLevel();

// MQTT beim Start einmalig verbinden (mit 5s Timeout)
void connectMQTTOnStartup();
