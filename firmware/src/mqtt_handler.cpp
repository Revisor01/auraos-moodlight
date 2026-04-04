#include "mqtt_handler.h"
#include "led_controller.h"
#include "sensor_manager.h"
#include "debug.h"
#include "MoodlightUtils.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// === ArduinoHA Globals ===
static WiFiClient wifiClientHA;
static byte mac[6];
static HADevice device;
HAMqtt mqtt(wifiClientHA, device);
HASensor haSentimentScore("sentiment_score", HASensor::PrecisionP2);
HASensor haTemperature("temperature", HASensor::PrecisionP1);
HASensor haHumidity("humidity", HASensor::PrecisionP0);
HALight haLight("moodlight", HALight::BrightnessFeature | HALight::RGBFeature);
HASelect haMode("mode");
HAButton haRefreshSentiment("refresh_sentiment");
HANumber haUpdateInterval("update_interval", HANumber::PrecisionP0);
HANumber haDhtInterval("dht_interval", HANumber::PrecisionP0);
HASensor haSentimentCategory("sentiment_category");
// Heartbeat-Sensoren
HASensor haUptime("uptime");
HASensor haWifiSignal("wifi_signal");
HASensor haSystemStatus("system_status");

// === Extern-Deklarationen fuer abhaengige Globals ===
extern AppState appState;
extern WatchdogManager watchdog;
extern void updateLEDs();
extern void setStatusLED(int mode);
extern void saveSettings();
extern void handleSentiment(float sentimentScore);

// ============================================================
// HA-Callbacks
// ============================================================

void onStateCommand(bool state, HALight *sender)
{
    // Ignoriere Callbacks waehrend wir Initial States senden
    if (appState.sendingInitialStates) {
        debug(F("Ignoriere State Command waehrend Initial States"));
        return;
    }

    if (state == appState.lightOn)
        return;
    appState.lightOn = state;
    debug(state ? F("HA Light: ON") : F("HA Light: OFF"));
    // LED-Update ueber Mutex-geschuetzten Pfad (NICHT pixels.show() direkt im Callback!)
    updateLEDs();
    sender->setState(state);

    // Verzoegerte Speicherung statt Flash-I/O im Callback-Kontext
    appState.settingsNeedSaving = true;
    appState.lastSettingsSaved = millis();
}

void onBrightnessCommand(uint8_t brightness, HALight *sender)
{
    // Ignoriere Callbacks waehrend wir Initial States senden
    if (appState.sendingInitialStates) {
        debug(F("Ignoriere Brightness Command waehrend Initial States"));
        return;
    }

    if (brightness == appState.manualBrightness)
        return;
    appState.manualBrightness = brightness;
    debug(F("HA Brightness Command"));
    if (!appState.autoMode && appState.lightOn)
    {
        updateLEDs();
    }
    sender->setBrightness(brightness);

    // Verzoegerte Speicherung statt Flash-I/O im Callback-Kontext
    appState.settingsNeedSaving = true;
    appState.lastSettingsSaved = millis();
}

void onRGBColorCommand(HALight::RGBColor color, HALight *sender)
{
    // Ignoriere Callbacks waehrend wir Initial States senden
    if (appState.sendingInitialStates) {
        debug(F("Ignoriere RGB Command waehrend Initial States"));
        return;
    }

    uint32_t newColor = ((uint32_t)color.red << 16) | ((uint32_t)color.green << 8) | color.blue;
    if (newColor == appState.manualColor)
        return;
    appState.manualColor = newColor;
    debug(F("HA RGB Command"));
    if (!appState.autoMode && appState.lightOn)
    {
        updateLEDs();
    }
    sender->setRGBColor(color);

    // Verzoegerte Speicherung statt Flash-I/O im Callback-Kontext
    appState.settingsNeedSaving = true;
    appState.lastSettingsSaved = millis();
}

void onModeCommand(int8_t index, HASelect *sender) {
    // Ignore mode commands during startup phase
    if (appState.initialStartupPhase) {
        debug(F("Ignoring mode command during startup grace period"));
        sender->setState(appState.autoMode ? 0 : 1);
        return;
    }

    bool newMode = (index == 0);
    if (newMode == appState.autoMode) return;
    appState.autoMode = newMode;
    debug(newMode ? F("HA Mode: Auto") : F("HA Mode: Manual"));
    if (appState.lightOn) {
        updateLEDs();
    }
    sender->setState(index);

    // Verzoegerte Speicherung statt Flash-I/O im Callback-Kontext
    appState.settingsNeedSaving = true;
    appState.lastSettingsSaved = millis();
}

void onUpdateIntervalCommand(HANumeric value, HANumber *sender)
{
    // Ignoriere Callbacks waehrend wir Initial States senden
    if (appState.sendingInitialStates) {
        debug(F("Ignoriere Update Interval Command waehrend Initial States"));
        return;
    }

    float intervalSeconds = value.toFloat();
    intervalSeconds = constrain(intervalSeconds, 10, 7200);
    unsigned long newInterval = (unsigned long)(intervalSeconds * 1000);
    if (newInterval == appState.moodUpdateInterval)
        return;
    appState.moodUpdateInterval = newInterval;
    sender->setState(float(intervalSeconds));
    debug(F("Mood Update Interval geaendert"));

    // Verzoegerte Speicherung statt Flash-I/O im Callback-Kontext
    appState.settingsNeedSaving = true;
    appState.lastSettingsSaved = millis();
}

// v9.0: onHeadlinesCommand() removed - headlines_per_source not used anymore

void onDHTIntervalCommand(HANumeric value, HANumber *sender)
{
    // Ignoriere Callbacks waehrend wir Initial States senden
    if (appState.sendingInitialStates) {
        debug(F("Ignoriere DHT Interval Command waehrend Initial States"));
        return;
    }

    float intervalSeconds = value.toFloat();
    intervalSeconds = constrain(intervalSeconds, 10, 7200);
    unsigned long newInterval = (unsigned long)(intervalSeconds * 1000);
    if (newInterval == appState.dhtUpdateInterval)
        return;
    appState.dhtUpdateInterval = newInterval;
    sender->setState(float(intervalSeconds));
    debug(F("DHT Interval geaendert"));

    // Verzoegerte Speicherung statt Flash-I/O im Callback-Kontext
    appState.settingsNeedSaving = true;
    appState.lastSettingsSaved = millis();
}

void onRefreshButtonPressed(HAButton *sender)
{
    // HTTP-Requests dürfen NICHT im Callback-Kontext ausgeführt werden
    // (mqtt.loop() → Callback → HTTP blockiert → WDT-Timeout / Stack-Korruption).
    // Stattdessen: Flag setzen, loop() führt den Abruf sicher aus.
    debug(F("Sentiment Refresh über Home Assistant ausgelöst — wird in loop() ausgeführt"));
    appState.mqttRefreshPending = true;
    appState.isPulsing = true;
    appState.pulseStartTime = millis();
}

// ============================================================
// Heartbeat
// ============================================================

// === MQTT Heartbeat ===
void sendHeartbeat()
{
    if (!appState.mqttEnabled || !mqtt.isConnected())
        return;

    // 1. Uptime
    unsigned long uptime = millis() / 1000;
    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int minutes = (uptime % 3600) / 60;
    int seconds = uptime % 60;
    char uptimeStr[50];
    sprintf(uptimeStr, "%dd %dh %dm %ds", days, hours, minutes, seconds);
    haUptime.setValue(uptimeStr);

    // 2. WiFi Signal
    int rssi = WiFi.RSSI();
    haWifiSignal.setValue(String(rssi).c_str());

    // 3. System Status
    String status = "OK";

    // Sentiment API Status
    if (!appState.sentimentAPIAvailable)
    {
        status = "Sentiment API nicht erreichbar";
    }

    // Temperatur/Luftfeuchtigkeit Status
    if (isnan(appState.currentTemp) || isnan(appState.currentHum))
    {
        status = "DHT Sensor Probleme";
    }

    // Speicher-Status
    if (ESP.getFreeHeap() < 10000)
    { // Weniger als 10KB
        status = "Speicher niedrig";
    }

    haSystemStatus.setValue(status.c_str());

    debug(F("MQTT Heartbeat gesendet"));
    appState.lastMqttHeartbeat = millis();
}

// ============================================================
// HA Setup
// ============================================================

// === Home Assistant Setup Routine ===
void setupHA()
{
    if (!appState.mqttEnabled || appState.mqttServer.isEmpty())
    {
        debug(F("MQTT nicht konfiguriert, überspringe HA Setup"));
        return;
    }

    WiFi.macAddress(mac); // Hole MAC-Adresse für Unique ID

    // --- Device Konfiguration ---
    // Setze Unique ID basierend auf MAC-Adresse
    device.setUniqueId(mac, sizeof(mac));
    // Setze Namen und Infos für das Gerät in Home Assistant
    device.setName("Moodlight"); // Name des Geräts in HA
    device.setSoftwareVersion(MOODLIGHT_VERSION);
    device.setManufacturer("Bridging Faith Tech"); // Hersteller
    device.setModel("Moodlight ESP32 Proto v1");   // Modell

    // --- Entitäten Konfiguration ---

    // Numerischer Sentiment Score (-1.00 bis +1.00)
    haSentimentScore.setName("Weltlage Score");     // Angezeigter Name in HA
    haSentimentScore.setIcon("mdi:gauge-low");      // Passendes Icon
    haSentimentScore.setUnitOfMeasurement("Score"); // Einheit für den Wert
    // Präzision wurde bereits im Konstruktor gesetzt: HASensor::PrecisionP2

    // Sentiment Kategorie (Text-Sensor)
    haSentimentCategory.setName("Weltlage Kategorie");
    haSentimentCategory.setIcon("mdi:tag-text-outline");
    // Keine Einheit für Text-Sensoren

    if (appState.dhtEnabled)
    {
        // Temperatur Sensor
        haTemperature.setName("Temperatur");
        haTemperature.setDeviceClass("temperature"); // Setzt Icon und Verhalten in HA
        haTemperature.setUnitOfMeasurement("°C");    // Einheit

        // Luftfeuchtigkeit Sensor
        haHumidity.setName("Luftfeuchtigkeit");
        haHumidity.setDeviceClass("humidity"); // Setzt Icon und Verhalten in HA
        haHumidity.setUnitOfMeasurement("%");  // Einheit
    }
    // Licht Steuerung
    haLight.setName("Moodlight"); // Name der Entität
    haLight.setIcon("mdi:lightbulb-variant-outline");
    haLight.onStateCommand(onStateCommand);           // Callback für An/Aus
    haLight.onBrightnessCommand(onBrightnessCommand); // Callback für Helligkeit
    haLight.onRGBColorCommand(onRGBColorCommand);     // Callback für Farbe
    haLight.setRetain(true);                          // Zustand behalten

    // Modus Auswahl (Auto/Manual)
    haMode.setName("Betriebsmodus");
    haMode.setOptions("Auto;Manual"); // Optionen für die Dropdown-Liste
    haMode.setIcon("mdi:cogs");
    haMode.onCommand(onModeCommand); // Callback registrieren
    haMode.setRetain(true);          // Zustand behalten

    // Update Intervall (Stimmung)
    haUpdateInterval.setName("Stimmung Update Intervall");
    haUpdateInterval.setMin(10);                // Minimalwert in Sekunden
    haUpdateInterval.setMax(7200);              // Maximalwert (2 Stunden)
    haUpdateInterval.setStep(10);               // Schrittweite
    haUpdateInterval.setUnitOfMeasurement("s"); // Einheit
    haUpdateInterval.setIcon("mdi:timer-sync-outline");
    haUpdateInterval.onCommand(onUpdateIntervalCommand); // Callback registrieren
    haUpdateInterval.setRetain(true);                    // Zustand behalten

    // Update Intervall (DHT Sensor)
    haDhtInterval.setName("Sensor Update Intervall");
    haDhtInterval.setMin(10);
    haDhtInterval.setMax(7200);
    haDhtInterval.setStep(10);
    haDhtInterval.setUnitOfMeasurement("s");
    haDhtInterval.setIcon("mdi:sun-clock");
    haDhtInterval.onCommand(onDHTIntervalCommand); // Callback registrieren
    haDhtInterval.setRetain(true);                 // Zustand behalten

    // Refresh Button
    haRefreshSentiment.setName("Weltlage aktualisieren");
    haRefreshSentiment.setIcon("mdi:refresh");
    haRefreshSentiment.onCommand(onRefreshButtonPressed);

    // v9.0: haHeadlinesPerSource setup removed - parameter not used anymore

    // Heartbeat Sensoren
    haUptime.setName("Moodlight Uptime");
    haUptime.setIcon("mdi:clock-outline");

    haWifiSignal.setName("Moodlight WiFi Signal");
    haWifiSignal.setIcon("mdi:wifi");
    haWifiSignal.setUnitOfMeasurement("dBm");

    haSystemStatus.setName("Moodlight Status");
    haSystemStatus.setIcon("mdi:information-outline");

    debug(F("HA Komponenten konfiguriert."));
}

// ============================================================
// Initiale Zustände
// ============================================================

// === Sende initiale Zustände an HA ===
void sendInitialStates()
{
    if (!appState.mqttEnabled || !mqtt.isConnected())
    {
        debug(F("MQTT not connected, skipping initial states."));
        return;
    }

    debug(F("Sende initiale Zustände an HA..."));

    // Setze Flag um Callback-Loops zu vermeiden
    appState.sendingInitialStates = true;

    // Licht & Modus (aus globalen Variablen)
    haLight.setState(appState.lightOn);
    haLight.setBrightness(appState.manualBrightness);
    // Farbe direkt aus customColors lesen — ohne pixels.Color() (vermeidet Crash bei uninitialisiertem NeoPixel)
    int safeIndex = constrain(appState.currentLedIndex, 0, 4);
    uint32_t initialColor = appState.autoMode ? appState.customColors[safeIndex] : appState.manualColor;
    HALight::RGBColor color;
    color.red = (initialColor >> 16) & 0xFF;
    color.green = (initialColor >> 8) & 0xFF;
    color.blue = initialColor & 0xFF;
    haLight.setRGBColor(color);
    haMode.setState(appState.autoMode ? 0 : 1);

    // Intervalle (aus globalen Variablen)
    haUpdateInterval.setState(float(appState.moodUpdateInterval / 1000.0));
    haDhtInterval.setState(float(appState.dhtUpdateInterval / 1000.0));

    // DHT: Letzte bekannte Werte senden statt direkt zu lesen
    // (DHT-I/O im Callback-/Reconnect-Kontext vermeiden — naechster regulaerer DHT-Zyklus liefert aktuelle Werte)
    if (!isnan(appState.currentTemp))
        haTemperature.setValue(floatToString(appState.currentTemp, 1).c_str());
    if (!isnan(appState.currentHum))
        haHumidity.setValue(floatToString(appState.currentHum, 1).c_str());

    // Sentiment (letzter bekannter Wert)
    haSentimentScore.setValue(floatToString(appState.sentimentScore, 2).c_str());
    haSentimentCategory.setValue(appState.sentimentCategory.c_str());

    // Initiale Heartbeat-Werte senden
    sendHeartbeat();

    debug(F("Initiale Zustände gesendet."));

    // Flag zurücksetzen - Callbacks sind jetzt wieder aktiv
    appState.sendingInitialStates = false;
}

// ============================================================
// MQTT Reconnect
// ============================================================

// === Verbesserte MQTT Reconnect und Heartbeat ===
void checkAndReconnectMQTT() {
    if (!appState.mqttEnabled || appState.mqttServer.isEmpty())
        return;

    unsigned long currentMillis = millis();

    // Heartbeat senden, wenn verbunden
    if (WiFi.status() == WL_CONNECTED && mqtt.isConnected()) {
        if (currentMillis - appState.lastMqttHeartbeat >= MQTT_HEARTBEAT_INTERVAL) {
            sendHeartbeat();
        }
    }

    // Reduce reconnection attempts frequency
    static unsigned long mqttReconnectBackoff = 10000; // Start with 10 seconds

    // Reconnect-Logik
    if (WiFi.status() == WL_CONNECTED) {
        if (!mqtt.isConnected()) {
            if (currentMillis - appState.lastMqttReconnectAttempt > mqttReconnectBackoff) {
                debug(F("MQTT nicht verbunden. Versuche Reconnect..."));

                // Update status LED indicator without direct LED update
                if (appState.statusLedMode != 4) {
                    appState.statusLedMode = 4; // MQTT mode
                    appState.statusLedBlinkStart = currentMillis;
                    appState.statusLedState = true;

                    if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
                        appState.ledColors[appState.statusLedIndex] = pixels.Color(0, 255, 255); // Cyan
                        appState.ledUpdatePending = true;
                        xSemaphoreGive(appState.ledMutex);
                    }
                }

                // Ensure we're not in an interrupt context
                yield();
                delay(10);

                mqtt.disconnect();
                delay(100);

                // MQTT neu starten
                mqtt.begin(appState.mqttServer.c_str(), appState.mqttUser.c_str(), appState.mqttPassword.c_str());

                // Kurz auf Verbindung warten (mit mqtt.loop()!)
                unsigned long mqttStart = millis();
                while (!mqtt.isConnected() && millis() - mqttStart < 3000) {
                    mqtt.loop(); // WICHTIG: mqtt.loop() hier aufrufen!
                    delay(100);
                    watchdog.feed(); // Watchdog füttern während MQTT-Reconnect-Wartezeit
                }

                appState.lastMqttReconnectAttempt = currentMillis;

                if (mqtt.isConnected()) {
                    debug(F("MQTT erfolgreich verbunden."));
                    appState.mqttWasConnected = true;
                    mqttReconnectBackoff = 10000; // Reset backoff on success

                    // Initiale Zustände für nächsten Loop-Zyklus einplanen
                    appState.mqttInitialStatesPending = true;

                    // Update status LED without direct update
                    appState.statusLedMode = 0; // Normal mode

                    if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
                        appState.ledUpdatePending = true; // Request full LED update
                        xSemaphoreGive(appState.ledMutex);
                    }
                } else {
                    debug(F("MQTT Reconnect fehlgeschlagen. Erhöhe Backoff."));
                    // Increase backoff time exponentially up to 5 minutes
                    mqttReconnectBackoff = min(mqttReconnectBackoff * 2, 300000UL);
                }
            }
        } else if (!appState.mqttWasConnected) {
            debug(F("MQTT wieder verbunden. Sende Zustände..."));

            // Initiale Zustände für nächsten Loop-Zyklus einplanen
            appState.mqttInitialStatesPending = true;

            appState.mqttWasConnected = true;

            // Update status LED without direct update
            appState.statusLedMode = 0; // Normal mode

            if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
                appState.ledUpdatePending = true; // Request full LED update
                xSemaphoreGive(appState.ledMutex);
            }
        }

        // Verzögerte initiale Zustände senden (AppState-Flag statt static local)
        if (appState.mqttInitialStatesPending && mqtt.isConnected()) {
            appState.mqttInitialStatesPending = false;
            sendInitialStates();
        }
    }
}

// === MQTT beim Start einmalig verbinden ===
void connectMQTTOnStartup() {
    if (!appState.mqttEnabled || appState.mqttServer.isEmpty()) return;

    debug(F("MQTT Konfiguration gefunden, starte verzögerte Initialisierung..."));
    delay(500);

    setupHA();

    debug(F("Versuche MQTT zu initialisieren..."));
    unsigned long mqttStartTime = millis();
    bool mqttInitSuccess = false;

    try {
        mqtt.begin(appState.mqttServer.c_str(), appState.mqttUser.c_str(), appState.mqttPassword.c_str());

        debug(F("Warte auf MQTT Verbindung (max 5s)..."));
        while (!mqtt.isConnected() && (millis() - mqttStartTime < 5000)) {
            mqtt.loop();
            delay(100);
            watchdog.feed(); // Watchdog füttern während MQTT-Startup-Wartezeit
        }
        mqttInitSuccess = mqtt.isConnected();
    } catch (...) {
        debug(F("Exception bei MQTT-Initialisierung"));
        mqttInitSuccess = false;
    }

    if (mqttInitSuccess) {
        debug(F("MQTT erfolgreich initialisiert und verbunden."));
        sendInitialStates();
    } else {
        debug(F("MQTT-Initialisierung/Verbindung fehlgeschlagen - Fahre ohne MQTT fort"));
    }
}
