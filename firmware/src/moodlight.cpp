// AuraOS Moodlight: setup() und loop(), initialisiert die Module.

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <ArduinoHA.h>
#include <DHT.h>
#include <Preferences.h>
#include "esp_log.h"
#include <WebServer.h>
#include <DNSServer.h>
#include "esp_wifi.h"
#include "config.h"
#include "app_state.h"
#include "debug.h"
#include "settings_manager.h"
#include "wifi_manager.h"
#include "led_controller.h"
#include "sensor_manager.h"
#include "mqtt_handler.h"
#include "web_server.h"
#include "update_checker.h"

// Zentrale AppState-Instanz
AppState appState;

// Versionierung — für Module die String-Kontext brauchen
extern const String SOFTWARE_VERSION = MOODLIGHT_FULL_VERSION;

#include "MoodlightUtils.h"

WatchdogManager watchdog;
MemoryMonitor memMonitor;
SafeFileOps fileOps;
NetworkDiagnostics netDiag;
SystemHealthCheck sysHealth;


// === Arduino Setup ===
void setup() {
    Serial.begin(115200);

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("rmt", ESP_LOG_INFO);
    esp_log_level_set("tcpip_adapter", ESP_LOG_WARN);
    esp_log_level_set("phy", ESP_LOG_WARN);

    btStop();
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);  // WiFi-Subsystem initialisieren ohne aktiven Modus
    delay(100);

    Serial.println(F("==========================================="));
    Serial.println(F("AuraOS Moodlight — " MOODLIGHT_FULL_VERSION));
    Serial.println(F("==========================================="));
    debug(F("Starte Moodlight..."));

    // Hardware-Mutex ZUERST (wird von loadSettings/updateLEDs gebraucht)
    appState.ledMutex = xSemaphoreCreateMutex();

    // Dateisystem und Utils
    initFS();
    watchdog.begin(30, false);
    watchdog.registerCurrentTask();
    memMonitor.begin(60000);
    netDiag.begin(3600000);
    sysHealth.begin(&memMonitor, &netDiag);
    initJsonPool();
    loadSettings();

    // Hardware — DHT mit Pin aus Settings initialisieren
    delay(200);
    initDHT();

    setupWebServer();

    if (connectWiFiAndStartServices()) {
        connectMQTTOnStartup();
    }

    // NeoPixel-LEDs ZULETZT initialisieren
    delay(500);  // Laengere Pause damit WiFi-Subsystem stabil ist
    // Mit echten Parametern erzeugen, ein späteres setPin() konfiguriert den GPIO nicht.
    initPixels();
    debug(String(F("NeoPixel initialisiert: Pin ")) + String(appState.ledPin) +
          F(", LEDs ") + String(appState.numLeds) +
          F(", Bibliothek meldet Pin ") + String(pixels.getPin()));
    debug(F("Setup abgeschlossen."));

    appState.startupTime = millis();
    appState.initialStartupPhase = true;
    appState.ledSafeToShow = true;  // LEDs erst jetzt freigeben
    Serial.println("=========== Loop Start ===========");
}

// === Arduino Loop ===
void loop() {
    watchdog.feed();  // IMMER fuettern am Loop-Anfang (nicht autoFeed mit 15s Intervall)

    // Im AP/Config-Modus: DNS + WebServer + Settings-Save + Reboot
    if (appState.isInConfigMode) {
        dnsServer.processNextRequest();
        server.handleClient();
        // Ein verbundener Setup-Client hält den AP offen.
        if (WiFi.softAPgetStationNum() > 0) {
            appState.apModeStartTime = millis();
        }
        if (millis() - appState.apModeStartTime > AP_TIMEOUT) {
            ESP.restart();
        }
        // Settings-Save im Config-Modus (sonst gehen WiFi-Credentials verloren)
        if (appState.settingsNeedSaving && (millis() - appState.lastSettingsSaved > SETTINGS_SAVE_DEBOUNCE_MS)) {
            saveSettings();
            appState.settingsNeedSaving = false;
        }
        // Reboot im Config-Modus — Overflow-sicherer Vergleich (millis() wrapt nach ~49 Tagen)
        if (appState.rebootNeeded && (long)(millis() - appState.rebootTime) >= 0) {
            delay(200);
            ESP.restart();
        }
        // AP-Status-LED sichtbar machen und Busy-Loop beenden
        processLEDUpdates();
        updateStatusLED();
        delay(LOOP_DELAY_MS);
        return; // Im Config-Modus keine Sentiment/MQTT
    }

    initFirstLEDUpdate();

    // delay(LOOP_DELAY_MS) am Loop-Ende drosselt bereits.
    server.handleClient();

    // Neustart-Anforderung prüfen — Overflow-sicherer Vergleich (millis() wrapt nach ~49 Tagen)
    if (appState.rebootNeeded && (long)(millis() - appState.rebootTime) >= 0) {
        delay(200);
        ESP.restart();
    }

    if (appState.initialStartupPhase && (millis() - appState.startupTime > STARTUP_GRACE_PERIOD)) {
        appState.initialStartupPhase = false;
        debug(F("Startup grace period ended"));
    }

    checkNtpTimeSync();

    static unsigned long lastMqttLoop = 0;
    if (appState.mqttEnabled && WiFi.status() == WL_CONNECTED && (millis() - lastMqttLoop >= LOOP_MQTT_INTERVAL_MS)) {
        mqtt.loop();
        lastMqttLoop = millis();
    }

    static unsigned long lastConnectionCheck = 0;
    if (millis() - lastConnectionCheck >= LOOP_CONNECTION_CHECK_MS) {
        lastConnectionCheck = millis();
        checkAndReconnectWifi();
        if (appState.mqttEnabled) {
            checkAndReconnectMQTT();
        }
    }

    // Angezeigte Farbe/Helligkeit an HA melden (Flag aus updateLEDs()/Callbacks)
    if (appState.mqttEnabled) {
        syncHaLightState();
        syncHaRelativeLevel();
    }

    processLEDUpdates();

    if (appState.settingsNeedSaving && (millis() - appState.lastSettingsSaved > SETTINGS_SAVE_DEBOUNCE_MS)) {
        saveSettings();
        appState.settingsNeedSaving = false;
        appState.lastSettingsSaved = millis();
    }

    // 3 s nach dem Verbindungsaufbau warten, sonst LoadProhibited, wenn die Verbindung während http.GET() abbricht.
    bool wifiStable = (WiFi.status() == WL_CONNECTED) &&
                      (appState.wifiConnectedSince > 0) &&
                      (millis() - appState.wifiConnectedSince >= 3000);
    if (wifiStable) {
        // Force-Refresh aus HA-Button oder Web-UI: lastMoodUpdate zuruecksetzen
        if (appState.mqttRefreshPending) {
            appState.mqttRefreshPending = false;
            appState.lastMoodUpdate = 0;
            debug(F("Force-Refresh: lastMoodUpdate zurueckgesetzt"));
        }
        // Auch im Manual-Modus abrufen, damit die HA-Sensoren aktuell bleiben.
        getSentiment();
        watchdog.feed();  // WDT nach potentiell langem HTTP-Request fuettern
        readAndPublishDHT();
        watchdog.feed();  // WDT nach DHT-Lesung fuettern

        // Stündlich nach freigegebener Firmware fragen; installiert wird nur auf Klick.
        handleUpdateCheck();
        watchdog.feed();
    }

    updateStatusLED();

    if (millis() - appState.lastSystemHealthCheckTime >= HEALTH_CHECK_INTERVAL) {
        runSystemHealthCheck();
    }

    static unsigned long lastHealthCheck = 0;
    if (millis() - lastHealthCheck > HEALTH_CHECK_SHORT_INTERVAL) {
        sysHealth.update();
        memMonitor.update();
        lastHealthCheck = millis();
    }

    if (millis() - appState.lastStatusLog >= STATUS_LOG_INTERVAL) {
        logSystemStatus();
        appState.lastStatusLog = millis();
    }

    yield();
    delay(LOOP_DELAY_MS);
}
