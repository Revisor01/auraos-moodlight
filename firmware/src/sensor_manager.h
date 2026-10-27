#pragma once

#include "app_state.h"
#include <ArduinoJson.h>
#include <DHT.h>
#include <WiFiClient.h>

// DHT-Sensor und Sentiment-Abruf

// Hardware-Instanzen
extern DHT* dhtSensor;
extern WiFiClient wifiClientHTTP;

// DHT initialisieren (nach Settings-Load aufrufen)
void initDHT();

// Sentiment-Hilfsfunktionen
int mapSentimentToLED(float sentimentScore);
// apiCategory: von der API gelieferte Kategorie, sonst leer lassen (lokale Berechnung als Fallback)
void handleSentiment(float sentimentScore, const String &apiCategory = "");

// HTTP-Hilfsfunktion
bool safeHttpGet(const String &url, JsonDocument &doc);

// Sentiment-Abruf
void getSentiment();

// DHT-Sensorik
void readAndPublishDHT();

// Backend-Statistiken
bool fetchBackendStatistics(JsonDocument &doc, int hours = 168);
