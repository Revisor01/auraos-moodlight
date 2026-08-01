// ========================================================
// AuraOS Moodlight — Main Application
// ========================================================
// Orchestriert Module: WiFi, LED, Web-Server, MQTT, Settings, Sensor
// Zentrale setup()/loop() mit Modul-Initialisierung

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

    // Log-Level konfigurieren
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

    // Webserver-Routen definieren
    setupWebServer();

    // WiFi + NTP + mDNS + Server starten, dann MQTT
    if (connectWiFiAndStartServices()) {
        connectMQTTOnStartup();
    }

    // NeoPixel-LEDs ZULETZT initialisieren
    delay(500);  // Laengere Pause damit WiFi-Subsystem stabil ist
    // Instanz mit den echten Parametern erzeugen — exakt wie im minimalen
    // Testsketch, der nachweislich funktioniert. Nachtraegliches setPin() auf
    // einem parameterlos konstruierten Objekt hat den GPIO nie konfiguriert.
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
        // AP-Timeout nicht auffrischen waehrend aktiv konfiguriert wird —
        // ein verbundener Client (Handy/Laptop im Setup-WLAN) zaehlt als aktive Nutzung
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
        // AP-Status-LED sichtbar machen und Busy-Loop beenden (A-MITTEL Blockaden in loop())
        processLEDUpdates();
        updateStatusLED();
        delay(LOOP_DELAY_MS);
        return; // Im Config-Modus keine Sentiment/MQTT
    }

    // Erste LED-Initialisierung (nur im Normal-Modus)
    initFirstLEDUpdate();

    // Webserver-Anfragen verarbeiten — Gate entfernt, delay(LOOP_DELAY_MS) am Loop-Ende drosselt bereits
    server.handleClient();

    // Neustart-Anforderung prüfen — Overflow-sicherer Vergleich (millis() wrapt nach ~49 Tagen)
    if (appState.rebootNeeded && (long)(millis() - appState.rebootTime) >= 0) {
        delay(200);
        ESP.restart();
    }

    // Startup-Grace-Period beenden
    if (appState.initialStartupPhase && (millis() - appState.startupTime > STARTUP_GRACE_PERIOD)) {
        appState.initialStartupPhase = false;
        debug(F("Startup grace period ended"));
    }

    // Nicht-blockierende Pruefung der asynchronen NTP-Synchronisation
    checkNtpTimeSync();

    // MQTT-Loop periodisch ausführen
    static unsigned long lastMqttLoop = 0;
    if (appState.mqttEnabled && WiFi.status() == WL_CONNECTED && (millis() - lastMqttLoop >= LOOP_MQTT_INTERVAL_MS)) {
        mqtt.loop();
        lastMqttLoop = millis();
    }

    // Verbindungen prüfen
    static unsigned long lastConnectionCheck = 0;
    if (millis() - lastConnectionCheck >= LOOP_CONNECTION_CHECK_MS) {
        lastConnectionCheck = millis();
        checkAndReconnectWifi();
        if (appState.mqttEnabled) {
            checkAndReconnectMQTT();
        }
    }

    // LED-Updates verarbeiten
    processLEDUpdates();

    // Einstellungen speichern falls geändert
    if (appState.settingsNeedSaving && (millis() - appState.lastSettingsSaved > SETTINGS_SAVE_DEBOUNCE_MS)) {
        saveSettings();
        appState.settingsNeedSaving = false;
        appState.lastSettingsSaved = millis();
    }

    // Sentiment und Sensor bei aktiver WiFi-Verbindung
    // WiFi-Stabilitäts-Hysterese: 3s nach Verbindungsaufbau warten bevor HTTP-Calls erlaubt sind.
    // Verhindert LoadProhibited bei fluktuierendem Signal (Verbindung bricht während http.GET() ab).
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
        if (appState.autoMode) {
            getSentiment();
            watchdog.feed();  // WDT nach potentiell langem HTTP-Request fuettern
        }
        readAndPublishDHT();
        watchdog.feed();  // WDT nach DHT-Lesung fuettern

        // Stuendlich beim Backend nach freigegebener Firmware fragen.
        // Installiert wird nichts von selbst — nur auf Klick in der WebUI.
        handleUpdateCheck();
        watchdog.feed();
    }

    updateStatusLED();

    // System-Gesundheitsüberprüfung
    if (millis() - appState.lastSystemHealthCheckTime >= HEALTH_CHECK_INTERVAL) {
        runSystemHealthCheck();
    }

    // Kurzintervall-Systemcheck
    static unsigned long lastHealthCheck = 0;
    if (millis() - lastHealthCheck > HEALTH_CHECK_SHORT_INTERVAL) {
        sysHealth.update();
        memMonitor.update();
        lastHealthCheck = millis();
    }

    // Regelmäßige Statusprotokollierung
    if (millis() - appState.lastStatusLog >= STATUS_LOG_INTERVAL) {
        logSystemStatus();
        appState.lastStatusLog = millis();
    }

    yield();
    delay(LOOP_DELAY_MS);
}
