#ifndef APP_STATE_H
#define APP_STATE_H

// Zentrales AppState-Struct fuer alle geteilten globalen Variablen.
// Hardware-Library-Instanzen (pixels, dht, preferences, server, dnsServer,
// ArduinoHA-Objekte, WiFiClient, Utility-Instanzen, JsonBufferPool)
// gehoeren NICHT hierher — sie bleiben als separate Globals in moodlight.cpp.
//
// Diese Datei definiert nur das Interface; die Definition von `appState`
// erfolgt in Plan 02 in moodlight.cpp.

#include "config.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct AppState {

    // =========================================================
    // WiFi-Gruppe
    // =========================================================
    String wifiSSID = "";
    String wifiPassword = "";
    bool wifiConfigured = false;
    bool isInConfigMode = false;
    bool wifiReconnectActive = false;       // Unterdrueckt pixels.show() waehrend Reconnect
    unsigned long disconnectStartMs = 0;    // Grace-Timer fuer Status-LED
    unsigned long lastWifiCheck = 0;
    unsigned long wifiReconnectDelay = 5000;
    int wifiReconnectAttempts = 0;
    bool wifiWasConnected = false;
    unsigned long apModeStartTime = 0;
    unsigned long wifiConnectedSince = 0;   // Zeitstempel der letzten erfolgreichen Verbindung (für Stabilitäts-Hysterese)

    // =========================================================
    // LED-Gruppe
    // =========================================================
    int ledPin = DEFAULT_LED_PIN;
    int numLeds = DEFAULT_NUM_LEDS;
    uint8_t manualBrightness = DEFAULT_LED_BRIGHTNESS;
    bool lightOn = true;
    bool autoMode = true;
    uint32_t manualColor = 0x00FFFFFF;       // Default weiss; wird von Preferences ueberschrieben
    int currentLedIndex = 2;
    int lastLedIndex = 2;
    volatile bool ledUpdatePending = false;
    volatile uint32_t ledColors[MAX_LEDS] = {0};
    volatile uint8_t ledBrightness = DEFAULT_LED_BRIGHTNESS;
    volatile bool ledClear = false;
    SemaphoreHandle_t ledMutex = NULL;
    uint32_t customColors[5] = {0xFF0000, 0xFFA500, 0x1E90FF, 0x545DF0, 0x8A2BE2};
    bool firstLedShowDone = false;
    bool ledSafeToShow = false;  // Wird erst true nach WiFi-Init + NeoPixel-Init
    bool isPulsing = false;
    unsigned long pulseStartTime = 0;
    int statusLedIndex = DEFAULT_NUM_LEDS - 1;
    unsigned long statusLedBlinkStart = 0;
    bool statusLedState = false;
    int statusLedMode = 0; // 0=Normal, 1=WiFi-Verbindung, 2=API-Fehler, 3=Update, 4=MQTT-Verbindung, 5=AP-Modus

    // =========================================================
    // Sentiment-Gruppe
    // =========================================================
    float sentimentScore = 0.0;                              // Umbenannt von lastSentimentScore
    String sentimentCategory = "neutral";                    // Umbenannt von lastSentimentCategory
    unsigned long moodUpdateInterval = DEFAULT_MOOD_UPDATE_INTERVAL;
    unsigned long lastMoodUpdate = 0;
    bool initialAnalysisDone = false;
    unsigned long lastSuccessfulSentimentUpdate = 0;
    bool sentimentAPIAvailable = true;
    int consecutiveSentimentFailures = 0;
    String apiUrl = DEFAULT_NEWS_API_URL;

    // =========================================================
    // MQTT-Gruppe
    // =========================================================
    String mqttServer = "";
    String mqttUser = "";
    String mqttPassword = "";
    bool mqttEnabled = false;
    unsigned long lastMqttReconnectAttempt = 0;
    unsigned long lastMqttHeartbeat = 0;
    bool mqttWasConnected = false;
    bool sendingInitialStates = false;
    bool mqttRefreshPending = false;        // Force-Refresh aus HA-Callback, wird in loop() ausgeführt
    bool mqttInitialStatesPending = false;  // Initiale Zustände nach MQTT-Reconnect senden

    // =========================================================
    // DHT/Sensor-Gruppe
    // =========================================================
    bool dhtEnabled = true;
    int dhtPin = DEFAULT_DHT_PIN;
    unsigned long dhtUpdateInterval = DEFAULT_DHT_READ_INTERVAL;
    unsigned long lastDHTUpdate = 0;
    float currentTemp = NAN;
    float currentHum = NAN;

    // =========================================================
    // System-Gruppe
    // =========================================================
    bool rebootNeeded = false;
    unsigned long rebootTime = 0;
    bool initialStartupPhase = true;
    unsigned long startupTime = 0;
    bool timeInitialized = false;
    unsigned long lastSystemHealthCheckTime = 0;
    unsigned long lastStatusLog = 0;
    bool settingsNeedSaving = false;
    unsigned long lastSettingsSaved = 0;

    // =========================================================
    // Log-Gruppe
    // =========================================================
    String logBuffer[20];  // Ringpuffer fuer Log-Zeilen (20 Eintraege, Fix-Wert)
    int logIndex = 0;      // Aktuelle Position im Ringpuffer

};

// Externe Deklaration — Definition erfolgt in moodlight.cpp (Plan 02)
extern AppState appState;

#endif // APP_STATE_H
