// ========================================================
// AuraOS Moodlight — Main Application
// ========================================================
// Orchestriert Module: WiFi, LED, Web-Server, MQTT, Settings, Sensor
// Zentrale setup()/loop() mit Modul-Initialisierung

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoHA.h>
#include <DHT.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "esp_log.h"
#include <WebServer.h>
#include <DNSServer.h>
#include "esp_wifi.h"
#include "esp_task_wdt.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "LittleFS.h"
#include <time.h>
#include "config.h"
#include "app_state.h"
#include "debug.h"
#include "settings_manager.h"
#include "wifi_manager.h"
#include "led_controller.h"
#include "sensor_manager.h"
#include "mqtt_handler.h"
#include "web_server.h"

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
    // Initialisiere serielle Kommunikation für Debug-Ausgaben
    Serial.begin(115200);
    
    // Watchdog-Timer initialisieren mit 30 Sekunden Timeout, ohne automatischen Reset
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // Arduino ESP32 Core 3.x (ESP-IDF >= 5.0): Config-Struktur erforderlich
    {
        esp_task_wdt_config_t wdt_config = {
            .timeout_ms    = 30000,
            .idle_core_mask = 0,
            .trigger_panic  = false
        };
        esp_task_wdt_init(&wdt_config);
    }
#else
    // Aeltere Versionen: Parameter direkt
    esp_task_wdt_init(30, false);
#endif

    // Stack-Auslastung des Loop-Tasks loggen
    {
        TaskHandle_t loopTask = xTaskGetCurrentTaskHandle();
        if (loopTask != NULL) {
            UBaseType_t stackSize = uxTaskGetStackHighWaterMark(loopTask);
            debug(String(F("Loop task stack high water mark: ")) + stackSize);
        }
    }
    
    // ESP-IDF Log-Level konfigurieren für bessere Diagnose
    esp_log_level_set("*", ESP_LOG_INFO);          // Allgemeines Log-Level
    esp_log_level_set("wifi", ESP_LOG_WARN);       // WiFi-Logs reduzieren
    esp_log_level_set("rmt", ESP_LOG_INFO);        // RMT-Log-Level (für LED-Steuerung)
    esp_log_level_set("tcpip_adapter", ESP_LOG_WARN);
    esp_log_level_set("phy", ESP_LOG_WARN);

    // Bluetooth deaktivieren um Speicher zu sparen
    btStop();

    // WiFi-Energiesparfunktionen deaktivieren für bessere Konnektivität
    WiFi.persistent(false);  // Verhindert Flash-Verschleiß durch häufiges Speichern
    WiFi.mode(WIFI_STA);     // Station-Modus setzen
    WiFi.setSleep(false);    // WiFi-Schlafmodus deaktivieren
    debug(F("Arduino WiFi Sleep disabled."));

    // Willkommensmeldung für Debug-Output
    Serial.println();
    Serial.println();
    Serial.println(F("==========================================="));
    Serial.println(F("Moodlight mit WiFi Manager & Settings Portal"));
    Serial.print("Version: ");
    Serial.println(MOODLIGHT_FULL_VERSION);
    Serial.println(F("==========================================="));
    Serial.println();

    debug(F("Starte Moodlight..."));

    // Dateisystem initialisieren
    initFS();

    // MoodlightUtils initialisieren (Hilfsfunktionen)
    debug(F("Initialisiere MoodlightUtils..."));
    
    // Watchdog initialisieren
    if (watchdog.begin(30, false)) {
        watchdog.registerCurrentTask();
        debug(F("Watchdog erfolgreich initialisiert"));
    }
    
    // Memory-Monitor starten für Speicherüberwachung
    if (memMonitor.begin(60000)) { // Berichte alle 60 Sekunden
        debug(F("Memory-Monitor gestartet"));
    }

    // Netzwerkdiagnostik initialisieren
    if (netDiag.begin(3600000)) { // Vollständige Analyse stündlich
        debug(F("Netzwerkdiagnostik initialisiert"));
    }
    
    // System-Gesundheitscheck initialisieren
    if (sysHealth.begin(&memMonitor, &netDiag)) {
        debug(F("System-Gesundheitscheck initialisiert"));
    }

    // JSON-Buffer-Pool initialisieren für effiziente JSON-Verarbeitung
    initJsonPool();

    // Einstellungen aus Flash/LittleFS laden
    loadSettings();

    // Hardware vorbereiten (ohne NeoPixel-Initialisierung)
    debug(F("Initialisiere Hardware (ohne NeoPixel)..."));
    delay(200);  // Kurze Pause für Stabilität

    // Mutex für sicheren LED-Zugriff erstellen
    appState.ledMutex = xSemaphoreCreateMutex();
    if (appState.ledMutex == NULL) {
        debug(F("Failed to create LED mutex!"));
    }

    // DHT-Sensor initialisieren (falls verwendet)
    dht.begin();

    // Webserver-Routen definieren (Server wird später gestartet)
    setupWebServer();

    // WiFi-Verbindung herstellen (falls konfiguriert)
    if (appState.wifiConfigured && !appState.wifiSSID.isEmpty()) {
        debug(String(F("Vorhandene WiFi-Konfiguration gefunden: ")) + appState.wifiSSID);

        // Verbindungsversuch
        bool wifiConnected = startWiFiStation();

        if (wifiConnected) {
            // Nach erfolgreicher Verbindung WiFi-Energiesparfunktionen deaktivieren
            debug(F("WiFi verbunden. Deaktiviere explizit Power Save..."));
            esp_wifi_set_ps(WIFI_PS_NONE);
            debug(F("ESP-IDF WiFi Power Save explicitly disabled (STA)."));

            // Zeit über NTP synchronisieren
            initTime();

            // Webserver starten
            debug(F("Starte Webserver..."));
            server.begin();
            debug(F("Webserver gestartet"));

            // mDNS-Service für lokale Namensauflösung starten
            if (MDNS.begin("moodlight")) {
                MDNS.addService("http", "tcp", 80);
                debug(F("mDNS responder gestartet"));
            } else {
                debug(F("mDNS start fehlgeschlagen"));
            }

            // MQTT einrichten für Home Assistant Integration
            if (appState.mqttEnabled && !appState.mqttServer.isEmpty()) {
                debug(F("MQTT Konfiguration gefunden, starte verzögerte Initialisierung..."));
                delay(500);

                setupHA();  // Home Assistant Entities konfigurieren

                // MQTT-Verbindung mit Timeout-Sicherheit aufbauen
                debug(F("Versuche MQTT zu initialisieren..."));
                unsigned long mqttStartTime = millis();
                bool mqttInitSuccess = false;

                try {
                    // MQTT-Verbindung starten (nicht-blockierend)
                    mqtt.begin(appState.mqttServer.c_str(), appState.mqttUser.c_str(), appState.mqttPassword.c_str());

                    // Kurzes Warten mit mqtt.loop() für Verbindungsaufbau
                    debug(F("Warte auf MQTT Verbindung (max 5s)..."));
                    while (!mqtt.isConnected() && (millis() - mqttStartTime < 5000)) {
                        mqtt.loop();
                        delay(100);
                    }
                    mqttInitSuccess = mqtt.isConnected();
                } catch (...) {
                    debug(F("Exception bei MQTT-Initialisierung"));
                    mqttInitSuccess = false;
                }

                if (mqttInitSuccess) {
                    debug(F("MQTT erfolgreich initialisiert und verbunden."));
                    sendInitialStates();  // Zustände an Home Assistant senden
                } else {
                    debug(F("MQTT-Initialisierung/Verbindung fehlgeschlagen - Fahre ohne MQTT fort"));
                }
            }
        } else {
            // WiFi-Verbindung fehlgeschlagen, starte Access Point
            debug(F("WiFi-Verbindung fehlgeschlagen. Starte Access Point..."));
            startAPModeWithServer();
        }
    } else {
        // Keine WiFi-Konfiguration vorhanden, starte Access Point
        debug(F("Keine WiFi-Konfiguration gefunden. Starte Access Point..."));
        startAPModeWithServer();
    }

    // NeoPixel-LEDs ZULETZT initialisieren (nach allem anderen)
    debug(F("Finalizing Hardware Init: NeoPixel..."));
    delay(200);
    pixels = Adafruit_NeoPixel(appState.numLeds, appState.ledPin, NEO_GRB + NEO_KHZ800);
    pixels.begin();
    pixels.setBrightness(DEFAULT_LED_BRIGHTNESS);
    debug(F("NeoPixel basic init done (begin/setBrightness)."));

    debug(F("Setup abgeschlossen."));
    
    // Startwerte für Betriebsparameter setzen
    appState.startupTime = millis();
    appState.initialStartupPhase = true;
    debug(F("Startup grace period active - ignoring mode changes for 15 seconds"));

    // Marker für den Start der Loop
    Serial.println("=========== Loop Start ===========");
}

// === Arduino Loop ===
void loop() {
    // Watchdog regelmäßig füttern, um automatischen Neustart zu verhindern
    watchdog.autoFeed();
    
    // Erste LED-Initialisierung nach dem Setup
    if (!appState.firstLedShowDone) {
        // LED-Puffer mit Nullen initialisieren für sauberen Start
        if (xSemaphoreTake(appState.ledMutex, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < appState.numLeds; i++) {
                appState.ledColors[i] = 0;
            }
            appState.ledClear = true;
            appState.ledUpdatePending = true;
            xSemaphoreGive(appState.ledMutex);
        }
        
        // LEDs werden durch processLEDUpdates aktualisiert
        appState.firstLedShowDone = true;
        debug(F("First LED update scheduled"));
    }

    // DNS-Anfragen verarbeiten, falls im Access-Point-Modus
    if (appState.isInConfigMode) {
        dnsServer.processNextRequest();

        // Timeout für AP-Modus prüfen (Neustart nach bestimmter Zeit)
        if (millis() - appState.apModeStartTime > AP_TIMEOUT) {
            debug(F("AP-Modus Timeout. Starte neu..."));
            ESP.restart();
        }
    }

    // Webserver-Anfragen verarbeiten mit reduzierter Frequenz
    static unsigned long lastServerHandle = 0;
    if (millis() - lastServerHandle >= LOOP_SERVER_HANDLE_MS) {
        server.handleClient();
        lastServerHandle = millis();
    }

    // Prüfen, ob ein Neustart angefordert wurde
    if (appState.rebootNeeded && millis() > appState.rebootTime) {
        debug(F("Ausführen des angeforderten Neustarts..."));
        delay(200);
        ESP.restart();
    }

    // Startup-Grace-Period beenden nach definierter Zeit
    if (appState.initialStartupPhase && (millis() - appState.startupTime > STARTUP_GRACE_PERIOD)) {
        appState.initialStartupPhase = false;
        debug(F("Initial startup grace period ended, now accepting all commands"));
    }
    
    // MQTT-Loop periodisch ausführen für Verbindungspflege
    static unsigned long lastMqttLoop = 0;
    if (appState.mqttEnabled && WiFi.status() == WL_CONNECTED && (millis() - lastMqttLoop >= LOOP_MQTT_INTERVAL_MS)) {
        mqtt.loop();
        lastMqttLoop = millis();
    }

    // WiFi- und MQTT-Verbindungen prüfen und wiederherstellen
    static unsigned long lastConnectionCheck = 0;
    if (millis() - lastConnectionCheck >= LOOP_CONNECTION_CHECK_MS) {
        lastConnectionCheck = millis();

        checkAndReconnectWifi();

        if (appState.mqttEnabled) {
            checkAndReconnectMQTT();
        }
    }

    // Anstehende LED-Updates verarbeiten
    processLEDUpdates();

    // Einstellungen speichern, falls geändert
    if (appState.settingsNeedSaving && (millis() - appState.lastSettingsSaved > SETTINGS_SAVE_DEBOUNCE_MS)) {
        debug(F("Verzögerte Speicherung ausführen..."));
        saveSettings();
        appState.settingsNeedSaving = false;
        appState.lastSettingsSaved = millis();
    }

    // Sentiment-Aktualisierung und DHT-Sensor auslesen bei aktiver WiFi-Verbindung
    if (WiFi.status() == WL_CONNECTED) {
        if (appState.autoMode) {
            getSentiment();  // Weltlage-Stimmung abrufen
        }
        readAndPublishDHT();  // DHT-Sensor auslesen
    } else if (appState.isPulsing) {
        // Pulsieren stoppen, wenn WiFi nicht verbunden
        appState.isPulsing = false;
        
        // LED-Helligkeit zurücksetzen
        if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            appState.ledBrightness = appState.autoMode ? DEFAULT_LED_BRIGHTNESS : appState.manualBrightness;
            appState.ledUpdatePending = true;
            xSemaphoreGive(appState.ledMutex);
        }
    }

    // Status-LED aktualisieren (blinken, je nach Modus)
    updateStatusLED();
    
    // Pulsieren der LEDs verarbeiten
    if (appState.isPulsing && appState.lightOn) {
        static unsigned long lastPulseUpdate = 0;
        if (millis() - lastPulseUpdate >= LOOP_PULSE_UPDATE_MS) {
            unsigned long currentTime = millis();
            unsigned long elapsedTime = currentTime - appState.pulseStartTime;
            
            // Auto-deaktivieren des Pulsierens nach Timeout
            if (elapsedTime > DEFAULT_WAVE_DURATION * 3) {
                debug(F("Pulse: Timeout - Auto-disable nach 3 Zyklen"));
                appState.isPulsing = false;
                
                if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
                    appState.ledBrightness = appState.autoMode ? DEFAULT_LED_BRIGHTNESS : appState.manualBrightness;
                    appState.ledUpdatePending = true;
                    xSemaphoreGive(appState.ledMutex);
                }
            } else {
                // Sinuswelle für sanftes Pulsieren berechnen
                float progress = fmod((float)elapsedTime, (float)DEFAULT_WAVE_DURATION) / (float)DEFAULT_WAVE_DURATION;
                float easedValue = (sin(progress * 2.0 * PI - PI / 2.0) + 1.0) / 2.0;
                
                // Helligkeit skalieren
                uint8_t targetBrightness = appState.autoMode ? DEFAULT_LED_BRIGHTNESS : appState.manualBrightness;
                uint8_t minPulseBright = (DEFAULT_WAVE_MIN_BRIGHTNESS < targetBrightness / 2) ? DEFAULT_WAVE_MIN_BRIGHTNESS : (targetBrightness / 2);
                uint8_t maxPulseBright = (DEFAULT_WAVE_MAX_BRIGHTNESS < targetBrightness) ? DEFAULT_WAVE_MAX_BRIGHTNESS : targetBrightness;
                
                int brightness = minPulseBright + (int)(easedValue * (maxPulseBright - minPulseBright));
                
                // LED-Helligkeit sicher aktualisieren
                if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
                    appState.ledBrightness = constrain(brightness, 0, 255);
                    appState.ledUpdatePending = true;
                    xSemaphoreGive(appState.ledMutex);
                }
            }
            lastPulseUpdate = millis();
        }
    }

    // System-Gesundheitsüberprüfung alle HEALTH_CHECK_INTERVAL (1 Stunde)
    unsigned long currentMillis = millis();
    if (currentMillis - appState.lastSystemHealthCheckTime >= HEALTH_CHECK_INTERVAL) {
        debug(F("Führe regelmäßige Systemprüfung durch..."));
        
        // Memory-Analyse durchführen
        memMonitor.update();
        
        // Systemstatistiken in JSON-Datei speichern
        if (LittleFS.exists("/data")) {
            JsonDocument statsDoc;
            
            // System-Informationen
            statsDoc["timestamp"] = currentMillis / 1000;
            statsDoc["uptime"] = currentMillis / 1000;
            statsDoc["heap"] = ESP.getFreeHeap();
            statsDoc["maxBlock"] = ESP.getMaxAllocHeap();
            statsDoc["fragmentation"] = 100.0f - ((float)ESP.getMaxAllocHeap() / ESP.getFreeHeap() * 100.0f);
            
            // Dateisystem-Informationen
            statsDoc["fsTotal"] = LittleFS.totalBytes();
            statsDoc["fsUsed"] = LittleFS.usedBytes();
            
            // WiFi-Informationen
            if (WiFi.status() == WL_CONNECTED) {
                statsDoc["wifiConnected"] = true;
                statsDoc["rssi"] = WiFi.RSSI();
            } else {
                statsDoc["wifiConnected"] = false;
            }
            
            // CPU-Temperatur auslesen
            statsDoc["temperature"] = temperatureRead();
            
            // Statistik-Datei rotieren
            static int fileCounter = 0;
            String fileName = "/data/sysstat_" + String(fileCounter++ % SYSSTAT_FILE_ROTATION) + ".json";
            
            String jsonStr;
            serializeJson(statsDoc, jsonStr);
            
            File statFile = LittleFS.open(fileName, "w");
            if (statFile) {
                statFile.print(jsonStr);
                statFile.close();
            }
        }
        
        // Systemgesundheit aktualisieren vor Restart-Entscheidung
        sysHealth.update();

        // Prüfen, ob Neustart empfohlen wird
        if (sysHealth.isRestartRecommended()) {
            debug(F("System empfiehlt Neustart - plane Neustart für 3:00 Uhr..."));
            
            // Aktuelle Uhrzeit ermitteln
            time_t now;
            time(&now);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            
            // Neustart in der Nacht planen
            if (timeinfo.tm_hour >= NIGHT_REBOOT_HOUR_START && timeinfo.tm_hour < NIGHT_REBOOT_HOUR_END) {
                debug(F("Nachtstunden, führe Neustart sofort durch..."));
                appState.rebootNeeded = true;
                appState.rebootTime = currentMillis + NIGHT_REBOOT_DELAY;
            } else {
                debug(F("Neustart verschoben auf Nachtstunden..."));
                // Flag für späteren Neustart setzen
                preferences.begin("syshealth", false);
                preferences.putBool("restartPending", true);
                preferences.end();
            }
        }
        
        // Geplanten Neustart prüfen
        preferences.begin("syshealth", true);
        bool restartPending = preferences.getBool("restartPending", false);
        preferences.end();
        
        if (restartPending) {
            // Aktuelle Uhrzeit ermitteln
            time_t now;
            time(&now);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            
            // Neustart in der Nacht ausführen
            if (timeinfo.tm_hour >= SCHEDULED_REBOOT_HOUR_START && timeinfo.tm_hour < SCHEDULED_REBOOT_HOUR_END) {
                debug(F("Geplanter Neustart wird ausgeführt..."));
                appState.rebootNeeded = true;
                appState.rebootTime = currentMillis + SCHEDULED_REBOOT_DELAY;
                
                // Neustart-Flag zurücksetzen
                preferences.begin("syshealth", false);
                preferences.putBool("restartPending", false);
                preferences.end();
            }
        }
        
        // Speicherplatz-Prüfung und ggf. Archivierung
        uint64_t total, used, free;
        getStorageInfo(total, used, free);
        float percentUsed = (float)used * 100.0 / total;
        
        if (percentUsed > STORAGE_WARNING_PERCENT) {
            debug(F("Hohe Dateisystembelegung erkannt"));
        }
        
        appState.lastSystemHealthCheckTime = currentMillis;
    }

    // Regelmäßige Statusprotokollierung
    if (millis() - appState.lastStatusLog >= STATUS_LOG_INTERVAL) {
        logSystemStatus();
        appState.lastStatusLog = millis();
    }

    // System etwas Zeit geben - verhindert 100% CPU-Auslastung
    yield();
    delay(LOOP_DELAY_MS);

    // Regelmaessiger Systemgesundheitscheck
    static unsigned long lastHealthCheck = 0;
    if (millis() - lastHealthCheck > HEALTH_CHECK_SHORT_INTERVAL) {
        sysHealth.update();
        memMonitor.update();
        lastHealthCheck = millis();
    }
}