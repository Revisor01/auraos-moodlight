#include "sensor_manager.h"
#include "led_controller.h"
#include "relative_level_logic.h"
#include "debug.h"
#include "MoodlightUtils.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <DHT.h>
#include <ArduinoHA.h>
#include <time.h>

// Globals aus moodlight.cpp
extern AppState appState;
extern HAMqtt mqtt;
extern HASensor haSentimentScore;
extern HASensor haSentimentCategory;
extern HASensor haSentimentPercentile;
extern HASensor haTemperature;
extern HASensor haHumidity;
extern WatchdogManager watchdog;

// Hardware-Instanzen — definiert in diesem Modul
DHT* dhtSensor = nullptr;
WiFiClient wifiClientHTTP;

// DHT mit dem tatsächlichen Pin aus den Settings initialisieren
void initDHT() {
    if (dhtSensor) {
        delete dhtSensor;
        dhtSensor = nullptr;
    }
    if (!appState.dhtEnabled) {
        debug(F("DHT deaktiviert — ueberspringe Hardware-Initialisierung"));
        return;
    }
    // Internen Pull-Up aktivieren (ersetzt externen 4.7kΩ Widerstand)
    pinMode(appState.dhtPin, INPUT_PULLUP);
    dhtSensor = new DHT(appState.dhtPin, DHT22);
    dhtSensor->begin();
    debug(String(F("DHT22 initialisiert auf Pin ")) + String(appState.dhtPin) + F(" (mit internem Pull-Up)"));
}

// === Map Sentiment Score (-1 bis +1) zu LED Index (0-4) ===
int mapSentimentToLED(float sentimentScore)
{
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

// sentimentCategory erst hier nach dem Altwert-Vergleich setzen, sonst ist er immer false.
void handleSentiment(float sentimentScore, const String &apiCategory)
{
    sentimentScore = constrain(sentimentScore, -1.0, 1.0);

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

    String categoryText = apiCategory;
    if (categoryText.isEmpty())
    {
        int localIndex = mapSentimentToLED(sentimentScore);
        switch (localIndex)
        {
        case 4: categoryText = "sehr positiv"; break;
        case 3: categoryText = "positiv"; break;
        case 2: categoryText = "neutral"; break;
        case 1: categoryText = "negativ"; break;
        case 0: categoryText = "sehr negativ"; break;
        default: categoryText = "unbekannt"; break;
        }
    }

    // Sende Kategorie an HA nur bei Änderung (Vergleich gegen den ALTEN gespeicherten Wert)
    if (categoryText != appState.sentimentCategory || !appState.initialAnalysisDone)
    {
        if (appState.mqttEnabled && mqtt.isConnected())
        {
            haSentimentCategory.setValue(categoryText.c_str());
        }

        appState.sentimentCategory = categoryText;
        debug("Neue Sentiment Kategorie: " + categoryText);
    }
}

// === Lade Backend-Statistiken ===
bool fetchBackendStatistics(JsonDocument &doc, int hours)
{
    if (WiFi.status() != WL_CONNECTED) {
        debug(F("WiFi nicht verbunden - kann keine Backend-Statistiken laden"));
        return false;
    }

    // Stats-URL aus apiUrl ("/current" -> "/history"), sonst DEFAULT_STATS_API_URL.
    String baseApiUrl = appState.apiUrl;
    String statsBaseUrl;
    if (baseApiUrl.endsWith("/current")) {
        statsBaseUrl = baseApiUrl.substring(0, baseApiUrl.length() - String("/current").length()) + "/history";
    } else {
        statsBaseUrl = DEFAULT_STATS_API_URL;
    }
    String statsUrl = statsBaseUrl + "?hours=" + String(hours);
    debug(String(F("Lade Statistiken von Backend: ")) + statsUrl);

    HTTPClient http;
    http.setReuse(false);
    http.setUserAgent("MoodlightClient/1.0");

    if (wifiClientHTTP.connected()) {
        wifiClientHTTP.stop();
        delay(10);
    }

    if (!http.begin(wifiClientHTTP, statsUrl)) {
        debug(F("HTTP Begin fehlgeschlagen für Backend-Statistiken"));
        return false;
    }

    http.setTimeout(8000);

    int httpCode = http.GET();
    debug(String(F("Backend-Statistiken HTTP Code: ")) + String(httpCode));

    // WDT nach blockierendem HTTP-Call sofort füttern (kann bis zu 15s dauern)
    watchdog.feed();

    if (httpCode == HTTP_CODE_OK) {
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
}

// === Sicherer HTTP GET mit JSON-Parsing ===
bool safeHttpGet(const String &url, JsonDocument &doc)
{
    bool success = false;

    debug(String(F("Making safe HTTP request to: ")) + url);

    HTTPClient http;
    http.setReuse(false);
    http.setUserAgent("MoodlightClient/1.0");

    if (wifiClientHTTP.connected()) {
        wifiClientHTTP.stop();
        delay(10);
    }

    if (http.begin(wifiClientHTTP, url)) {
        http.setTimeout(10000);

        int httpCode = http.GET();
        debug(String(F("HTTP response code: ")) + httpCode);

        // WDT nach blockierendem HTTP-Call sofort füttern (kann bis zu 10s dauern)
        watchdog.feed();

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

        http.end();
    }
    else {
        debug(F("Failed to begin HTTP connection"));
    }

    if (wifiClientHTTP.connected()) {
        wifiClientHTTP.stop();
    }

    return success;
}

// Implementierungsdetails des servergeführten Poll-Delays (nicht in config.h, da modul-lokal)
static const unsigned long POLL_BUFFER_MS = 90000UL;       // Puffer nach Server-Analyse-Zeitpunkt
static const unsigned long POLL_MIN_DELAY_MS = 60000UL;    // Untergrenze gegen Poll-Schleifen

// Ersatz für timegm(), das newlib auf dem ESP32 nicht hat.
static time_t utcMkTime(struct tm *tmUtc)
{
    // mktime() rechnet in lokaler Zeit, daher TZ kurz auf UTC.
    char *oldTz = getenv("TZ");
    String savedTz = oldTz ? String(oldTz) : String();
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t result = mktime(tmUtc);
    if (!savedTz.isEmpty()) {
        setenv("TZ", savedTz.c_str(), 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    return result;
}

// === Rufe Sentiment vom Backend ab (-1 bis +1) ===
void getSentiment()
{
    static bool isUpdating = false;
    unsigned long currentMillis = millis();

    // Wirksames Poll-Delay: servergeführter Wert falls vorhanden, sonst konfiguriertes Intervall
    unsigned long effectiveDelay = appState.nextMoodPollDelay > 0 ? appState.nextMoodPollDelay : appState.moodUpdateInterval;

    static unsigned long lastIntervalDebug = 0;
    if (currentMillis - lastIntervalDebug >= 300000)
    {
        debug(String(F("Sentiment Interval Status: ")) + String(currentMillis - appState.lastMoodUpdate) + F("/") + String(effectiveDelay) + F("ms"));
        lastIntervalDebug = currentMillis;
    }

    if (appState.sentimentAPIAvailable && appState.lastSuccessfulSentimentUpdate > 0 && currentMillis - appState.lastSuccessfulSentimentUpdate > SENTIMENT_FALLBACK_TIMEOUT)
    {
        debug(F("API-Timeout: Kein erfolgreicher Sentiment-Abruf seit über einer Stunde."));
        debug(F("Wechsel in Neutral-Modus."));
        appState.sentimentAPIAvailable = false;
        handleSentiment(0.0);
        // LED-Index auf Neutral setzen, damit der "Neutral-Modus" auch die LEDs neutral faerbt
        appState.currentLedIndex = 2;
        // Neutral ist hier ein Notbehelf, keine gelieferte Stufe — HA bekommt "unbekannt"
        appState.relativeLevel = RELATIVE_LEVEL_UNKNOWN;
        if (appState.autoMode && appState.lightOn)
        {
            updateLEDs();
        }
        // Im Manual-Modus hängt die Lampe nicht an der API, daher kein Fehler-Pixel.
        if (appState.autoMode)
        {
            setStatusLED(2);
        }
    }

    if (isUpdating)
        return;

    if (!(currentMillis - appState.lastMoodUpdate >= effectiveDelay || !appState.initialAnalysisDone))
        return;

    debug(F("Starte Sentiment-Abruf..."));
    isUpdating = true;


    // Use static document to avoid heap fragmentation
    static JsonDocument doc;
    doc.clear();

    bool success = safeHttpGet(appState.apiUrl, doc);

    appState.lastMoodUpdate = currentMillis;

    if (success && doc["sentiment"].is<float>())
    {
        float receivedSentiment = doc["sentiment"].as<float>();
        debug(String(F("Sentiment empfangen: ")) + String(receivedSentiment, 2));

        // Ohne led_index (altes Backend) über mapSentimentToLED().
        int apiLedIndex = -1;
        bool hasApiLedIndex = doc["led_index"].is<int>();
        if (hasApiLedIndex)
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

        // Zuweisung erst in handleSentiment(), damit der Änderungsvergleich gegen den Altwert läuft.
        String apiCategory = doc["category"].is<const char*>() ? doc["category"].as<String>() : String();

        handleSentiment(receivedSentiment, apiCategory);

        if (doc["percentile"].is<float>())
        {
            appState.percentile = doc["percentile"].as<float>();
            if (appState.mqttEnabled && mqtt.isConnected())
            {
                haSentimentPercentile.setValue(floatToString(appState.percentile * 100.0, 0).c_str());
            }
        }
        if (doc["headlines_analyzed"].is<int>()) appState.headlinesAnalyzed = doc["headlines_analyzed"].as<int>();
        if (doc["thresholds"].is<JsonObject>()) {
            if (doc["thresholds"]["p20"].is<float>()) appState.thresholdP20 = doc["thresholds"]["p20"].as<float>();
            if (doc["thresholds"]["p40"].is<float>()) appState.thresholdP40 = doc["thresholds"]["p40"].as<float>();
            if (doc["thresholds"]["p60"].is<float>()) appState.thresholdP60 = doc["thresholds"]["p60"].as<float>();
            if (doc["thresholds"]["p80"].is<float>()) appState.thresholdP80 = doc["thresholds"]["p80"].as<float>();
            appState.thresholdFallback = doc["thresholds"]["fallback"] | false;
        }
        if (doc["historical"].is<JsonObject>()) {
            if (doc["historical"]["min"].is<float>()) appState.histMin = doc["historical"]["min"].as<float>();
            if (doc["historical"]["max"].is<float>()) appState.histMax = doc["historical"]["max"].as<float>();
            if (doc["historical"]["median"].is<float>()) appState.histMedian = doc["historical"]["median"].as<float>();
            if (doc["historical"]["count"].is<int>()) appState.histCount = doc["historical"]["count"].as<int>();
        }

        // LED-Index aus API setzen — einzige Stelle die LEDs steuert
        debug(String(F("LED-Index setzen: ")) + String(apiLedIndex));
        appState.currentLedIndex = apiLedIndex;
        appState.lastLedIndex = apiLedIndex;
        // Nur ein echter Wochenvergleich zählt; gesendet wird in loop().
        appState.relativeLevel = relativeLevelFromResponse(hasApiLedIndex, apiLedIndex,
                                                           doc["thresholds"]["fallback"] | false);
        // LEDs nur im Auto-Modus — im Manual-Modus aktualisiert der Abruf nur die Sensoren
        if (appState.autoMode && appState.lightOn)
        {
            updateLEDs();
        }

        appState.initialAnalysisDone = true;
        appState.lastSuccessfulSentimentUpdate = currentMillis;

        // next_update_minutes ist das feste Worker-Intervall, die Restzeit ergibt sich aus dem Analyse-Alter.
        appState.nextMoodPollDelay = 0; // Default: Fallback auf moodUpdateInterval
        if (doc["next_update_minutes"].is<int>())
        {
            int intervalMinutes = doc["next_update_minutes"].as<int>();
            if (intervalMinutes > 0 && intervalMinutes <= 1440)
            {
                long ageSeconds = 0;

                // Analyse-Alter nur berechnen, wenn eine gültige lokale Uhrzeit vorliegt (NTP)
                if (appState.timeInitialized && doc["timestamp"].is<const char*>())
                {
                    const char* timestampStr = doc["timestamp"].as<const char*>();
                    struct tm analysisTm = {};
                    // ISO-8601 Basisformat parsen, z.B. "2026-07-31T13:02:49"
                    if (strptime(timestampStr, "%Y-%m-%dT%H:%M:%S", &analysisTm) != nullptr)
                    {
                        time_t analysisTime = utcMkTime(&analysisTm);
                        time_t now = time(nullptr);
                        ageSeconds = (long)(now - analysisTime);
                    }
                    else
                    {
                        debug(F("Konnte Analyse-Zeitstempel nicht parsen — Alter wird als 0 angenommen"));
                    }
                }

                // Alter auf plausiblen Bereich begrenzen (Uhr-Drift / veraltete Werte abfangen)
                long intervalSeconds = (long)intervalMinutes * 60L;
                if (ageSeconds < 0) ageSeconds = 0;
                if (ageSeconds > intervalSeconds) ageSeconds = intervalSeconds;

                long remainingMs = (intervalSeconds * 1000L) - (ageSeconds * 1000L) + (long)POLL_BUFFER_MS;
                if (remainingMs < (long)POLL_MIN_DELAY_MS) remainingMs = (long)POLL_MIN_DELAY_MS;
                if ((unsigned long)remainingMs > appState.moodUpdateInterval) remainingMs = (long)appState.moodUpdateInterval;

                appState.nextMoodPollDelay = (unsigned long)remainingMs;
                debug(String(F("Servergeführtes Poll-Delay berechnet: ")) + String(appState.nextMoodPollDelay / 1000) +
                      F(" Sekunden (Analyse-Alter: ") + String(ageSeconds) + F("s, Intervall: ") + String(intervalMinutes) + F("min)"));
            }
            else
            {
                debug(F("next_update_minutes ausserhalb des plausiblen Bereichs — Fallback auf moodUpdateInterval"));
            }
        }
        else
        {
            debug(F("next_update_minutes fehlt in API-Response — Fallback auf moodUpdateInterval"));
        }

        appState.consecutiveSentimentFailures = 0;
        appState.sentimentAPIAvailable = true;

        if (appState.statusLedMode == 2)
        {
            setStatusLED(0);
        }

    }
    else
    {
        debug(F("Sentiment Update fehlgeschlagen"));
        appState.consecutiveSentimentFailures++;

        // Bei API-Ausfall garantiert auf konfiguriertes moodUpdateInterval zurückfallen
        appState.nextMoodPollDelay = 0;

        if (appState.consecutiveSentimentFailures >= MAX_SENTIMENT_FAILURES && appState.sentimentAPIAvailable)
        {
            appState.sentimentAPIAvailable = false;
            debug(F("API nicht erreichbar nach mehreren Fehlversuchen. Wechsel in Neutral-Modus."));

            if (!appState.initialAnalysisDone)
            {
                handleSentiment(0.0);
                // handleSentiment() aktualisiert nur MQTT/HA, nicht die LEDs.
                if (appState.autoMode && appState.lightOn)
                {
                    updateLEDs();
                }
            }
        }

        // Nur im Auto-Modus und ohne WLAN-, MQTT- oder Update-Blinken zu überschreiben.
        if (!appState.sentimentAPIAvailable && appState.autoMode && appState.statusLedMode == 0)
        {
            setStatusLED(2);
        }

        if (appState.mqttEnabled && mqtt.isConnected())
        {
            haSentimentScore.setValue(floatToString(appState.sentimentScore, 2).c_str());
            haSentimentCategory.setValue(appState.sentimentCategory.c_str());
            haSentimentPercentile.setValue(floatToString(appState.percentile * 100.0, 0).c_str());
        }
    }

    isUpdating = false;

    unsigned long finalEffectiveDelay = appState.nextMoodPollDelay > 0 ? appState.nextMoodPollDelay : appState.moodUpdateInterval;
    debug(String(F("Sentiment Update abgeschlossen. Nächstes Update in ")) + String(finalEffectiveDelay / 1000) + F(" Sekunden."));
}

// === Lese DHT Sensor und sende an HA ===
void readAndPublishDHT()
{
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

        temp = dhtSensor->readTemperature();
        tempValid = !isnan(temp);

        // Small delay between reads for stability
        delay(10);

        hum = dhtSensor->readHumidity();
        humValid = !isnan(hum);

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
