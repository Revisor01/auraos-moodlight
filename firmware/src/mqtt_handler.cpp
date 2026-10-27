#include "mqtt_handler.h"
#include "led_controller.h"
#include "sensor_manager.h"
#include "relative_level_logic.h"
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
HASensor haSentimentPercentile("sentiment_percentile", HASensor::PrecisionP0);
// Heartbeat-Sensoren
HASensor haUptime("uptime");
HASensor haWifiSignal("wifi_signal");
HASensor haSystemStatus("system_status");
// Eigene unique_ids, damit bestehende Entitäten unverändert bleiben.
HASensor haSentimentRelative("sentiment_relative");
HASensor haSentimentLevel("sentiment_level");

// === Extern-Deklarationen fuer abhaengige Globals ===
extern AppState appState;
extern WatchdogManager watchdog;
extern void updateLEDs();
extern void setStatusLED(int mode);
extern void saveSettings();
extern void handleSentiment(float sentimentScore, const String &apiCategory);

// HA-Callbacks

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
    // Helligkeit schaltet den Modus nicht um; im Auto-Modus wird sie für Manual gemerkt.
    appState.manualBrightness = brightness;
    debug(F("HA Brightness Command"));
    if (!appState.autoMode && appState.lightOn)
    {
        updateLEDs();
    }
    // Rueckmeldung an HA ueber loop() (syncHaLightState) statt Echo des Befehls
    appState.haLightStateDirty = true;

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
    ColorCommandAction action = decideColorCommand(appState.autoMode, appState.initialStartupPhase,
                                                   newColor, appState.manualColor);
    if (action == ColorCommandAction::Ignore)
        return;
    appState.manualColor = newColor;
    if (action == ColorCommandAction::SwitchToManualAndApply)
    {
        // Wer eine Farbe waehlt, will sie sehen — Auto verlassen
        appState.autoMode = false;
        haMode.setState(1);
        debug(F("HA RGB Command im Auto-Modus — wechsle auf Manual"));
    }
    else
    {
        debug(F("HA RGB Command"));
    }
    if (!appState.autoMode && appState.lightOn)
    {
        updateLEDs();
    }
    // Rückmeldung über loop() statt Echo, damit HA bei StoreOnly die angezeigte Auto-Farbe behält.
    appState.haLightStateDirty = true;

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
    // Auch bei ausgeschaltetem Licht die Farbe des neuen Modus an HA melden
    appState.haLightStateDirty = true;
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
    // Kein HTTP im Callback-Kontext (WDT-Timeout); loop() führt den Abruf aus.
    debug(F("Sentiment Refresh über Home Assistant ausgelöst — wird in loop() ausgeführt"));
    appState.mqttRefreshPending = true;
}

// Heartbeat

void sendHeartbeat()
{
    if (!appState.mqttEnabled || !mqtt.isConnected())
        return;

    unsigned long uptime = millis() / 1000;
    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int minutes = (uptime % 3600) / 60;
    int seconds = uptime % 60;
    char uptimeStr[50];
    snprintf(uptimeStr, sizeof(uptimeStr), "%dd %dh %dm %ds", days, hours, minutes, seconds);
    haUptime.setValue(uptimeStr);

    int rssi = WiFi.RSSI();
    haWifiSignal.setValue(String(rssi).c_str());

    String status = "OK";

    if (!appState.sentimentAPIAvailable)
    {
        status = "Sentiment API nicht erreichbar";
    }

    // Temperatur/Luftfeuchtigkeit Status — nur pruefen wenn DHT aktiviert ist
    if (appState.dhtEnabled && (isnan(appState.currentTemp) || isnan(appState.currentHum)))
    {
        status = "DHT Sensor Probleme";
    }

    if (ESP.getFreeHeap() < 10000)
    { // Weniger als 10KB
        status = "Speicher niedrig";
    }

    haSystemStatus.setValue(status.c_str());

    debug(F("MQTT Heartbeat gesendet"));
    appState.lastMqttHeartbeat = millis();
}

// HA Setup

void setupHA()
{
    if (!appState.mqttEnabled || appState.mqttServer.isEmpty())
    {
        debug(F("MQTT nicht konfiguriert, überspringe HA Setup"));
        return;
    }

    WiFi.macAddress(mac);

    // Unique ID aus der MAC-Adresse
    device.setUniqueId(mac, sizeof(mac));
    device.setName("Moodlight");
    device.setSoftwareVersion(MOODLIGHT_VERSION);
    device.setManufacturer("Bridging Faith Tech");
    device.setModel("Moodlight ESP32 Proto v1");


    // Numerischer Sentiment Score (-1.00 bis +1.00)
    haSentimentScore.setName("Weltlage Score");
    haSentimentScore.setIcon("mdi:gauge-low");
    haSentimentScore.setUnitOfMeasurement("Score");
    // Präzision wurde bereits im Konstruktor gesetzt: HASensor::PrecisionP2

    haSentimentCategory.setName("Weltlage Kategorie");
    haSentimentCategory.setIcon("mdi:tag-text-outline");

    // Sentiment Perzentil (Position im 7-Tage-Fenster, 0-100%)
    haSentimentPercentile.setName("Weltlage Perzentil");
    haSentimentPercentile.setIcon("mdi:chart-bell-curve-cumulative");
    haSentimentPercentile.setUnitOfMeasurement("%");

    // Stufe der Auto-Farbe relativ zur Woche; "Weltlage Kategorie" ist absolut und kann abweichen.
    haSentimentRelative.setName("Weltlage relativ");
    haSentimentRelative.setIcon("mdi:scale-balance");

    // Dieselbe Stufe als Zahl 0-4 für Automationen; "None", solange keine echte Stufe vorliegt.
    haSentimentLevel.setName("Weltlage Stufe");
    haSentimentLevel.setIcon("mdi:stairs");
    haSentimentLevel.setStateClass("measurement");

    if (appState.dhtEnabled)
    {
        haTemperature.setName("Temperatur");
        haTemperature.setDeviceClass("temperature");
        haTemperature.setUnitOfMeasurement("°C");

        haHumidity.setName("Luftfeuchtigkeit");
        haHumidity.setDeviceClass("humidity");
        haHumidity.setUnitOfMeasurement("%");
    }
    haLight.setName("Moodlight");
    haLight.setIcon("mdi:lightbulb-variant-outline");
    haLight.onStateCommand(onStateCommand);
    haLight.onBrightnessCommand(onBrightnessCommand);
    haLight.onRGBColorCommand(onRGBColorCommand);
    // Kein setRetain(true): retained Befehle kämen bei jedem Reconnect veraltet zurück.

    haMode.setName("Betriebsmodus");
    haMode.setOptions("Auto;Manual");
    haMode.setIcon("mdi:cogs");
    haMode.onCommand(onModeCommand);

    haUpdateInterval.setName("Stimmung Update Intervall");
    haUpdateInterval.setMin(10);
    haUpdateInterval.setMax(7200);
    haUpdateInterval.setStep(10);
    haUpdateInterval.setUnitOfMeasurement("s");
    haUpdateInterval.setIcon("mdi:timer-sync-outline");
    haUpdateInterval.onCommand(onUpdateIntervalCommand);

    haDhtInterval.setName("Sensor Update Intervall");
    haDhtInterval.setMin(10);
    haDhtInterval.setMax(7200);
    haDhtInterval.setStep(10);
    haDhtInterval.setUnitOfMeasurement("s");
    haDhtInterval.setIcon("mdi:sun-clock");
    haDhtInterval.onCommand(onDHTIntervalCommand);

    // Refresh Button
    haRefreshSentiment.setName("Weltlage aktualisieren");
    haRefreshSentiment.setIcon("mdi:refresh");
    haRefreshSentiment.onCommand(onRefreshButtonPressed);


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

// Angezeigte Farbe und Helligkeit an HA melden

// HALight sendet nur bei Änderung und liefert false, wenn das Senden scheitert.
static bool publishShownLight()
{
    ShownLight shown = currentShownLight();
    // Der Standardkonstruktor setzt isSet=false, solche Farben verwirft publishRGBColor() still.
    HALight::RGBColor color((shown.color >> 16) & 0xFF, (shown.color >> 8) & 0xFF,
                            shown.color & 0xFF);
    bool colorOk = haLight.setRGBColor(color);
    bool brightnessOk = haLight.setBrightness(shown.brightness);
    return colorOk && brightnessOk;
}

// Aus loop() aufrufen, nie aus einem Callback.
void syncHaLightState()
{
    if (!appState.haLightStateDirty)
        return;
    if (!appState.mqttEnabled || appState.sendingInitialStates || !mqtt.isConnected())
        return; // Flag bleibt gesetzt — nach Reconnect sendet sendInitialStates() ohnehin alles

    if (publishShownLight())
    {
        appState.haLightStateDirty = false;
    }
}

// Weltlage relativ an HA melden

// Zuletzt an HA gemeldete Stufe; -2 = noch nie gemeldet
static int publishedRelativeLevel = -2;

static bool publishRelativeLevel()
{
    int level = appState.relativeLevel;
    bool textOk = haSentimentRelative.setValue(relativeLevelText(level));
    char number[4];
    bool numberOk = relativeLevelNumber(level, number, sizeof(number))
                        ? haSentimentLevel.setValue(number)
                        : haSentimentLevel.setValue(nullptr); // "None" -> HA: unbekannt
    if (textOk && numberOk)
    {
        publishedRelativeLevel = level;
        return true;
    }
    return false;
}

// Aus loop() aufrufen, nie aus einem Callback.
void syncHaRelativeLevel()
{
    if (appState.relativeLevel == publishedRelativeLevel)
        return;
    if (!appState.mqttEnabled || appState.sendingInitialStates || !mqtt.isConnected())
        return; // nach Reconnect sendet sendInitialStates() ohnehin alles

    if (publishRelativeLevel())
    {
        debug(String(F("HA Weltlage relativ: ")) + relativeLevelText(appState.relativeLevel));
    }
}

// Initiale Zustände

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

    haLight.setState(appState.lightOn);
    // Ohne pixels.Color() berechnen, das bei uninitialisiertem NeoPixel abstürzt.
    publishShownLight();
    haMode.setState(appState.autoMode ? 0 : 1);

    haUpdateInterval.setState(float(appState.moodUpdateInterval / 1000.0));
    haDhtInterval.setState(float(appState.dhtUpdateInterval / 1000.0));

    // Letzte bekannte DHT-Werte, kein DHT-I/O im Reconnect-Kontext.
    if (!isnan(appState.currentTemp))
        haTemperature.setValue(floatToString(appState.currentTemp, 1).c_str());
    if (!isnan(appState.currentHum))
        haHumidity.setValue(floatToString(appState.currentHum, 1).c_str());

    haSentimentScore.setValue(floatToString(appState.sentimentScore, 2).c_str());
    haSentimentCategory.setValue(appState.sentimentCategory.c_str());
    haSentimentPercentile.setValue(floatToString(appState.percentile * 100.0, 0).c_str());
    // Weltlage relativ: vor dem ersten echten Wert "unbekannt"
    publishRelativeLevel();

    sendHeartbeat();

    debug(F("Initiale Zustände gesendet."));

    appState.haLightStateDirty = false;

    appState.sendingInitialStates = false;
}

// MQTT Reconnect

void checkAndReconnectMQTT() {
    if (!appState.mqttEnabled || appState.mqttServer.isEmpty())
        return;

    unsigned long currentMillis = millis();

    if (WiFi.status() == WL_CONNECTED && mqtt.isConnected()) {
        if (currentMillis - appState.lastMqttHeartbeat >= MQTT_HEARTBEAT_INTERVAL) {
            sendHeartbeat();
        }
    }

    // Reduce reconnection attempts frequency
    static unsigned long mqttReconnectBackoff = 10000; // Start with 10 seconds

    if (WiFi.status() == WL_CONNECTED) {
        if (!mqtt.isConnected()) {
            // Abriss merken, sonst feuert der Wiederverbunden-Zweig nie und die Status-LED bleibt im Modus 4.
            appState.mqttWasConnected = false;

            if (currentMillis - appState.lastMqttReconnectAttempt > mqttReconnectBackoff) {
                debug(F("MQTT nicht verbunden. Versuche Reconnect..."));

                // Zentrale Status-LED-Funktion nutzen (loggt AN/AUS-Wechsel selbst)
                if (appState.statusLedMode != 4) {
                    setStatusLED(4); // MQTT mode
                }

                // Ensure we're not in an interrupt context
                yield();

                mqtt.disconnect();

                // Kein Busy-Wait: HAMqtt::loop() verbindet selbst nach, ausgewertet beim nächsten Check.
                mqtt.begin(appState.mqttServer.c_str(), appState.mqttUser.c_str(), appState.mqttPassword.c_str());

                appState.lastMqttReconnectAttempt = currentMillis;
                // Backoff fuer den naechsten Versuch — wird bei Erfolg im else-Zweig zurueckgesetzt
                mqttReconnectBackoff = min(mqttReconnectBackoff * 2, 300000UL);
            }
        } else if (!appState.mqttWasConnected) {
            debug(F("MQTT wieder verbunden. Sende Zustände..."));

            appState.mqttInitialStatesPending = true;

            appState.mqttWasConnected = true;
            mqttReconnectBackoff = 10000; // Reset backoff bei erfolgreicher Verbindung

            // Nur Modus 4 zurücksetzen, die API-Fehleranzeige (2) bleibt.
            if (appState.statusLedMode == 4) {
                setStatusLED(0); // Normal mode — fordert selbst das volle LED-Update an
            } else if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
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
        appState.mqttWasConnected = true;
        sendInitialStates();
    } else {
        debug(F("MQTT-Initialisierung/Verbindung fehlgeschlagen - Fahre ohne MQTT fort"));
    }
}
