#include "sensor_manager.h"
#include "led_controller.h"
#include "debug.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <DHT.h>
#include <ArduinoHA.h>
#include <stdarg.h>

// Globals aus moodlight.cpp
extern AppState appState;
extern HAMqtt mqtt;
extern HASensor haSentimentScore;
extern HASensor haSentimentCategory;
extern HASensor haTemperature;
extern HASensor haHumidity;

// Hardware-Instanzen — definiert in diesem Modul
DHT* dhtSensor = nullptr;
WiFiClient wifiClientHTTP;

// DHT mit dem tatsächlichen Pin aus den Settings initialisieren
void initDHT() {
    if (dhtSensor) {
        delete dhtSensor;
    }
    dhtSensor = new DHT(appState.dhtPin, DHT22);
    dhtSensor->begin();
    debug(String(F("DHT22 initialisiert auf Pin ")) + String(appState.dhtPin));
}

// === Map Sentiment Score (-1 bis +1) zu LED Index (0-4) ===
int mapSentimentToLED(float sentimentScore)
{
    // Angepasste Schwellenwerte für die Stimmungsfarben
    if (sentimentScore >= 0.20)
        return 4; // sehr positiv
    if (sentimentScore >= 0.10)
        return 3; // positiv
    if (sentimentScore >= -0.20)
        return 2; // neutral
    if (sentimentScore >= -0.50)
        return 1; // negativ
    return 0;     // sehr negativ
}

// === Verarbeite den empfangenen Sentiment Score (-1 bis +1) ===
void handleSentiment(float sentimentScore)
{
    sentimentScore = constrain(sentimentScore, -1.0, 1.0);

    // Sende Score an HA nur bei Änderung
    if (abs(sentimentScore - appState.sentimentScore) >= 0.01 || !appState.initialAnalysisDone)
    {
        String haValue = floatToString(sentimentScore, 2);

        if (appState.mqttEnabled && mqtt.isConnected())
        {
            haSentimentScore.setValue(haValue.c_str());
        }

        appState.sentimentScore = sentimentScore;
        debug("Neuer Sentiment Score: " + haValue);
    }

    int newLedIndex = mapSentimentToLED(sentimentScore);

    String categoryText;
    switch (newLedIndex)
    {
    case 4:
        categoryText = "sehr positiv";
        break;
    case 3:
        categoryText = "positiv";
        break;
    case 2:
        categoryText = "neutral";
        break;
    case 1:
        categoryText = "negativ";
        break;
    case 0:
        categoryText = "sehr negativ";
        break;
    default:
        categoryText = "unbekannt";
        break;
    }

    // Sende Kategorie an HA nur bei Änderung
    if (categoryText != appState.sentimentCategory || !appState.initialAnalysisDone)
    {
        if (appState.mqttEnabled && mqtt.isConnected())
        {
            haSentimentCategory.setValue(categoryText.c_str());
        }

        appState.sentimentCategory = categoryText;
        debug("Neue Sentiment Kategorie: " + categoryText);
    }

    // Aktualisiere LED Index & LEDs nur bei Änderung
    if (newLedIndex != appState.currentLedIndex || !appState.initialAnalysisDone)
    { // Auch beim ersten Mal aktualisieren
        debug("LED Index geändert von " + String(appState.currentLedIndex) + " zu " + String(newLedIndex) + " (" + categoryText + ")");
        appState.currentLedIndex = newLedIndex;
        appState.lastLedIndex = appState.currentLedIndex;
        if (appState.autoMode && appState.lightOn)
        {
            updateLEDs(); // Ruft optimierte Funktion auf
        }
    }

    // API ist erreichbar
    appState.sentimentAPIAvailable = true;
    appState.consecutiveSentimentFailures = 0;
    appState.lastSuccessfulSentimentUpdate = millis();
}

// Safer string handling function
void formatString(char *buffer, size_t maxLen, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, maxLen, format, args);
    va_end(args);
}

// === Lade Backend-Statistiken ===
bool fetchBackendStatistics(JsonDocument &doc, int hours)
{
    if (WiFi.status() != WL_CONNECTED) {
        debug(F("WiFi nicht verbunden - kann keine Backend-Statistiken laden"));
        return false;
    }

    String statsUrl = String(DEFAULT_STATS_API_URL) + "?hours=" + String(hours);
    debug(String(F("Lade Statistiken von Backend: ")) + statsUrl);

    HTTPClient http;
    http.setReuse(false);
    http.setUserAgent("MoodlightClient/1.0");

    // Ensure any previous connection is closed
    if (wifiClientHTTP.connected()) {
        wifiClientHTTP.stop();
        delay(10);
    }

    if (!http.begin(wifiClientHTTP, statsUrl)) {
        debug(F("HTTP Begin fehlgeschlagen für Backend-Statistiken"));
        return false;
    }

    http.setTimeout(15000);  // Längeres Timeout für Statistiken

    int httpCode = 0;
    try {
        httpCode = http.GET();
        debug(String(F("Backend-Statistiken HTTP Code: ")) + String(httpCode));
    } catch (...) {
        debug(F("Exception während HTTP GET für Backend-Statistiken"));
        http.end();
        return false;
    }

    if (httpCode == HTTP_CODE_OK) {
        // Parse JSON direkt vom Stream wie safeHttpGet
        DeserializationError error = deserializeJson(doc, http.getStream());
        http.end();

        if (error) {
            debug(String(F("JSON Parsing Fehler bei Backend-Statistiken: ")) + error.c_str());
            return false;
        }

        debug(F("Backend-Statistiken erfolgreich geladen"));
        return true;
    } else {
        debug(String(F("HTTP Fehler beim Laden der Backend-Statistiken: ")) + httpCode);
        http.end();
        return false;
    }

    // Force close WiFi client
    if (wifiClientHTTP.connected()) {
        wifiClientHTTP.stop();
    }

    return false;
}

// === Sicherer HTTP GET mit JSON-Parsing ===
bool safeHttpGet(const String &url, JsonDocument &doc)
{
    bool success = false;

    debug(String(F("Making safe HTTP request to: ")) + url);

    // Create client in local scope
    HTTPClient http;
    http.setReuse(false);
    http.setUserAgent("MoodlightClient/1.0");

    // Ensure any previous connection is closed
    if (wifiClientHTTP.connected()) {
        wifiClientHTTP.stop();
        delay(10);
    }

    // Begin HTTP connection with proper error handling
    if (http.begin(wifiClientHTTP, url)) {
        http.setTimeout(10000);

        // Make request with error handling
        int httpCode = 0;
        try {
            httpCode = http.GET();
            debug(String(F("HTTP response code: ")) + httpCode);
        }
        catch (...) {
            debug(F("Exception during HTTP GET"));
        }

        if (httpCode == HTTP_CODE_OK) {
            // Parse JSON directly from stream to avoid memory copies
            DeserializationError error = deserializeJson(doc, http.getStream());

            if (!error) {
                success = true;
                debug(F("JSON parsed successfully"));
            }
            else {
                debug(String(F("JSON parse error: ")) + error.c_str());
            }
        }

        // Always end the connection
        http.end();
    }
    else {
        debug(F("Failed to begin HTTP connection"));
    }

    // Force close WiFi client if somehow still connected
    if (wifiClientHTTP.connected()) {
        wifiClientHTTP.stop();
    }

    return success;
}

// === Rufe Sentiment vom Backend ab (-1 bis +1) ===
void getSentiment()
{
    static bool isUpdating = false;
    unsigned long currentMillis = millis();

    // Debug output for Interval Status (reduced frequency)
    static unsigned long lastIntervalDebug = 0;
    if (currentMillis - lastIntervalDebug >= 300000)
    {
        debug(String(F("Sentiment Interval Status: ")) + String(currentMillis - appState.lastMoodUpdate) + F("/") + String(appState.moodUpdateInterval) + F("ms"));
        lastIntervalDebug = currentMillis;
    }

    // Check for API timeout logic
    if (appState.sentimentAPIAvailable && appState.lastSuccessfulSentimentUpdate > 0 && currentMillis - appState.lastSuccessfulSentimentUpdate > SENTIMENT_FALLBACK_TIMEOUT)
    {
        debug(F("API-Timeout: Kein erfolgreicher Sentiment-Abruf seit über einer Stunde."));
        debug(F("Wechsel in Neutral-Modus."));
        appState.sentimentAPIAvailable = false;
        handleSentiment(0.0);
        setStatusLED(2);
    }

    // Avoid concurrent updates
    if (isUpdating)
        return;

    // Check if it's time for an update
    if (!(currentMillis - appState.lastMoodUpdate >= appState.moodUpdateInterval || !appState.initialAnalysisDone))
        return;

    debug(F("Starte Sentiment-Abruf..."));
    isUpdating = true;
    appState.isPulsing = true;
    appState.pulseStartTime = currentMillis;

    // v9.0: headlines_per_source parameter removed - not used by new /api/moodlight/* endpoints

    // Use static document to avoid heap fragmentation
    static JsonDocument doc;
    doc.clear();

    // Use the safe HTTP request function
    bool success = safeHttpGet(appState.apiUrl, doc);

    // Always update timing state
    appState.lastMoodUpdate = currentMillis;

    if (success && doc["sentiment"].is<float>())
    {
        float receivedSentiment = doc["sentiment"].as<float>();
        debug(String(F("Sentiment empfangen: ")) + String(receivedSentiment, 2));

        // Phase 18: led_index aus API-Response lesen (dynamische Skalierung)
        // Fallback auf mapSentimentToLED() wenn Feld fehlt (altes Backend)
        int apiLedIndex = -1;
        if (doc["led_index"].is<int>())
        {
            apiLedIndex = doc["led_index"].as<int>();
            apiLedIndex = constrain(apiLedIndex, 0, 4);

            // Fallback-Schwellwerte signalisiert vom Backend
            if (doc["thresholds"]["fallback"].is<bool>() && doc["thresholds"]["fallback"].as<bool>())
            {
                debug(F("Hinweis: Backend nutzt Fallback-Schwellwerte (weniger als 3 historische Datenpunkte)"));
            }
            debug(String(F("LED-Index aus API: ")) + String(apiLedIndex) +
                  String(F(" (Perzentil: ")) + String(doc["percentile"].as<float>(), 2) + String(F(")")));
        }
        else
        {
            // Altes Backend ohne led_index-Feld — lokale Berechnung als Fallback
            apiLedIndex = mapSentimentToLED(receivedSentiment);
            debug(F("led_index nicht in Response — lokaler Fallback über mapSentimentToLED()"));
        }

        // handleSentiment() mit exaktem Rohwert aufrufen (MQTT + HA-Werte)
        // (setzt currentLedIndex intern via mapSentimentToLED)
        handleSentiment(receivedSentiment);

        // LED-Index aus API direkt setzen (überschreibt mapSentimentToLED() aus handleSentiment)
        if (apiLedIndex != appState.currentLedIndex || !appState.initialAnalysisDone)
        {
            appState.currentLedIndex = apiLedIndex;
            appState.lastLedIndex = apiLedIndex;
            if (appState.autoMode && appState.lightOn)
            {
                updateLEDs();
            }
        }

        appState.initialAnalysisDone = true;
        appState.lastSuccessfulSentimentUpdate = currentMillis;

        // Reset error tracking
        appState.consecutiveSentimentFailures = 0;
        appState.sentimentAPIAvailable = true;

        // Reset status LED if there was a previous API error
        if (appState.statusLedMode == 2)
        {
            setStatusLED(0);
        }

        // v9.0: CSV stats removed - data managed in backend
    }
    else
    {
        debug(F("Sentiment Update fehlgeschlagen"));
        appState.consecutiveSentimentFailures++;

        // Error handling for consecutive failures
        if (appState.consecutiveSentimentFailures >= MAX_SENTIMENT_FAILURES && appState.sentimentAPIAvailable)
        {
            appState.sentimentAPIAvailable = false;
            debug(F("API nicht erreichbar nach mehreren Fehlversuchen. Wechsel in Neutral-Modus."));

            // Set neutral mood if no previous value exists
            if (!appState.initialAnalysisDone)
            {
                handleSentiment(0.0);
            }

            // Set status LED to API error
            setStatusLED(2);
        }

        // Update HA values with last known value anyway
        if (appState.mqttEnabled && mqtt.isConnected())
        {
            haSentimentScore.setValue(floatToString(appState.sentimentScore, 2).c_str());
            haSentimentCategory.setValue(appState.sentimentCategory.c_str());
        }
    }

    // Always clean up
    appState.isPulsing = false;
    isUpdating = false;
    updateLEDs();

    debug(String(F("Sentiment Update abgeschlossen. Nächstes Update in ")) + String(appState.moodUpdateInterval / 1000) + F(" Sekunden."));
}

// === Lese DHT Sensor und sende an HA ===
void readAndPublishDHT()
{
    // First check if DHT is enabled and initialized
    if (!appState.dhtEnabled || !dhtSensor)
    {
        return; // Skip DHT processing entirely
    }

    if (millis() - appState.lastDHTUpdate >= appState.dhtUpdateInterval)
    {
        debug(F("DHT Lesezyklus gestartet..."));

        float temp = NAN;
        float hum = NAN;
        bool tempValid = false;
        bool humValid = false;

        // Try/catch block for DHT reads to prevent crashes
        try
        {
            temp = dhtSensor->readTemperature();
            tempValid = !isnan(temp);

            // Small delay between reads for stability
            delay(10);

            hum = dhtSensor->readHumidity();
            humValid = !isnan(hum);
        }
        catch (...)
        {
            debug(F("Exception bei DHT-Lesung aufgetreten!"));
        }

        // Process temperature if valid
        if (tempValid)
        {
            bool tempChanged = abs(temp - appState.currentTemp) >= 0.1; // 0.1°C Schwelle
            if (tempChanged || isnan(appState.currentTemp))
            {
                appState.currentTemp = temp;
                if (appState.mqttEnabled && mqtt.isConnected())
                {
                    haTemperature.setValue(floatToString(temp, 1).c_str());
                }
                debug(String(F("DHT Temp: ")) + String(temp, 1) + F("C (geändert)"));
            }
            else
            {
                debug(String(F("DHT Temp: ")) + String(temp, 1) + F("C (unverändert)"));
            }
        }
        else
        {
            debug(F("DHT Temp Lesefehler!"));
        }

        // Process humidity if valid
        if (humValid)
        {
            bool humChanged = abs(hum - appState.currentHum) >= 0.5; // 0.5% Schwelle
            if (humChanged || isnan(appState.currentHum))
            {
                appState.currentHum = hum;
                if (appState.mqttEnabled && mqtt.isConnected())
                {
                    haHumidity.setValue(floatToString(hum, 1).c_str());
                }
                debug(String(F("DHT Hum: ")) + String(hum, 1) + F("% (geändert)"));
            }
            else
            {
                debug(String(F("DHT Hum: ")) + String(hum, 1) + F("% (unverändert)"));
            }
        }
        else
        {
            debug(F("DHT Hum Lesefehler!"));
        }

        appState.lastDHTUpdate = millis();
        debug(String(F("DHT Lesezyklus beendet. Nächstes Update in ")) + String(appState.dhtUpdateInterval / 1000) + F(" Sekunden."));
    }
}
