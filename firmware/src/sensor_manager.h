#pragma once

#include "app_state.h"
#include <ArduinoJson.h>
#include <DHT.h>
#include <WiFiClient.h>

// === Sensor & Sentiment Manager ===
// Verwaltet DHT-Sensorik und Sentiment-API-Abruf.
// Beide Funktionen holen externe Daten und aktualisieren AppState.

// Hardware-Instanzen
extern DHT dht;
extern WiFiClient wifiClientHTTP;

// Sentiment-Hilfsfunktionen
int mapSentimentToLED(float sentimentScore);
void handleSentiment(float sentimentScore);

// HTTP-Hilfsfunktion
bool safeHttpGet(const String &url, JsonDocument &doc);

// Sentiment-Abruf
void getSentiment();

// DHT-Sensorik
void readAndPublishDHT();

// Backend-Statistiken
bool fetchBackendStatistics(JsonDocument &doc, int hours = 168);

// String-Formatierung
void formatString(char *buffer, size_t maxLen, const char *format, ...);
