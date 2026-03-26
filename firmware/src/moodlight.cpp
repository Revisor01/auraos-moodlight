// ========================================================
// Moodlight mit OpenAI Sentiment Analyse & Home Assistant
// ========================================================
// Version: 9.0 - Backend-Optimized Architecture
// - Removed RSS Feed configuration (hardcoded in backend)
// - Removed local CSV statistics (fetched from backend)
// - Uses /api/moodlight/* endpoints with Redis caching
// Erwartet Sentiment Score von -1.0 bis +1.0 vom Backend

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
#include <Update.h>
#include <DNSServer.h>
#include "esp_wifi.h"
#include "esp_task_wdt.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "LittleFS.h"
#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>
#include <time.h>
#define DEBUG_MODE  // AKTIVIERT für besseres Debugging
// #define CONFIG_FREERTOS_UNICORE
#include "config.h"
#include "app_state.h"
#include "settings_manager.h"
#include "wifi_manager.h"

// Zentrale AppState-Instanz — alle geteilten Globals migrieren hierher (Plan 02)
AppState appState;

// 192.168.4.1 - Standard-IP für den Access Point
#define CAPTIVE_PORTAL_IP \
    {192, 168, 4, 1}

    // JSON-Puffer-Pool für HTTP-Antworten
#define JSON_BUFFER_SIZE 16384
#define JSON_BUFFER_COUNT 2  // Anzahl der Puffer im Pool

// DNS Server für den Captive Portal
DNSServer dnsServer;
extern const byte DNS_PORT = 53;
// appState.isInConfigMode -> appState.appState.isInConfigMode (migriert)

// === Farbdefinitionen ===
struct ColorDefinition
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// appState.customColors -> appState.appState.customColors (migriert)

// Farbnamen für UI
const char *colorNames[5] = {
    "Sehr negativ",
    "Negativ",
    "Neutral",
    "Positiv",
    "Sehr positiv"};

// Hilfsfunktion, um uint32_t in ColorDefinition zu konvertieren
ColorDefinition uint32ToColorDef(uint32_t color)
{
    ColorDefinition def;
    def.r = (color >> 16) & 0xFF;
    def.g = (color >> 8) & 0xFF;
    def.b = color & 0xFF;
    return def;
}

// Hilfsfunktion zum dynamischen Abrufen von Farbdefinitionen
ColorDefinition getColorDefinition(int index)
{
    index = constrain(index, 0, 4);
    return uint32ToColorDef(appState.customColors[index]);
}

// === Hilfsfunktion ===
String floatToString(float value, int decimalPlaces)
{
    char buffer[16];
    dtostrf(value, 0, decimalPlaces, buffer);
    return String(buffer);
}

// === ArduinoHA Setup ===
WiFiClient wifiClientHA;
WiFiClient wifiClientHTTP; // HTTP Client für Backend API
byte mac[6];
HADevice device; // Wird in setupHA initialisiert
HAMqtt mqtt(wifiClientHA, device);
HASensor haSentimentScore("sentiment_score", HASensor::PrecisionP2);
HASensor haTemperature("temperature", HASensor::PrecisionP1);
HASensor haHumidity("humidity", HASensor::PrecisionP0);
HALight haLight("moodlight", HALight::BrightnessFeature | HALight::RGBFeature);
HASelect haMode("mode");
HAButton haRefreshSentiment("refresh_sentiment");
HANumber haUpdateInterval("update_interval", HANumber::PrecisionP0);
HANumber haDhtInterval("dht_interval", HANumber::PrecisionP0);
// v9.0: haHeadlinesPerSource removed - parameter only for legacy API endpoints
HASensor haSentimentCategory("sentiment_category");
// Heartbeat-Sensoren
HASensor haUptime("uptime");
HASensor haWifiSignal("wifi_signal");
HASensor haSystemStatus("system_status");

// === Webserver für Updates und Logging ===
WebServer server(80);
// logBuffer/logIndex -> appState.logBuffer/appState.logIndex (migriert)
const int LOG_BUFFER_SIZE = 20; // Groesse des Ringpuffers (muss mit AppState.logBuffer[20] uebereinstimmen)

// === Hardware Setup ===
Adafruit_NeoPixel pixels;
DHT dht(DEFAULT_DHT_PIN, DHT22);
Preferences preferences;

// NTP constants (nicht im AppState - compile-time constants)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;     // GMT+1 (Adjust for your timezone, in seconds)
const int   daylightOffset_sec = 3600; // +1 hour for DST (summer time)
// appState.timeInitialized -> appState.appState.timeInitialized (migriert)

// === Globale Variablen (migrierte Variablen sind in AppState appState) ===

const String SOFTWARE_VERSION = MOODLIGHT_FULL_VERSION;

// Konstanten bleiben als #define / const (nicht im AppState)
const unsigned long STARTUP_GRACE_PERIOD = 15000;
extern const unsigned long MAX_RECONNECT_DELAY = 300000; // Maximale Verzögerung (5 Minuten)
extern const unsigned long STATUS_LED_GRACE_MS = 30000;  // LED-03: 30s bevor Status-LED aktiviert wird
const unsigned long MQTT_HEARTBEAT_INTERVAL = 60000; // 1 Minute
const unsigned long SENTIMENT_FALLBACK_TIMEOUT = 3600000; // Nach 1 Stunde ohne erfolgreiche Aktualisierung
const int MAX_SENTIMENT_FAILURES = 5;
const unsigned long STATUS_LOG_INTERVAL = 300000; // 5 Minuten
extern const unsigned long AP_TIMEOUT = 300000; // 5 Minuten
const unsigned long REBOOT_DELAY = 5000; // 5 Sekunden bis zum Reboot

#ifdef DEBUG_MODE
// === Debug-Funktion mit Logging ===
void debug(const String &message) {
    // Use static buffer for timestamps to avoid repeated allocations
    static char timeBuffer[16];
    static char messageBuffer[256];
    
    // Calculate timestamp
    unsigned long ms = millis();
    snprintf(timeBuffer, sizeof(timeBuffer), "[%lus] ", ms / 1000);
    
    // Copy message safely into buffer
    snprintf(messageBuffer, sizeof(messageBuffer), "%s%s", timeBuffer, message.c_str());
    
    // Store in ring buffer (use a local copy)
    String logEntry = messageBuffer; // Create the String locally
    appState.logBuffer[appState.logIndex] = logEntry;  // Copy assignment is safer
    appState.logIndex = (appState.logIndex + 1) % LOG_BUFFER_SIZE;
    
    // Print to Serial
    Serial.print(messageBuffer);
    Serial.print(F(" (Mem: "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(")"));
}

void debug(const __FlashStringHelper *message) {
    static char timeBuffer[16];
    static char messageBuffer[256];
    
    // Calculate timestamp
    unsigned long ms = millis();
    snprintf(timeBuffer, sizeof(timeBuffer), "[%lus] ", ms / 1000);
    
    // Create combined message (avoid String operations)
    snprintf(messageBuffer, sizeof(messageBuffer), "%s%s", timeBuffer, (const char*)message);
    
    // Store in ring buffer (using direct copy)
    String logEntry = messageBuffer;
    appState.logBuffer[appState.logIndex] = logEntry;
    appState.logIndex = (appState.logIndex + 1) % LOG_BUFFER_SIZE;
    
    // Print to Serial
    Serial.print(messageBuffer);
    Serial.print(F(" (Mem: "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(")"));
}
#else

void debug(const String &message) {
    if (message.startsWith("ERROR:") || message.startsWith("CRITICAL:")) {
        Serial.println(message);
    }
}

void debug(const __FlashStringHelper *message) {
    // Optional: Nur kritische Meldungen
    if (strstr_P((PGM_P)message, PSTR("ERROR:")) || strstr_P((PGM_P)message, PSTR("CRITICAL:"))) {
        Serial.println(message);
    }
}

#endif

void handleStaticFile(String path) {
    if (path.endsWith("/")) path += "index.html";
  
    String contentType = "text/html";
    if (path.endsWith(".css")) contentType = "text/css";
    else if (path.endsWith(".js")) contentType = "application/javascript";
    else if (path.endsWith(".json")) contentType = "application/json";
    else if (path.endsWith(".png")) contentType = "image/png";
    else if (path.endsWith(".jpg")) contentType = "image/jpeg";
    else if (path.endsWith(".ico")) contentType = "image/x-icon";
  
    if (!path.startsWith("/")) {
      path = "/" + path;
    }
  
    if (LittleFS.exists(path)) {
      File file = LittleFS.open(path, "r");
      server.streamFile(file, contentType);
      file.close();
    } else {
      server.send(404, "text/plain", "Datei nicht gefunden: " + path);
    }
}

#include "MoodlightUtils.h"

WatchdogManager watchdog;
MemoryMonitor memMonitor;
SafeFileOps fileOps;
// v9.0: CSVBuffer removed - stats from backend
// CSVBuffer statsBuffer("/data/stats.csv");
NetworkDiagnostics netDiag;
SystemHealthCheck sysHealth;
// v9.0: archiveTask removed - archiving handled in backend
// appState.lastSystemHealthCheckTime -> appState.appState.lastSystemHealthCheckTime (migriert)
const unsigned long HEALTH_CHECK_INTERVAL = 3600000; // 1 Stunde

struct JsonBufferPool {
    char buffers[JSON_BUFFER_COUNT][JSON_BUFFER_SIZE];
    bool inUse[JSON_BUFFER_COUNT];
    SemaphoreHandle_t mutex;
    
    // Initialisiert den Pool
    void init() {
        mutex = xSemaphoreCreateMutex();
        for (int i = 0; i < JSON_BUFFER_COUNT; i++) {
            inUse[i] = false;
        }
        debug(F("JSON-Puffer-Pool initialisiert"));
    }
    
    // Reserviert einen Puffer
    char* acquire() {
        if (xSemaphoreTake(mutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
            for (int i = 0; i < JSON_BUFFER_COUNT; i++) {
                if (!inUse[i]) {
                    inUse[i] = true;
                    xSemaphoreGive(mutex);
                    return buffers[i];
                }
            }
            xSemaphoreGive(mutex);
        }
        // Fallback wenn kein Puffer verfügbar ist
        debug(F("WARNUNG: Kein JSON-Puffer verfügbar, verwende Heap!"));
        return new char[JSON_BUFFER_SIZE];
    }
    
    // Gibt einen Puffer frei
    void release(char* buffer) {
        if (!buffer) return;
        if (xSemaphoreTake(mutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
            for (int i = 0; i < JSON_BUFFER_COUNT; i++) {
                if (buffer == buffers[i]) {
                    inUse[i] = false;
                    xSemaphoreGive(mutex);
                    return;
                }
            }
            xSemaphoreGive(mutex);
        }
        // KRITISCHER FIX: delete[] IMMER wenn nicht im Pool —
        // unabhaengig ob Mutex erfolgreich war
        delete[] buffer;
    }
};

JsonBufferPool jsonPool;

// RAII-Guard fuer sichere JSON-Buffer-Verwaltung (automatisches release() im Destruktor)
class JsonBufferGuard {
public:
    char* buf;
    JsonBufferGuard() : buf(jsonPool.acquire()) {}
    ~JsonBufferGuard() { if (buf) jsonPool.release(buf); }
    // Nicht kopierbar
    JsonBufferGuard(const JsonBufferGuard&) = delete;
    JsonBufferGuard& operator=(const JsonBufferGuard&) = delete;
};

// === Aktualisiere die LEDs ===
void updateLEDs() {
    if (!appState.lightOn) {
        // Just set the clear flag and return
        if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            appState.ledClear = true;
            appState.ledUpdatePending = true;
            xSemaphoreGive(appState.ledMutex);
        }
        return;
    }

    // Licht soll an sein
    uint32_t colorToShow;
    uint8_t brightnessToShow;

    if (appState.autoMode) {
        appState.currentLedIndex = constrain(appState.currentLedIndex, 0, 4);
        ColorDefinition color = getColorDefinition(appState.currentLedIndex);
        colorToShow = pixels.Color(color.r, color.g, color.b);
        brightnessToShow = DEFAULT_LED_BRIGHTNESS;
    } else {
        colorToShow = appState.manualColor;
        brightnessToShow = appState.manualBrightness;
    }

    // Instead of directly updating LEDs, store the values safely
    if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        // Fill LED buffer
        for (int i = 0; i < appState.numLeds; i++) {
            // Skip status LED if in status mode
            if (i == (appState.numLeds - 1) && appState.statusLedMode != 0) {
                continue;
            }
            appState.ledColors[i] = colorToShow;
        }
        appState.ledBrightness = brightnessToShow;
        appState.ledClear = false;
        appState.ledUpdatePending = true;
        xSemaphoreGive(appState.ledMutex);
    }

    // Debug output optimized
    uint8_t r = (colorToShow >> 16) & 0xFF;
    uint8_t g = (colorToShow >> 8) & 0xFF;
    uint8_t b = colorToShow & 0xFF;

    char debugBuffer[100];
    snprintf(debugBuffer, sizeof(debugBuffer),
             "LEDs update requested: %s B=%d RGB(%d,%d,%d)",
             (appState.autoMode ? "Auto" : "Manual"),
             brightnessToShow, r, g, b);
    debug(debugBuffer);
}

// === Status-LED Funktionen ===
void setStatusLED(int mode) {
    appState.statusLedMode = mode;
    appState.statusLedBlinkStart = millis();
    appState.statusLedState = true;

    // Store LED state but don't update directly
    if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        // LED-Farbe entsprechend dem Status setzen
        uint32_t statusColor = pixels.Color(0, 0, 0); // Default black
        
        switch (mode) {
            case 1: // WiFi-Verbindung (blau blinkend)
                statusColor = pixels.Color(0, 0, 255);
                break;
            case 2: // API-Fehler (rot blinkend)
                statusColor = pixels.Color(255, 0, 0);
                break;
            case 3: // Update (grün blinkend)
                statusColor = pixels.Color(0, 255, 0);
                break;
            case 4: // MQTT-Verbindung (cyan blinkend)
                statusColor = pixels.Color(0, 255, 255);
                break;
            case 5: // AP-Modus (gelb blinkend)
                statusColor = pixels.Color(255, 255, 0);
                break;
            default: // Normal (LED aus oder normaler Betrieb)
                // Just update the whole LED strip
                appState.ledUpdatePending = true;
                xSemaphoreGive(appState.ledMutex);
                return;
        }

        // Only update the status LED
        appState.ledColors[appState.statusLedIndex] = statusColor;
        appState.ledUpdatePending = true;
        xSemaphoreGive(appState.ledMutex);
    }
}

void updateStatusLED() {
    // Kein Blinken im Normalmodus
    if (appState.statusLedMode == 0)
        return;

    // Zeit für Blink-Update
    unsigned long currentMillis = millis();

    // Unterschiedliche Blink-Frequenzen für verschiedene Modi
    unsigned long blinkInterval;
    switch (appState.statusLedMode) {
        case 1: blinkInterval = 500; break; // WiFi (schnell)
        case 2: blinkInterval = 300; break; // API-Fehler (sehr schnell)
        case 3: blinkInterval = 1000; break; // Update (langsam)
        case 4: blinkInterval = 700; break; // MQTT (mittel)
        case 5: blinkInterval = 200; break; // AP-Modus (sehr schnell)
        default: blinkInterval = 500;
    }

    // Zeit abgelaufen?
    if (currentMillis - appState.statusLedBlinkStart >= blinkInterval) {
        appState.statusLedState = !appState.statusLedState;
        appState.statusLedBlinkStart = currentMillis;

        if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            if (appState.statusLedState) {
                // LED einschalten mit entsprechender Farbe
                uint32_t statusColor = pixels.Color(0, 0, 0);
                
                switch (appState.statusLedMode) {
                    case 1: statusColor = pixels.Color(0, 0, 255); break; // Blau
                    case 2: statusColor = pixels.Color(255, 0, 0); break; // Rot
                    case 3: statusColor = pixels.Color(0, 255, 0); break; // Grün
                    case 4: statusColor = pixels.Color(0, 255, 255); break; // Cyan
                    case 5: statusColor = pixels.Color(255, 255, 0); break; // Gelb
                }
                
                appState.ledColors[appState.statusLedIndex] = statusColor;
            } else {
                // LED ausschalten
                appState.ledColors[appState.statusLedIndex] = pixels.Color(0, 0, 0);
            }
            
            appState.ledUpdatePending = true;
            xSemaphoreGive(appState.ledMutex);
        }
    }
}

void processLEDUpdates() {
    static unsigned long lastLedUpdateTime = 0;
    unsigned long currentTime = millis();
    
    // Only process LED updates every 50ms maximum to avoid conflicts with WiFi
    if (currentTime - lastLedUpdateTime < 50) {
        return;
    }
    
    // Check if we have pending LED updates
    bool needsUpdate = false;
    
    if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        needsUpdate = appState.ledUpdatePending;
        if (needsUpdate) {
            // Apply the stored LED values
            if (appState.ledClear) {
                pixels.clear();
            } else {
                pixels.setBrightness(appState.ledBrightness);
                
                for (int i = 0; i < appState.numLeds; i++) {
                    pixels.setPixelColor(i, appState.ledColors[i]);
                }
            }
            appState.ledUpdatePending = false;
        }
        xSemaphoreGive(appState.ledMutex);
    }
    
    // Only call show() if we have updates and not in a critical section
    if (needsUpdate) {
        // LED-02: LEDs nur aktualisieren wenn kein WiFi-Reconnect aktiv ist
        if (!appState.wifiReconnectActive) {
            // Wait for any ongoing interrupt operations
            yield();
            delay(1);
            
            // Now it should be safe to update LEDs
            pixels.show();
            lastLedUpdateTime = currentTime;
            
            static unsigned long lastShowDebug = 0;
            if (currentTime - lastShowDebug > 10000) {
                debug(F("LED show() executed safely"));
                lastShowDebug = currentTime;
            }
        }
    }
}

// ===== REMOVED IN v9.0: RSS Feed Configuration =====
// RSS feeds are now hardcoded in backend (app.py)
// No longer needed on device - reduces memory usage and complexity

// ===== NEW IN v9.0: Backend Statistics API =====
// Fetch statistics from backend instead of generating locally
// Reduces device load and enables centralized analytics

bool fetchBackendStatistics(JsonDocument& doc, int hours = 168) {
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

void initFS() {
    if (!LittleFS.begin()) {
        debug(F("LittleFS Mount Failed, attempting format..."));
        if (!LittleFS.format()) {
            debug(F("LittleFS Format Failed!"));
            return;
        }
        if (!LittleFS.begin()) {
            debug(F("LittleFS Still Not Working After Format!"));
            return;
        }
    }

    // Create required directories if they don't exist
    const char* directories[] = {"/data", "/temp", "/extract", "/extract/css", "/extract/js", "/backup", "/css", "/js"};
    for (const char* dir : directories) {
        if (!LittleFS.exists(dir)) {
            debug(String(F("Creating directory: ")) + dir);
            if (!LittleFS.mkdir(dir)) {
                debug(String(F("Failed to create directory: ")) + dir);
                // Continue anyway, the operation might work later
            }
        }
    }

    // Create default files if they don't exist
    // REMOVED v9.0: RSS feeds now managed in backend
    // if (!LittleFS.exists("/data/feeds.json")) {
    //     debug(F("Creating default feeds.json..."));
    //     saveDefaultRssFeeds();
    // }

    // REMOVED v9.0: Stats now managed in backend, no local CSV needed
    // if (!LittleFS.exists("/data/stats.csv")) {
    //     debug(F("Creating default stats.csv..."));
    // }

    if (!LittleFS.exists("/ui-version.txt")) {
        debug(F("Creating ui-version.txt..."));
        File versionFile = LittleFS.open("/ui-version.txt", "w");
        if (versionFile) {
            versionFile.print(SOFTWARE_VERSION);
            versionFile.close();
        }
    }
    
    if (!LittleFS.exists("/firmware-version.txt")) {
        debug(F("Creating firmware-version.txt..."));
        File versionFile = LittleFS.open("/firmware-version.txt", "w");
        if (versionFile) {
            versionFile.print(SOFTWARE_VERSION);
            versionFile.close();
        }
    }
}

// Helper function to copy a file
bool copyFile(const String& source, const String& destination) {
    // Create local char buffers for paths
    char sourceBuffer[64]; 
    char destBuffer[64];
    
    // Copy strings to local buffers
    source.toCharArray(sourceBuffer, sizeof(sourceBuffer));
    destination.toCharArray(destBuffer, sizeof(destBuffer));
    
    if (!LittleFS.exists(sourceBuffer)) {
        debug("Quelldatei existiert nicht: " + source);
        return false;
    }
    
    File sourceFile = LittleFS.open(sourceBuffer, "r");
    if (!sourceFile) {
        debug("Quelldatei konnte nicht geöffnet werden: " + source);
        return false;
    }
    
    File destFile = LittleFS.open(destBuffer, "w");
    if (!destFile) {
        debug("Zieldatei konnte nicht erstellt werden: " + destination);
        sourceFile.close();
        return false;
    }
    
    static uint8_t buffer[512];
    size_t bytesRead = 0;
    
    while ((bytesRead = sourceFile.read(buffer, sizeof(buffer))) > 0) {
        if (destFile.write(buffer, bytesRead) != bytesRead) {
            debug(F("Fehler beim Schreiben in Zieldatei"));
            sourceFile.close();
            destFile.close();
            return false;
        }
        yield(); // Allow for WiFi processing
    }
    
    debug("Datei kopiert von " + source + " nach " + destination);
    destFile.close();
    sourceFile.close();
    return true;
}
// Helper function to move a file
bool moveFile(const String& source, const String& destination) {
    if (!LittleFS.exists(source)) {
        return false;
    }
    
    if (!copyFile(source, destination)) {
        return false;
    }
    
    return LittleFS.remove(source);
}



// Helper function to copy a directory
bool copyDir(const String& sourceDir, const String& destDir) {
    File root = LittleFS.open(sourceDir);
    if (!root) {
        return false;
    }
    
    if (!root.isDirectory()) {
        return false;
    }
    
    File file = root.openNextFile();
    
    while (file) {
        if (file.isDirectory()) {
            String dirName = String(file.name()).substring(String(file.name()).lastIndexOf('/') + 1);
            String newSourceDir = sourceDir + "/" + dirName;
            String newDestDir = destDir + "/" + dirName;
            LittleFS.mkdir(newDestDir);
            copyDir(newSourceDir, newDestDir);
        } else {
            String fileName = String(file.name()).substring(String(file.name()).lastIndexOf('/') + 1);
            copyFile(String(file.path()), destDir + "/" + fileName);
        }
        file = root.openNextFile();
    }
    
    return true;
}

// Helper function to delete a directory recursively
bool deleteDir(const String& dirPath) {
    File root = LittleFS.open(dirPath);
    if (!root) {
        return false;
    }
    
    if (!root.isDirectory()) {
        return false;
    }
    
    File file = root.openNextFile();
    
    while (file) {
        if (file.isDirectory()) {
            String path = String(file.path());
            file = root.openNextFile(); // Get next file before deleting directory
            deleteDir(path);
        } else {
            String path = String(file.path());
            file = root.openNextFile(); // Get next file before deleting file
            LittleFS.remove(path);
        }
    }
    
    return LittleFS.rmdir(dirPath);
}

String getCurrentUiVersion() {
    if (LittleFS.exists("/ui-version.txt")) {
        File versionFile = LittleFS.open("/ui-version.txt", "r");
        if (versionFile) {
            String version = versionFile.readString();
            versionFile.close();
            // Fix the trim issue
            version.trim();
            return version;
        }
    }
    
    // Default version if file doesn't exist - match SOFTWARE_VERSION
    return String(SOFTWARE_VERSION);
}

String getCurrentFirmwareVersion() {
    // First, check if version file exists
    if (LittleFS.exists("/firmware-version.txt")) {
        File versionFile = LittleFS.open("/firmware-version.txt", "r");
        if (versionFile) {
            String version = versionFile.readString();
            versionFile.close();
            // Fix the trim issue
            version.trim();
            return version;
        }
    }
    
    // Default to the SOFTWARE_VERSION if file doesn't exist
    // Return the numeric part before any space
    return String(SOFTWARE_VERSION);
}

void handleUiUpload() {
    HTTPUpload& upload = server.upload();
    static String uploadPath;
    static bool isTgzFile = false;
    static String extractedVersion = "";
    
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        Serial.printf("UI Upload: %s\n", filename.c_str());
        debug(String(F("UI Upload gestartet: ")) + filename);
        
        // Check if it's a TGZ file
        isTgzFile = filename.endsWith(".tgz") || filename.endsWith(".tar.gz");
        if (!isTgzFile) {
            debug(F("Fehler: Keine TGZ-Datei"));
            return;
        }
        
        // Extract version
        if (filename.length() > 3) {
            String versionStr = filename.substring(3);
            int dashPos = versionStr.indexOf("-");
            if (dashPos > 0) {
                extractedVersion = versionStr.substring(0, dashPos);
                debug(String(F("UI-Version aus Dateiname: ")) + extractedVersion);
            }
        }

        // Create all required directories first
        if (!LittleFS.exists("/temp")) {
            debug(F("Erstelle Verzeichnis /temp"));
            if (!LittleFS.mkdir("/temp")) {
                debug(F("Fehler beim Erstellen des /temp Verzeichnisses"));
                return;
            }
        }
        
        if (!LittleFS.exists("/extract")) {
            debug(F("Erstelle Verzeichnis /extract"));
            if (!LittleFS.mkdir("/extract")) {
                debug(F("Fehler beim Erstellen des /extract Verzeichnisses"));
                return;
            }
        }
        
        // Create required subdirectories
        if (!LittleFS.exists("/extract/css")) {
            LittleFS.mkdir("/extract/css");
        }
        
        if (!LittleFS.exists("/extract/js")) {
            LittleFS.mkdir("/extract/js");
        }
        
        // Clean temp directory
        debug(F("Lösche alten Inhalt in /temp"));
        File tempRoot = LittleFS.open("/temp");
        if (tempRoot && tempRoot.isDirectory()) {
            File tempFile = tempRoot.openNextFile();
            while (tempFile) {
                String path = String(tempFile.path());
                tempFile = tempRoot.openNextFile();
                LittleFS.remove(path);
            }
        }
        
        // Clean extract directory
        debug(F("Lösche alten Inhalt in /extract"));
        File extractRoot = LittleFS.open("/extract");
        if (extractRoot && extractRoot.isDirectory()) {
            File extractFile = extractRoot.openNextFile();
            while (extractFile) {
                String path = String(extractFile.path());
                extractFile = extractRoot.openNextFile();
                LittleFS.remove(path);
            }
        }
        
        uploadPath = "/temp/" + filename;
        debug(String(F("Upload-Pfad: ")) + uploadPath);
        
        File tgzFile = LittleFS.open(uploadPath, "w");
        if (!tgzFile) {
            debug(F("Fehler beim Erstellen der TGZ-Datei"));
            return;
        }
        tgzFile.close();
    }
    else if (upload.status == UPLOAD_FILE_WRITE && isTgzFile) {
        if (uploadPath.length() > 0) {
            File tgzFile = LittleFS.open(uploadPath, "a");
            if (tgzFile) {
                size_t bytesWritten = tgzFile.write(upload.buf, upload.currentSize);
                tgzFile.close();
                
                if (bytesWritten != upload.currentSize) {
                    debug(String(F("Fehler beim Schreiben: Nur ")) + bytesWritten + F(" von ") + upload.currentSize + F(" Bytes geschrieben"));
                }
            } else {
                debug(F("Fehler beim Öffnen der TGZ-Datei zum Schreiben"));
            }
        }
    }
    else if (upload.status == UPLOAD_FILE_END && isTgzFile) {
        debug(String(F("UI Upload abgeschlossen: ")) + upload.totalSize + F(" Bytes"));
        debug(F("Starte TAR-Extraktion..."));
        
        // Ensure memory heap is sufficient
        debug(String(F("Verfügbarer Heap vor Extraktion: ")) + ESP.getFreeHeap());
        
        // Initialize TarGzUnpacker with proper memory management
        TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();
        
        // Configure unpacker with more verbose logging
        TARGZUnpacker->setTarVerify(false);  // Skip verification to save memory
        TARGZUnpacker->setupFSCallbacks(targzTotalBytesFn, targzFreeBytesFn);
        TARGZUnpacker->setGzProgressCallback(BaseUnpacker::defaultProgressCallback);
        TARGZUnpacker->setLoggerCallback(BaseUnpacker::targzPrintLoggerCallback);
        TARGZUnpacker->setTarProgressCallback(BaseUnpacker::defaultProgressCallback);
        TARGZUnpacker->setTarStatusProgressCallback(BaseUnpacker::defaultTarStatusProgressCallback);
        TARGZUnpacker->setTarMessageCallback(BaseUnpacker::targzPrintLoggerCallback);
        
        // Try the extraction with proper error reporting
        debug(String(F("Entpacke ")) + uploadPath + F(" nach /extract"));
        bool success = false;
        
        try {
            // Direct extraction to avoid temporary files
            success = TARGZUnpacker->tarGzExpanderNoTempFile(tarGzFS, uploadPath.c_str(), tarGzFS, "/extract");
        } catch (const std::exception& e) {
            debug(String(F("Ausnahme bei Extraktion: ")) + e.what());
        } catch (...) {
            debug(F("Unbekannte Ausnahme bei Extraktion"));
        }
        
        debug(String(F("Verfügbarer Heap nach Extraktion: ")) + ESP.getFreeHeap());
        
        if (success) {
            debug(F("TAR-Extraktion erfolgreich. Überprüfe Dateien..."));
            
            // Manual file installation instead of checking for files
            // Create backup directory if needed
            if (!LittleFS.exists("/backup")) {
                debug(F("Erstelle Backup-Verzeichnis"));
                LittleFS.mkdir("/backup");
            }
            
            // Backup existing files
            debug(F("Erstelle Backup der aktuellen Dateien"));
            if (LittleFS.exists("/index.html")) 
                copyFile("/index.html", "/backup/index.html");
            if (LittleFS.exists("/setup.html")) 
                copyFile("/setup.html", "/backup/setup.html");
            if (LittleFS.exists("/diagnostics.html")) 
                copyFile("/diagnostics.html", "/backup/diagnostics.html");
            if (LittleFS.exists("/mood.html")) 
                copyFile("/mood.html", "/backup/mood.html");
            
            // Ensure CSS and JS directories exist
            if (!LittleFS.exists("/css")) 
                LittleFS.mkdir("/css");
            if (!LittleFS.exists("/js")) 
                LittleFS.mkdir("/js");
            
            // Copy main HTML files
            debug(F("Kopiere Hauptdateien"));
            copyFile("/extract/index.html", "/index.html");
            copyFile("/extract/setup.html", "/setup.html");
            copyFile("/extract/diagnostics.html", "/diagnostics.html");
            copyFile("/extract/mood.html", "/mood.html");
            
            // Copy CSS files
            debug(F("Kopiere CSS-Dateien"));
            if (LittleFS.exists("/extract/css/style.css"))
                copyFile("/extract/css/style.css", "/css/style.css");
            if (LittleFS.exists("/extract/css/mood.css"))
                copyFile("/extract/css/mood.css", "/css/mood.css");
            
            // Copy JS files
            debug(F("Kopiere JS-Dateien"));
            if (LittleFS.exists("/extract/js/script.js"))
                copyFile("/extract/js/script.js", "/js/script.js");
            if (LittleFS.exists("/extract/js/mood.js"))
                copyFile("/extract/js/mood.js", "/js/mood.js");
            if (LittleFS.exists("/extract/js/setup.js"))
                copyFile("/extract/js/setup.js", "/js/setup.js");
            
            // Save version
            if (extractedVersion.length() > 0) {
                debug(String(F("Speichere UI-Version: ")) + extractedVersion);
                File versionFile = LittleFS.open("/ui-version.txt", "w");
                if (versionFile) {
                    versionFile.print(extractedVersion);
                    versionFile.close();
                }
            }
            
            debug(F("UI-Update erfolgreich!"));
        } else {
            int errorCode = TARGZUnpacker->tarGzGetError();
            debug(String(F("Extraktion fehlgeschlagen mit Fehler: ")) + errorCode);
            
            switch (errorCode) {
                case -1: debug(F("Fehler: Allgemeiner TAR-Lesefehler")); break;
                case -2: debug(F("Fehler: Nicht genügend Speicher für TAR-Extraktion")); break;
                case -3: debug(F("Fehler: TAR-Header fehlerhaft")); break;
                case -4: debug(F("Fehler: TAR-Datei fehlerhaft")); break;
                case -5: debug(F("Fehler: Fehler beim Schreiben der extrahierten Dateien")); break;
                default: debug(String(F("Fehler: Unbekannter Fehlercode ")) + errorCode); break;
            }
        }
        
        // Clean up
        debug(F("Aufräumen nach UI-Update"));
        LittleFS.remove(uploadPath);
        
        delete TARGZUnpacker;
    }
}

void handleApiStatus() {
    JsonDocument doc;

    doc["wifi"] = WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected";
    doc["mqtt"] = appState.mqttEnabled && mqtt.isConnected() ? "Connected" : (appState.mqttEnabled ? "Disconnected" : "Disabled");

    // System Info
    unsigned long uptime = millis() / 1000;
    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int minutes = (uptime % 3600) / 60;
    int seconds = uptime % 60;
    char uptimeStr[50];
    sprintf(uptimeStr, "%dd %dh %dm %ds", days, hours, minutes, seconds);
    doc["uptime"] = uptimeStr;

    doc["rssi"] = WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm" : "N/A";
    doc["heap"] = String(ESP.getFreeHeap() / 1024) + " KB";
    doc["sentiment"] = String(appState.sentimentScore, 2) + " (" + appState.sentimentCategory + ")";
    doc["dhtEnabled"] = appState.dhtEnabled;
    doc["dht"] = isnan(appState.currentTemp) ? "N/A" : String(appState.currentTemp, 1) + "°C / " + String(appState.currentHum, 1) + "%";
    doc["mode"] = appState.autoMode ? "Auto" : "Manual";
    doc["lightOn"] = appState.lightOn;
    doc["brightness"] = appState.manualBrightness;
    // v9.0: headlines removed
    doc["version"] = SOFTWARE_VERSION;

    // LED-Farbe als Hex
    uint32_t currentColor;
    if (appState.autoMode)
    {
        appState.currentLedIndex = constrain(appState.currentLedIndex, 0, 4);
        ColorDefinition color = getColorDefinition(appState.currentLedIndex);
        currentColor = pixels.Color(color.r, color.g, color.b);
    }
    else
    {
        currentColor = appState.manualColor;
    }

    uint8_t r = (currentColor >> 16) & 0xFF;
    uint8_t g = (currentColor >> 8) & 0xFF;
    uint8_t b = currentColor & 0xFF;
    char hexColor[8];
    sprintf(hexColor, "#%02X%02X%02X", r, g, b);
    doc["ledColor"] = hexColor;

    // Status-LED Info
    if (appState.statusLedMode != 0)
    {
        char statusLedColor[8] = "#000000";
        switch (appState.statusLedMode)
        {
        case 1:
            strcpy(statusLedColor, "#0000FF");
            break; // WiFi - Blau
        case 2:
            strcpy(statusLedColor, "#FF0000");
            break; // API - Rot
        case 3:
            strcpy(statusLedColor, "#00FF00");
            break; // Update - Grün
        case 4:
            strcpy(statusLedColor, "#00FFFF");
            break; // MQTT - Cyan
        case 5:
            strcpy(statusLedColor, "#FFFF00");
            break; // AP - Gelb
        }
        doc["statusLedMode"] = appState.statusLedMode;
        doc["statusLedColor"] = statusLedColor;
    }
    else
    {
        doc["statusLedMode"] = 0;
    }

    char* jsonBuffer = jsonPool.acquire();
    size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    server.send(200, "application/json", jsonBuffer);
    jsonPool.release(jsonBuffer);
}

// Handle deleting a specific data point - FIXED
// ===== DISABLED IN v9.0: Local CSV Management =====
// Data is managed in backend, cannot delete from device
void handleApiDeleteDataPoint() {
    server.send(410, "application/json",
        "{\"error\":\"Feature removed in v9.0 - Data is managed in backend\"}");
    debug(F("Delete datapoint disabled - backend manages data"));
}

// ===== DISABLED IN v9.0: Local CSV Management =====
void handleApiResetAllData() {
    server.send(410, "application/json",
        "{\"error\":\"Feature removed in v9.0 - Data is managed in backend\"}");
    debug(F("Reset all data disabled - backend manages data"));
}

// v9.0: Archive functions removed (getArchivesList, archiveOldData) - handled in backend

  // Speicherinformationen abrufen
void getStorageInfo(uint64_t &totalBytes, uint64_t &usedBytes, uint64_t &freeBytes) {
    if (LittleFS.begin()) {
      totalBytes = LittleFS.totalBytes();
      usedBytes = LittleFS.usedBytes();
      freeBytes = totalBytes - usedBytes;
    } else {
      totalBytes = 0;
      usedBytes = 0;
      freeBytes = 0;
    }
  }

  // API-Endpunkt für Speicherinformationen
  void handleApiStorageInfo() {
    uint64_t totalBytes, usedBytes, freeBytes;
    getStorageInfo(totalBytes, usedBytes, freeBytes);

    JsonDocument doc;
    doc["total"] = totalBytes;
    doc["used"] = usedBytes;
    doc["free"] = freeBytes;
    doc["percentUsed"] = (totalBytes > 0) ? ((float)usedBytes / totalBytes * 100) : 0;

    // v9.0: Stats and archives managed in backend, no local record count needed
    doc["recordCount"] = 0;

    char* jsonBuffer = jsonPool.acquire();
    size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    server.send(200, "application/json", jsonBuffer);
    jsonPool.release(jsonBuffer);
  }

  // v9.0: CSV/Archive functions removed (countStatsRecords, setupArchiveEndpoints) - stats managed in backend

  // ===== UPDATED IN v9.0: Stats from Backend =====
  // Stats are now fetched from backend instead of local CSV
  void handleApiStats() {
    // Get hours parameter from query string (default: 168 = 7 days)
    int hours = 168;
    if (server.hasArg("hours")) {
        hours = server.arg("hours").toInt();
    }

    // Großes JsonDocument für Backend-Daten (bis zu 48 Datenpunkte * ~150 bytes)
    JsonDocument doc;
    if (fetchBackendStatistics(doc, hours)) {
        // Forward backend response to client
        char* jsonBuffer = jsonPool.acquire();
        size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
        server.send(200, "application/json", jsonBuffer);
        jsonPool.release(jsonBuffer);
        debug(String(F("Stats from backend sent: ")) + len + F(" bytes"));
    } else {
        debug(F("Backend statistics fetch failed, sending 503"));
        server.send(503, "application/json", "{\"error\":\"Backend statistics unavailable\"}");
    }
}

// ===== REMOVED IN v9.0: RSS Feed Functions =====
// These functions are no longer needed as RSS feeds are managed in the backend
// - loadRssFeeds()
// - sendRSSConfigToAPI()
// Memory saved: ~3KB Flash

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

// ===== REMOVED IN v9.0: Local CSV Statistics Storage =====
// Statistics are now fetched from backend instead of stored locally
// saveSentimentStats() removed - data managed in backend

// ===== REMOVED IN v9.0: RSS Feed HTTP Handlers =====
// - handleApiGetFeeds()
// - handleApiSaveFeeds()
// RSS configuration is now managed entirely in the backend

// ===== NEW IN v9.0: Backend Statistics Handler =====
void handleApiBackendStats() {
    int hours = 168; // Default: 7 days

    if (server.hasArg("hours")) {
        hours = server.arg("hours").toInt();
        if (hours < 1) hours = 168;
        if (hours > 720) hours = 720; // Max 30 days
    }

    JsonDocument doc;
    if (fetchBackendStatistics(doc, hours)) {
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    } else {
        server.send(503, "application/json", "{\"error\":\"Backend statistics unavailable\"}");
    }
}

// Webserver-Setup anpassen
void setupWebServer()
{
    server.onNotFound([]() {
        String path = server.uri();
        String method = (server.method() == HTTP_GET) ? "GET" : 
                        (server.method() == HTTP_POST) ? "POST" : 
                        (server.method() == HTTP_PUT) ? "PUT" : 
                        (server.method() == HTTP_DELETE) ? "DELETE" : "OTHER";
        
        debug(String(F("404 Not Found: ")) + method + " " + path);
        
        // Print query parameters if any
        if (server.args() > 0) {
            debug(F("Query parameters:"));
            for (int i = 0; i < server.args(); i++) {
                debug(String(F("  ")) + server.argName(i) + ": " + server.arg(i));
            }
        }
        
        // Check for favicons that might be requested by browsers
        if (path.indexOf("favicon") != -1) {
            debug(F("Favicon request - serving empty response"));
            server.send(204); // No content
            return;
        }
        
        // Rest of your handler...
        // If path ends with slash, append index.html
        if (path.endsWith("/")) path += "index.html";
        
        // Try handling as static file first
        if (LittleFS.exists(path)) {
            debug(String(F("File exists, serving: ")) + path);
            handleStaticFile(path);
            return;
        }
        
        // Special case handling for common paths
        if (path == "/") {
            debug(F("Root request, serving index.html"));
            handleStaticFile("/index.html");
            return;
        }
        
        if (path == "/setup" || path == "/setup.html") {
            debug(F("Setup page requested, serving setup.html"));
            handleStaticFile("/setup.html");
            return;
        }
        
        if (path == "/diagnostics" || path == "/diagnostics.html") {
            debug(F("D  iagnostics page requested, serving diagnostics.html"));
            handleStaticFile("/diagnostics.html");
            return;
        }

        if (path == "/mood" || path == "/mood.html") {
            debug(F("Mood page requested, serving mood.html"));
            handleStaticFile("/mood.html");
            return;
        }
        
        // If we got here, no handler found
        debug(String(F("No handler for: ")) + path + F(", sending 404"));
        server.send(404, "text/plain", "404: Not Found - " + path);
    });
    // Statische Dateien aus LittleFS
    server.on("/", HTTP_GET, []()
              { handleStaticFile("/index.html"); });

    server.on("/setup", HTTP_GET, []()
              { handleStaticFile("/setup.html"); });

    server.on("/mood", HTTP_GET, []()
              { handleStaticFile("/mood.html"); });

    server.on("/diagnostics", HTTP_GET, []()
              { handleStaticFile("/diagnostics.html"); });

    // System diagnostics endpoints
    server.on("/api/system/metrics", HTTP_GET, []() {
        JsonDocument doc;

        doc["heap"] = ESP.getFreeHeap();
        doc["maxBlock"] = ESP.getMaxAllocHeap();
        doc["minHeap"] = memMonitor.getLowestHeap();

        uint64_t total, used, free;
        getStorageInfo(total, used, free);
        doc["fsTotal"] = (unsigned long)total;
        doc["fsUsed"] = (unsigned long)used;
        doc["fsFree"] = (unsigned long)(total - used);
        doc["fsPercent"] = (float)used * 100.0 / total;

        doc["uptime"] = millis() / 1000;

        doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
        if (WiFi.status() == WL_CONNECTED) {
            doc["rssi"] = WiFi.RSSI();
            doc["ssid"] = WiFi.SSID();
            doc["channel"] = WiFi.channel();
            doc["ip"] = WiFi.localIP().toString();
        }

        doc["mqttEnabled"] = appState.mqttEnabled;
        doc["mqttConnected"] = appState.mqttEnabled && mqtt.isConnected();
        doc["temperature"] = temperatureRead();
        doc["sentiment"] = appState.sentimentScore;
        doc["sentimentCategory"] = appState.sentimentCategory;

        bool memoryOk = ESP.getFreeHeap() > 30000;
        bool fragmentationOk = (float)ESP.getMaxAllocHeap() / ESP.getFreeHeap() > 0.7;
        bool filesystemOk = ((float)used * 100.0 / total) < 80.0;
        bool wifiOk = WiFi.status() == WL_CONNECTED && WiFi.RSSI() > -80;
        bool mqttOk = !appState.mqttEnabled || (appState.mqttEnabled && mqtt.isConnected());

        doc["status"]["memory"] = memoryOk ? "ok" : "warning";
        doc["status"]["fragmentation"] = fragmentationOk ? "ok" : "warning";
        doc["status"]["filesystem"] = filesystemOk ? "ok" : "warning";
        doc["status"]["wifi"] = wifiOk ? "ok" : "warning";
        doc["status"]["mqtt"] = mqttOk ? "ok" : "warning";
        doc["status"]["overall"] = (memoryOk && fragmentationOk && filesystemOk && wifiOk && mqttOk) ? "ok" : "warning";

        char* jsonBuffer = jsonPool.acquire();
        size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
        server.send(200, "application/json", jsonBuffer);
        jsonPool.release(jsonBuffer);
    });

    server.on("/api/system/diagnose", HTTP_GET, []() {
        debug(F("Vollständige Systemdiagnose angefordert"));
        memMonitor.diagnose();
        netDiag.fullAnalysis();
        sysHealth.performFullCheck();
        fileOps.listDir("/", 1);

        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = F("Diagnose abgeschlossen");

        char* jsonBuffer = jsonPool.acquire();
        size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
        server.send(200, "application/json", jsonBuffer);
        jsonPool.release(jsonBuffer);
    });

    server.on("/api/system/cleanup", HTTP_GET, []() {
        debug(F("Dateisystem-Bereinigung angefordert"));
        int cleanedFiles = 0;

        if (LittleFS.exists("/temp")) {
            File root = LittleFS.open("/temp");
            if (root && root.isDirectory()) {
                File file = root.openNextFile();
                while (file) {
                    String filePath = file.path();
                    file.close();
                    if (LittleFS.remove(filePath)) {
                        cleanedFiles++;
                        debug(String(F("Gelöscht: ")) + filePath);
                    }
                    file = root.openNextFile();
                }
            }
        }

        if (LittleFS.exists("/data")) {
            File dataDir = LittleFS.open("/data");
            if (dataDir && dataDir.isDirectory()) {
                File file = dataDir.openNextFile();
                while (file) {
                    String filePath = file.path();
                    file.close();
                    if (filePath.endsWith(".tmp") || filePath.endsWith(".bak")) {
                        if (LittleFS.remove(filePath)) {
                            cleanedFiles++;
                            debug(String(F("Gelöscht: ")) + filePath);
                        }
                    }
                    file = dataDir.openNextFile();
                }
            }
        }

        JsonDocument doc;
        doc["success"] = true;
        doc["filesRemoved"] = cleanedFiles;
        doc["message"] = String(cleanedFiles) + F(" Dateien bereinigt");

        uint64_t total, used, free;
        getStorageInfo(total, used, free);
        doc["freeSpace"] = (unsigned long)free;
        doc["freeSpacePercent"] = (float)free * 100.0 / total;

        char* jsonBuffer = jsonPool.acquire();
        size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
        server.send(200, "application/json", jsonBuffer);
        jsonPool.release(jsonBuffer);
    });

    // API-Endpunkte für dynamische Daten
    server.on("/api/status", HTTP_GET, handleApiStatus);
    server.on("/api/stats", HTTP_GET, handleApiStats);
    server.on("/api/backend/stats", HTTP_GET, handleApiBackendStats); // NEW v9.0: Backend stats
    // REMOVED v9.0: RSS feeds now managed in backend
    // server.on("/api/feeds", HTTP_GET, handleApiGetFeeds);
    // server.on("/api/feeds", HTTP_POST, handleApiSaveFeeds);

    server.on("/api/storage", HTTP_GET, handleApiStorageInfo);
    // v9.0: Archive endpoints disabled - data managed in backend
    // setupArchiveEndpoints();

    // Additional common files
server.on("/favicon.ico", HTTP_GET, []() {
    if (LittleFS.exists("/favicon.ico")) {
        File file = LittleFS.open("/favicon.ico", "r");
        server.streamFile(file, "image/x-icon");
        file.close();
    } else {
        server.send(204); // No content
    }
});

server.on("/api/repair/stats", HTTP_GET, []() {
    // v9.0: CSV repair removed - stats managed in backend
    JsonDocument doc;
    doc["success"] = false;
    doc["message"] = F("CSV-Reparatur entfernt in v9.0 - Statistiken werden vom Backend verwaltet");
    
    char* jsonBuffer = jsonPool.acquire();
    size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    server.send(200, "application/json", jsonBuffer);
    jsonPool.release(jsonBuffer);
});

// For CSS and JS files that might be missing
server.on("/css/style.css", HTTP_GET, []() { handleStaticFile("/css/style.css"); });
server.on("/css/mood.css", HTTP_GET, []() { handleStaticFile("/css/mood.css"); });
server.on("/js/script.js", HTTP_GET, []() { handleStaticFile("/js/script.js"); });
server.on("/js/mood.js", HTTP_GET, []() { handleStaticFile("/js/mood.js"); });
server.on("/js/setup.js", HTTP_GET, []() { handleStaticFile("/js/setup.js"); });
    server.on("/api/stats/delete", HTTP_GET, handleApiDeleteDataPoint);
server.on("/api/stats/reset", HTTP_GET, handleApiResetAllData);

server.on("/ui-upload", HTTP_POST,
    []() {
        server.send(200, "text/html", "<html><body><h1>UI Update Complete</h1><a href='/setup'>Return to Setup</a></body></html>");
    },
    handleUiUpload
);

server.on("/api/ui-version", HTTP_GET, []() {
    JsonDocument doc;
    doc["version"] = getCurrentUiVersion();
    char* jsonBuffer = jsonPool.acquire();
    size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    server.send(200, "application/json", jsonBuffer);
    jsonPool.release(jsonBuffer);
});
    server.on("/api/settings/hardware", HTTP_GET, []() {
        JsonDocument doc;
        
        doc["ledPin"] = appState.ledPin;
        doc["dhtPin"] = appState.dhtPin;
        doc["numLeds"] = appState.numLeds;
        
        char* jsonBuffer = jsonPool.acquire();
        size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
        server.send(200, "application/json", jsonBuffer);
        jsonPool.release(jsonBuffer);
      });

      // Im Abschnitt setupWebServer() folgende Endpunkte hinzufügen:

// v9.0: CSV export removed - stats managed in backend

// Einstellungen herunterladen
server.on("/api/export/settings", HTTP_GET, []() {
    JsonDocument doc;
    
    // Allgemeine Einstellungen
    doc["moodInterval"] = appState.moodUpdateInterval / 1000;
    doc["dhtInterval"] = appState.dhtUpdateInterval / 1000;
    doc["autoMode"] = appState.autoMode;
    doc["lightOn"] = appState.lightOn;
    doc["manBright"] = appState.manualBrightness;
    doc["manColor"] = appState.manualColor;
    // v9.0: headlinesPS removed
    
    // WiFi-Einstellungen
    doc["wifiSSID"] = appState.wifiSSID;
    doc["wifiPass"] = "****";
    doc["wifiConfigured"] = appState.wifiConfigured;

    // Erweiterte Einstellungen
    doc["apiUrl"] = appState.apiUrl;
    doc["mqttServer"] = appState.mqttServer;
    doc["mqttUser"] = appState.mqttUser;
    doc["mqttPass"] = "****";
    doc["dhtPin"] = appState.dhtPin;
    doc["dhtEnabled"] = appState.dhtEnabled;
    doc["ledPin"] = appState.ledPin;
    doc["numLeds"] = appState.numLeds;
    doc["mqttEnabled"] = appState.mqttEnabled;

    // Benutzerdefinierte Farben
    for (int i = 0; i < 5; i++) {
        doc["color" + String(i)] = appState.customColors[i];
    }

    String jsonContent;
    serializeJson(doc, jsonContent);

    server.sendHeader("Content-Disposition", "attachment; filename=moodlight_settings.json");
    server.send(200, "application/json", jsonContent);
    debug(F("Einstellungen wurden exportiert"));
});

    
    // Neue API-Endpunkte für Einstellungen
server.on("/api/settings/api", HTTP_GET, []() {
    JsonDocument doc;
    
    doc["apiUrl"] = appState.apiUrl;
    doc["moodInterval"] = appState.moodUpdateInterval / 1000;
    doc["dhtInterval"] = appState.dhtUpdateInterval / 1000;
    // v9.0: headlinesPerSource removed
    doc["dhtEnabled"] = appState.dhtEnabled;
    
    char* jsonBuffer = jsonPool.acquire();
    size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    server.send(200, "application/json", jsonBuffer);
    jsonPool.release(jsonBuffer);
  });
  
  server.on("/api/settings/mqtt", HTTP_GET, []() {
    JsonDocument doc;
    
    doc["enabled"] = appState.mqttEnabled;
    doc["server"] = appState.mqttServer;
    doc["user"] = appState.mqttUser;
    doc["pass"] = "****";
    
    char* jsonBuffer = jsonPool.acquire();
    size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    server.send(200, "application/json", jsonBuffer);
    jsonPool.release(jsonBuffer);
  });

  server.on("/api/settings/colors", HTTP_GET, []() {
    JsonDocument doc;
    
    JsonArray colors = doc["colors"].to<JsonArray>();
  
    for (int i = 0; i < 5; i++) {
      // uint32 Farbe in Hex-String konvertieren
      uint32_t color = appState.customColors[i];
      char hexColor[8];
      sprintf(hexColor, "#%06X", color);
      colors.add(hexColor);
    }
    
    doc["colorNames"] = JsonArray();
    for (int i = 0; i < 5; i++) {
        if (i < 5 && colorNames[i]) {
            doc["colorNames"][i] = colorNames[i];
        }
    }
    
    char* jsonBuffer = jsonPool.acquire();
    size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    server.send(200, "application/json", jsonBuffer);
    jsonPool.release(jsonBuffer);
  });
  
  server.on("/api/system/info", HTTP_GET, []() {
    JsonDocument doc;
    
    doc["version"] = getCurrentUiVersion();
    doc["firmwareVersion"] = getCurrentFirmwareVersion(); 
    doc["chip"] = ESP.getChipModel();
    
    // MAC-Adresse formatieren
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    doc["mac"] = macStr;
    
    char* jsonBuffer = jsonPool.acquire();
    size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    server.send(200, "application/json", jsonBuffer);
    jsonPool.release(jsonBuffer);
  });

    // WiFi Scan Endpunkt
    server.on("/wifiscan", HTTP_GET, []()
              {
    String jsonResult = scanWiFiNetworks();
    server.send(200, "application/json", jsonResult); });

    // WiFi Einstellungen speichern
    server.on("/savewifi", HTTP_POST, []()
              {
    String jsonStr = server.arg("plain");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
      debug(String(F("JSON Parsing Fehler: ")) + error.c_str());
      server.send(400, "text/plain", "JSON Parsing Fehler");
      return;
    }

    // Werte aus JSON extrahieren
    appState.wifiSSID = doc["ssid"].as<String>();
    appState.wifiPassword = doc["pass"].as<String>();
    appState.wifiConfigured = true;

    // Einstellungen speichern
    appState.settingsNeedSaving = true;
    appState.lastSettingsSaved = millis();

    // Reboot planen
    appState.rebootNeeded = true;
    appState.rebootTime = millis() + REBOOT_DELAY;

    server.send(200, "text/plain", "OK");
    debug(F("Neue WiFi-Einstellungen gespeichert, Reboot geplant")); });

    // WiFi zurücksetzen
    server.on("/resetwifi", HTTP_POST, []()
              {
    appState.wifiSSID = "";
    appState.wifiPassword = "";
    appState.wifiConfigured = false;

    // Einstellungen speichern
    appState.settingsNeedSaving = true;
    appState.lastSettingsSaved = millis();

    // Reboot planen
    appState.rebootNeeded = true;
    appState.rebootTime = millis() + REBOOT_DELAY;

    server.send(200, "text/plain", "OK");
    debug(F("WiFi-Einstellungen zurückgesetzt, Reboot geplant")); });

    // MQTT Einstellungen speichern
    server.on("/savemqtt", HTTP_POST, []()
              {
    String jsonStr = server.arg("plain");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
      debug(String(F("JSON Parsing Fehler: ")) + error.c_str());
      server.send(400, "text/plain", "JSON Parsing Fehler");
      return;
    }

    // Werte aus JSON extrahieren
    appState.mqttEnabled = doc["enabled"].as<bool>();
    appState.mqttServer = doc["server"].as<String>();
    appState.mqttUser = doc["user"].as<String>();
    appState.mqttPassword = doc["pass"].as<String>();

    // Einstellungen speichern
    appState.settingsNeedSaving = true;
    appState.lastSettingsSaved = millis();

    // Reboot planen
    appState.rebootNeeded = true;
    appState.rebootTime = millis() + REBOOT_DELAY;

    server.send(200, "text/plain", "OK");
    debug(F("MQTT-Einstellungen gespeichert, Reboot geplant")); });

    // API Einstellungen speichern
    // API und Intervall Einstellungen speichern (ohne Neustart)
    server.on("/saveapi", HTTP_POST, []()
              {
    String jsonStr = server.arg("plain");
    JsonDocument doc;  // Ggf. etwas größer für mehr Keys
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
      debug(String(F("JSON Parsing Fehler: ")) + error.c_str());
      server.send(400, "text/plain", "JSON Parsing Fehler");
      return;
    }

    bool changed = false;  // Flag, um zu prüfen, ob sich *irgendetwas* geändert hat

    // Werte aus JSON extrahieren und globale Variablen aktualisieren
    if (doc["apiUrl"].is<const char*>()) {
      String newApiUrl = doc["apiUrl"].as<String>();
      if (newApiUrl != appState.apiUrl) {
        appState.apiUrl = newApiUrl;
        appState.lastMoodUpdate = 0;  // Erzwinge Sentiment-Update bei nächster Gelegenheit
        changed = true;
        debug(String(F("API URL geändert zu: ")) + appState.apiUrl);
      }
    }
    if (doc["moodInterval"].is<float>()) {
      unsigned long newMoodInterval = 1000UL * doc["moodInterval"].as<unsigned long>();
      newMoodInterval = constrain(newMoodInterval, 10000, 7200000);  // Mind. 10s, Max 2h
      if (newMoodInterval != appState.moodUpdateInterval) {
        appState.moodUpdateInterval = newMoodInterval;
        changed = true;
        debug(String(F("Mood Interval geändert zu: ")) + String(appState.moodUpdateInterval / 1000) + F("s"));
      }
    }
    if (doc["dhtEnabled"].is<float>()) {
      bool newDhtEnabled = doc["dhtEnabled"].as<bool>();
      if (newDhtEnabled != appState.dhtEnabled) {
        appState.dhtEnabled = newDhtEnabled;
        changed = true;
        debug(String(F("DHT Enabled geändert zu: ")) + (appState.dhtEnabled ? "ja" : "nein"));
      }
    }
    // v9.0: headlinesPerSource removed - only for legacy API endpoints

    // NEU: DHT Intervall hier verarbeiten
    if (doc["dhtInterval"].is<float>()) {
      unsigned long newDhtInterval = 1000UL * doc["dhtInterval"].as<unsigned long>();
      newDhtInterval = constrain(newDhtInterval, 10000, 3600000);  // Mind. 10s, Max 1h
      if (newDhtInterval != appState.dhtUpdateInterval) {
        appState.dhtUpdateInterval = newDhtInterval;
        changed = true;
        debug(String(F("DHT Interval geändert zu: ")) + String(appState.dhtUpdateInterval / 1000) + F("s"));
      }
    }

    // Nur speichern und HA updaten, wenn sich tatsächlich etwas geändert hat
    if (changed) {
      debug(F("API/Intervall-Einstellungen geändert. Speichere und aktualisiere HA..."));
      // Einstellungen speichern
      appState.settingsNeedSaving = true;
      appState.lastSettingsSaved = millis();

      // HA Entitäten aktualisieren, falls MQTT verbunden
      if (appState.mqttEnabled && mqtt.isConnected()) {
        // API Update Interval
        haUpdateInterval.setState(float(appState.moodUpdateInterval / 1000.0));
        debug(String(F("  HA: haUpdateInterval auf ")) + String(appState.moodUpdateInterval / 1000.0) + F("s gesetzt."));

        // v9.0: haHeadlinesPerSource removed

        // DHT Update Interval
        haDhtInterval.setState(float(appState.dhtUpdateInterval / 1000.0));
        debug(String(F("  HA: haDhtInterval auf ")) + String(appState.dhtUpdateInterval / 1000.0) + F("s gesetzt."));
      } else {
        debug(F("  HA: MQTT nicht verbunden, Zustände nicht gesendet."));
      }
      server.send(200, "text/plain", "OK");
      debug(F("API/Intervall-Einstellungen erfolgreich gespeichert und HA aktualisiert (falls verbunden)."));
    } else {
      debug(F("API/Intervall-Einstellungen: Keine Änderungen erkannt."));
      server.send(200, "text/plain", "Keine Änderungen");  // Senden, dass nichts geändert wurde
    } });

    // Farben speichern
    server.on("/savecolors", HTTP_POST, []()
              {
    String jsonStr = server.arg("plain");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
      debug(String(F("JSON Parsing Fehler: ")) + error.c_str());
      server.send(400, "text/plain", "JSON Parsing Fehler");
      return;
    }

    // Farben aus JSON extrahieren
    if (doc["colors"].is<JsonArray>()) {
        JsonArray colorArray = doc["colors"].as<JsonArray>();
        int index = 0;

      for (JsonVariant colorValue : colorArray) {
        if (index < 5 && colorValue.is<String>()) {
            String hexColor = colorValue.as<String>();
            uint32_t rgb = 0;
            sscanf(hexColor.c_str(), "%x", &rgb);
            appState.customColors[index] = rgb;
            index++;
        }
    }

      // Einstellungen speichern
      appState.settingsNeedSaving = true;
      appState.lastSettingsSaved = millis();

      server.send(200, "text/plain", "OK");
      debug(F("Farbeinstellungen gespeichert."));
    } else {
      server.send(400, "text/plain", "Ungültiges Farbformat");
    } });

    // API testen
// API testen
server.on("/testapi", HTTP_POST, []() {
    String jsonStr = server.arg("plain");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
      debug(String(F("JSON Parsing Fehler: ")) + error.c_str());
      server.send(400, "application/json", "{\"success\":false,\"message\":\"JSON Parsing Fehler\"}");
      return;
    }

    // Werte aus JSON extrahieren
    String testApiUrl = doc["apiUrl"].as<String>();
    // v9.0: headlinesPerSource removed - not needed for new API endpoints

    // API testen
    HTTPClient http;
    http.setReuse(false);
    http.setUserAgent("MoodlightClient/1.0");

    debug(String(F("Teste API URL: ")) + testApiUrl);
    // v9.0: headlines_per_source parameter removed - not used by new /api/moodlight/* endpoints

    if (http.begin(wifiClientHTTP, testApiUrl)) {
      http.setTimeout(10000);
      int httpCode = http.GET();

      if (httpCode == HTTP_CODE_OK) {
        WiFiClient* stream = http.getStreamPtr();
        JsonDocument testDoc;
        DeserializationError testError = deserializeJson(testDoc, *stream);

        if (!testError) {
            if (testDoc["sentiment"].is<float>()) {
                float sentimentValue = testDoc["sentiment"].as<float>();
                debug(String(F("API Test erfolgreich! Sentiment: ")) + String(sentimentValue, 2));

                JsonDocument resultDoc;
                resultDoc["success"] = true;
                resultDoc["sentiment"] = sentimentValue;

                String resultJson;
                serializeJson(resultDoc, resultJson);
                http.end();
                server.send(200, "application/json", resultJson);
                return;
            } else {
                debug(F("API Test: JSON enthält keinen gültigen 'sentiment' Wert"));
                http.end();
                server.send(200, "application/json", "{\"success\":false,\"message\":\"JSON enthält keinen gültigen 'sentiment' Wert\"}");
                return;
            }
        } else {
            debug(String(F("API Test: JSON Parsing Fehler: ")) + testError.c_str());
            http.end();
            server.send(200, "application/json", "{\"success\":false,\"message\":\"JSON Parsing Fehler: " + String(testError.c_str()) + "\"}");
            return;
        }
      } else {
        debug(String(F("API Test: HTTP Fehler: ")) + String(httpCode));
        http.end();
        server.send(200, "application/json", "{\"success\":false,\"message\":\"HTTP Fehler: " + String(httpCode) + "\"}");
        return;
      }
      http.end();
    } else {
      debug(String(F("API Test: Verbindungsfehler zu: ")) + testApiUrl);
      server.send(200, "application/json", "{\"success\":false,\"message\":\"Verbindungsfehler zur API\"}");
      return;
    }
});

    // Hardware Einstellungen speichern (nur Pin/LEDs, erfordert Neustart)
    server.on("/savehardware", HTTP_POST, []()
              {
    String jsonStr = server.arg("plain");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
      debug(String(F("JSON Parsing Fehler: ")) + error.c_str());
      server.send(400, "text/plain", "JSON Parsing Fehler");
      return;
    }

    bool needsReboot = false;

    // Werte aus JSON extrahieren und prüfen, ob Änderung vorliegt
    if (doc["ledPin"].is<int>() && doc["ledPin"].as<int>() != appState.ledPin) {
        appState.ledPin = doc["ledPin"].as<int>();
        needsReboot = true;  // Pin-Änderung erfordert Neustart
    }
    if (doc["dhtPin"].is<int>() && doc["dhtPin"].as<int>() != appState.dhtPin) {
        appState.dhtPin = doc["dhtPin"].as<int>();
        needsReboot = true;  // Pin-Änderung erfordert Neustart
    }

    if (doc["numLeds"].is<int>() && doc["numLeds"].as<int>() != appState.numLeds) {
        appState.numLeds = doc["numLeds"].as<int>();
        needsReboot = true;  // LED-Anzahl-Änderung erfordert Neustart
    }

    // DHT Intervall wird hier NICHT mehr verarbeitet

    if (needsReboot) {
      // Einstellungen speichern (nur wenn relevant)
      appState.settingsNeedSaving = true;
      appState.lastSettingsSaved = millis();  // Speichert die geänderten Pins/LEDs

      // Reboot planen (ist für Pin/LED-Änderungen notwendig)
      appState.rebootNeeded = true;
      appState.rebootTime = millis() + REBOOT_DELAY;

      server.send(200, "text/plain", "OK");
      debug(F("Hardware Pin/LED-Einstellungen gespeichert, Reboot geplant"));
    } else {
      debug(F("Hardware Pin/LED-Einstellungen: Keine Änderungen erkannt."));
      server.send(200, "text/plain", "Keine Änderungen");
    } });

    // Factory Reset - Alle Einstellungen zurücksetzen
    server.on("/factoryreset", HTTP_POST, []()
              {
    debug(F("Factory Reset angefordert"));

    // Preferences komplett löschen
    preferences.begin("moodlight", false);
    preferences.clear();
    preferences.end();

    // Standardwerte wiederherstellen
    appState.wifiSSID = "";
    appState.wifiPassword = "";
    appState.wifiConfigured = false;
    appState.mqttEnabled = false;
    appState.mqttServer = "";
    appState.mqttUser = "";
    appState.mqttPassword = "";
    appState.apiUrl = DEFAULT_NEWS_API_URL;
    appState.moodUpdateInterval = DEFAULT_MOOD_UPDATE_INTERVAL;
    appState.dhtUpdateInterval = DEFAULT_DHT_READ_INTERVAL;
    appState.ledPin = DEFAULT_LED_PIN;
    appState.dhtPin = DEFAULT_DHT_PIN;
    appState.numLeds = DEFAULT_NUM_LEDS;
    // v9.0: headlines_per_source removed

    // Reboot planen
    appState.rebootNeeded = true;
    appState.rebootTime = millis() + REBOOT_DELAY;

    server.send(200, "text/plain", "OK");
    debug(F("Factory Reset durchgeführt, Reboot geplant")); });

    // Log-Anzeige
    server.on("/logs", HTTP_GET, []()
              {
    String logs = "";
    for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
      int idx = (appState.logIndex + i) % LOG_BUFFER_SIZE;
      if (appState.logBuffer[idx].length() > 0) {
        logs += appState.logBuffer[idx] + "<br>";
      }
    }
    server.send(200, "text/html", logs); });

    // Status-Endpunkt für AJAX-Aktualisierungen
    server.on("/status", HTTP_GET, []()
              {
    JsonDocument doc;

    doc["wifi"] = WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected";
    doc["mqtt"] = appState.mqttEnabled && mqtt.isConnected() ? "Connected" : (appState.mqttEnabled ? "Disconnected" : "Disabled");

    unsigned long uptime = millis() / 1000;
    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int minutes = (uptime % 3600) / 60;
    int seconds = uptime % 60;
    char uptimeStr[50];
    sprintf(uptimeStr, "%dd %dh %dm %ds", days, hours, minutes, seconds);
    doc["uptime"] = uptimeStr;

    doc["rssi"] = WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm" : "N/A";
    doc["heap"] = String(ESP.getFreeHeap() / 1024) + " KB";
    doc["sentiment"] = String(appState.sentimentScore, 2) + " (" + appState.sentimentCategory + ")";
    doc["dhtEnabled"] = appState.dhtEnabled;
    doc["dht"] = isnan(appState.currentTemp) ? "N/A" : String(appState.currentTemp, 1) + "°C / " + String(appState.currentHum, 1) + "%";
    doc["mode"] = appState.autoMode ? "Auto" : "Manual";
    doc["lightOn"] = appState.lightOn;
    doc["brightness"] = appState.manualBrightness;
    // v9.0: headlines removed

    // LED-Farbe als Hex holen
    uint32_t currentColor;
    if (appState.autoMode) {
      appState.currentLedIndex = constrain(appState.currentLedIndex, 0, 4);
      ColorDefinition color = getColorDefinition(appState.currentLedIndex);
      currentColor = pixels.Color(color.r, color.g, color.b);
    } else {
      currentColor = appState.manualColor;
    }

    uint8_t r = (currentColor >> 16) & 0xFF;
    uint8_t g = (currentColor >> 8) & 0xFF;
    uint8_t b = currentColor & 0xFF;
    char hexColor[8];
    sprintf(hexColor, "#%02X%02X%02X", r, g, b);
    doc["ledColor"] = hexColor;

    // Status-LED Info
    if (appState.statusLedMode != 0) {
      char statusLedColor[8] = "#000000";
      switch (appState.statusLedMode) {
        case 1: strcpy(statusLedColor, "#0000FF"); break;  // WiFi - Blau
        case 2: strcpy(statusLedColor, "#FF0000"); break;  // API - Rot
        case 3: strcpy(statusLedColor, "#00FF00"); break;  // Update - Grün
        case 4: strcpy(statusLedColor, "#00FFFF"); break;  // MQTT - Cyan
        case 5: strcpy(statusLedColor, "#FFFF00"); break;  // AP - Gelb
      }
      doc["statusLedMode"] = appState.statusLedMode;
      doc["statusLedColor"] = statusLedColor;
    } else {
      doc["statusLedMode"] = 0;
    }

    char* jsonBuffer = jsonPool.acquire();
    size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    server.send(200, "application/json", jsonBuffer);
    jsonPool.release(jsonBuffer);
});

// Force-Refresh für Sentiment
server.on("/refresh", HTTP_GET, []() {
    debug(F("Force-Update des Sentiments gestartet..."));

    // Direkt den HTTP-Client starten
    HTTPClient http;
    bool success = false;
    float receivedSentiment = 0.0;

    http.setReuse(false);
    http.setUserAgent("MoodlightClient/1.0");

    // Erfolg melden, damit die UI nicht blockiert
    server.send(200, "text/plain", "Refresh initiated");

    appState.isPulsing = true;
    appState.pulseStartTime = millis();

    debug(F("Starte Sentiment HTTP-Abruf (Force-Update)..."));
    // v9.0: headlines_per_source parameter removed - not used by new /api/moodlight/* endpoints
    if (http.begin(wifiClientHTTP, appState.apiUrl)) {
      http.setTimeout(10000);  // Kürzeres Timeout für UI-Feedback
      int httpCode = http.GET();

      if (httpCode == HTTP_CODE_OK) {
        WiFiClient* stream = http.getStreamPtr();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, *stream);
        if (!error) {
            if (doc["sentiment"].is<float>()) {
                receivedSentiment = doc["sentiment"].as<float>();
                success = true;
                debug(String(F("Sentiment empfangen (Force-Update): ")) + String(receivedSentiment, 2));
                appState.lastMoodUpdate = millis();
            } else {
                debug(F("Fehler: 'sentiment' fehlt/falsch in JSON."));
            }
        } else {
            debug(String(F("JSON Parsing Fehler: ")) + error.c_str());
        }
      } else {
        debug(String(F("HTTP Fehler: ")) + String(httpCode));
        appState.consecutiveSentimentFailures++;
      }
      http.end();
    }

    // Verarbeite das empfangene Sentiment
    if (success) {
      handleSentiment(receivedSentiment);
      appState.initialAnalysisDone = true;
      if (appState.statusLedMode == 2) {
        setStatusLED(0);  // Normalmodus
      }

      // v9.0: CSV stats removed - data managed in backend

    } else {
      debug(String(F("Force-Update fehlgeschlagen")));
    }

    // Aktualisierung abschließen
    appState.isPulsing = false;
    updateLEDs();
    debug(F("Force-Update abgeschlossen."));
});
    // toggle-light Endpunkt
    server.on("/toggle-light", HTTP_GET, []()
              {
    appState.lightOn = !appState.lightOn;

    // Erst die Antwort senden
    server.send(200, "text/plain", "OK");

    // Kleine Pause einfügen
    delay(50);

    // Dann LEDs aktualisieren
    if (appState.lightOn) {
      updateLEDs();
    } else {
      pixels.clear();
      pixels.show();
    }

    // Kleine Pause vor dem Speichern
    delay(50);

    // Zum Schluss die Einstellungen speichern
    appState.settingsNeedSaving = true;
    appState.lastSettingsSaved = millis();

    // Home Assistant aktualisieren
    if (appState.mqttEnabled && mqtt.isConnected()) {
      haLight.setState(appState.lightOn);
    }

    debug(String(F("Licht über Web umgeschaltet: ")) + (appState.lightOn ? "AN" : "AUS")); });

    // toggle-mode Endpunkt
    server.on("/toggle-mode", HTTP_GET, []()
              {
    appState.autoMode = !appState.autoMode;
    // Home Assistant aktualisieren, wenn aktiviert
    if (appState.mqttEnabled && mqtt.isConnected()) {
      haMode.setState(appState.autoMode ? 0 : 1);
    }

    // LEDs aktualisieren, wenn Licht an ist
    if (appState.lightOn) {
      updateLEDs();
    }

    // Einstellung speichern
    appState.settingsNeedSaving = true;
    appState.lastSettingsSaved = millis();

    server.send(200, "text/plain", "OK");
    debug(String(F("Modus über Web umgeschaltet: ")) + (appState.autoMode ? "Auto" : "Manual")); });

    // set-color Endpunkt
    server.on("/set-color", HTTP_GET, []()
              {
    if (server.hasArg("hex")) {
      String hexColor = server.arg("hex");

      // Hex zu RGB konvertieren
      uint32_t rgb = 0;
      sscanf(hexColor.c_str(), "%x", &rgb);

      // RGB-Komponenten extrahieren
      uint8_t r = (rgb >> 16) & 0xFF;
      uint8_t g = (rgb >> 8) & 0xFF;
      uint8_t b = rgb & 0xFF;

      // Farbe setzen
      appState.manualColor = pixels.Color(r, g, b);

      // LEDs aktualisieren, wenn im manuellen Modus und Licht an
      if (!appState.autoMode && appState.lightOn) {
        updateLEDs();
      }

      // Home Assistant aktualisieren, wenn aktiviert
      if (appState.mqttEnabled && mqtt.isConnected()) {
        HALight::RGBColor color;
        color.red = r;
        color.green = g;
        color.blue = b;
        haLight.setRGBColor(color);
      }

      // Einstellung speichern
      appState.settingsNeedSaving = true;
      appState.lastSettingsSaved = millis();

      server.send(200, "text/plain", "OK");
      debug(String(F("Farbe über Web gesetzt: #")) + hexColor);
    } else {
      server.send(400, "text/plain", "Missing hex parameter");
    } });

    // set-brightness Endpunkt
    server.on("/set-brightness", HTTP_GET, []()
              {
    if (server.hasArg("value")) {
      int brightness = server.arg("value").toInt();
      brightness = constrain(brightness, 10, 255);
      appState.manualBrightness = brightness;

      // LEDs aktualisieren, wenn im manuellen Modus und Licht an
      if (!appState.autoMode && appState.lightOn) {
        updateLEDs();
      }

      // Home Assistant aktualisieren, wenn aktiviert
      if (appState.mqttEnabled && mqtt.isConnected()) {
        haLight.setBrightness(brightness);
      }

      // Einstellung speichern
      appState.settingsNeedSaving = true;
      appState.lastSettingsSaved = millis();

      server.send(200, "text/plain", "OK");
      debug(String(F("Helligkeit über Web gesetzt: ")) + brightness);
    } else {
      server.send(400, "text/plain", "Missing value parameter");
    } });

    // v9.0: set-headlines endpoint removed - parameter not used anymore

    server.on("/api/settings/all", HTTP_GET, []() {
        JsonDocument doc;
        
        // Allgemeine Einstellungen
        doc["moodInterval"] = appState.moodUpdateInterval / 1000;
        doc["dhtInterval"] = appState.dhtUpdateInterval / 1000;
        doc["autoMode"] = appState.autoMode;
        doc["lightOn"] = appState.lightOn;
        doc["manBright"] = appState.manualBrightness;
        
        // Farbe als HEX
        char hexColor[10];
        sprintf(hexColor, "#%06X", appState.manualColor);
        doc["manColor"] = hexColor;
        
        // v9.0: headlinesPS removed
      
        // WiFi-Einstellungen (Passwort maskiert)
        doc["wifiSSID"] = appState.wifiSSID;
        doc["wifiConfigured"] = appState.wifiConfigured;
      
        // Erweiterte Einstellungen (Passwort maskiert)
        doc["apiUrl"] = appState.apiUrl;
        doc["mqttServer"] = appState.mqttServer;
        doc["mqttUser"] = appState.mqttUser;
        doc["dhtPin"] = appState.dhtPin;
        doc["dhtEnabled"] = appState.dhtEnabled;
        doc["ledPin"] = appState.ledPin;
        doc["numLeds"] = appState.numLeds;
        doc["mqttEnabled"] = appState.mqttEnabled;
        
        // Farben
        JsonArray colors = doc["colors"].to<JsonArray>();
        for (int i = 0; i < 5; i++) {
          char hexColor[10];
          sprintf(hexColor, "#%06X", appState.customColors[i]);
          colors.add(hexColor);
        }
        
        // Dateisystem-Informationen
if (LittleFS.begin()) {
  size_t totalBytes = LittleFS.totalBytes();
  size_t usedBytes = LittleFS.usedBytes();
  doc["fsTotal"] = totalBytes;
  doc["fsUsed"] = usedBytes;
  doc["hasSettings"] = LittleFS.exists("/data/settings.json");
  doc["hasStats"] = false; // v9.0: stats managed in backend
  doc["hasFeeds"] = false; // v9.0: feeds managed in backend
}
        
char* jsonBuffer = jsonPool.acquire();
size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
server.send(200, "application/json", jsonBuffer);
jsonPool.release(jsonBuffer);
      });

    // Restart Endpunkt
    server.on("/restart", HTTP_GET, []()
              {
    server.send(200, "text/html", "<html><body><h1>Restarting...</h1><p>Device will restart in a few seconds.</p><script>setTimeout(function(){window.location.href='/';}, 10000);</script></body></html>");
    delay(1000);
    ESP.restart(); });

    // Update-Handler für Firmware
    server.on(
        "/update", HTTP_POST, []() {
            server.sendHeader("Connection", "close");
            server.send(200, "text/html", Update.hasError() ? 
                "<html><body><h1>Update Failed!</h1><a href='/'>Return to Homepage</a></body></html>" : 
                "<html><body><h1>Update Successful!</h1><p>Device is restarting...</p><script>setTimeout(function(){window.location.href='/';}, 10000);</script></body></html>");
            delay(1000);
            ESP.restart();
        },
        []() {
            HTTPUpload &upload = server.upload();
            static String extractedVersion = "";
    
            if (upload.status == UPLOAD_FILE_START) {
                String filename = upload.filename;
                debug("Update: " + filename);
                
                // Check naming convention: Firmware-X.X-AuraOS.bin
                if (filename.startsWith("Firmware-") && filename.endsWith(".bin")) {
                    int dashPos = filename.indexOf('-', 9); // Position after "Firmware-X.X"
                    
                    if (dashPos > 0) {
                        // Extract version from filename (e.g., "2.1" from "Firmware-2.1-AuraOS.bin")
                        extractedVersion = filename.substring(9, dashPos);
                        debug(String(F("Firmware-Version aus Dateiname: ")) + extractedVersion);
                    }
                } else {
                    debug(F("Warnung: Firmware folgt nicht der Namenskonvention (Firmware-X.X-AuraOS.bin)"));
                }
                
                setStatusLED(3); // Update-Modus für Status-LED
    
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    debug(F("ERROR: Update Begin fehlgeschlagen"));
                    Update.printError(Serial);
                }
            }
            else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    debug(F("ERROR: Update Write fehlgeschlagen"));
                    Update.printError(Serial);
                }
            }
            else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    debug("Update erfolgreich: " + String(upload.totalSize) + " Bytes");
                    
                    // Save the extracted version to a file for future reference
                    if (extractedVersion.length() > 0) {
                        debug(String(F("Speichere Firmware-Version: ")) + extractedVersion);
                        File versionFile = LittleFS.open("/firmware-version.txt", "w");
                        if (versionFile) {
                            versionFile.print(extractedVersion);
                            versionFile.close();
                        }
                    }
                }
                else {
                    debug(F("ERROR: Update End fehlgeschlagen"));
                    Update.printError(Serial);
                }
            }
        });

        server.on("/api/firmware-version", HTTP_GET, []() {
            JsonDocument doc;
            doc["version"] = getCurrentFirmwareVersion();
            char* jsonBuffer = jsonPool.acquire();
            size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
            server.send(200, "application/json", jsonBuffer);
            jsonPool.release(jsonBuffer);
        });
    // Generischer Handler für alle anderen statischen Dateien
    server.onNotFound([]()
                      {
    String path = server.uri();
    handleStaticFile(path); });

    server.begin();
    debug(F("Webserver gestartet"));

}

// === LED-Animationen ===
void pulseCurrentColor()
{
    uint8_t targetBrightness = appState.autoMode ? DEFAULT_LED_BRIGHTNESS : appState.manualBrightness;

    // Debug pulsing state periodically
    static unsigned long lastPulseDebug = 0;
    if (millis() - lastPulseDebug >= 10000)
    { // Every 10 seconds
        if (appState.isPulsing)
        {
            debug(String(F("Pulse Status: Aktiv - Laufzeit: ")) + String((millis() - appState.pulseStartTime) / 1000) + "s");
        }
        lastPulseDebug = millis();
    }

    if (!appState.isPulsing || !appState.lightOn)
    {
        // Stelle sicher, dass Helligkeit korrekt ist, wenn nicht gepulst wird
        static uint8_t lastSetBrightness = 0;
        if (pixels.getBrightness() != targetBrightness)
        {
            pixels.setBrightness(targetBrightness);
            pixels.show(); // Zeige korrekte Helligkeit
            debug(String(F("Pulse: Helligkeit zurückgesetzt auf ")) + targetBrightness);
        }
        return;
    }

    unsigned long currentTime = millis();
    unsigned long elapsedTime = currentTime - appState.pulseStartTime;

    // Auto-disable pulsing after timeout (3 cycles)
    if (elapsedTime > DEFAULT_WAVE_DURATION * 3)
    {
        debug(F("Pulse: Timeout - Auto-disable nach 3 Zyklen"));
        appState.isPulsing = false;
        pixels.setBrightness(targetBrightness);
        pixels.show();
        return;
    }

    float progress = fmod((float)elapsedTime, (float)DEFAULT_WAVE_DURATION) / (float)DEFAULT_WAVE_DURATION;
    float easedValue = (sin(progress * 2.0 * PI - PI / 2.0) + 1.0) / 2.0;

    // Use the config values instead of hardcoded ones
    // Explizite Typkonvertierung, um den Compilerfehler zu vermeiden
    uint8_t minPulseBright = (DEFAULT_WAVE_MIN_BRIGHTNESS < targetBrightness / 2) ? DEFAULT_WAVE_MIN_BRIGHTNESS : (targetBrightness / 2);
    uint8_t maxPulseBright = (DEFAULT_WAVE_MAX_BRIGHTNESS < targetBrightness) ? DEFAULT_WAVE_MAX_BRIGHTNESS : targetBrightness;

    int brightness = minPulseBright + (int)(easedValue * (maxPulseBright - minPulseBright));

    pixels.setBrightness(constrain(brightness, 0, 255));
    pixels.show();
}

// === Home Assistant Callbacks ===
void onStateCommand(bool state, HALight *sender)
{
    // Ignoriere Callbacks während wir Initial States senden
    if (appState.sendingInitialStates) {
        debug(F("Ignoriere State Command während Initial States"));
        return;
    }

    if (state == appState.lightOn)
        return;
    appState.lightOn = state;
    debug(String(F("HA Light State Command: ")) + (state ? "ON" : "OFF"));
    if (!state)
    {
        pixels.clear();
        pixels.show();
    }
    else
    {
        updateLEDs(); // Stellt korrekten Zustand wieder her
    }
    sender->setState(state);

    // Direkt speichern statt verzögerter Speicherung
    saveSettings();
}

void onBrightnessCommand(uint8_t brightness, HALight *sender)
{
    // Ignoriere Callbacks während wir Initial States senden
    if (appState.sendingInitialStates) {
        debug(F("Ignoriere Brightness Command während Initial States"));
        return;
    }

    if (brightness == appState.manualBrightness)
        return;
    appState.manualBrightness = brightness;
    debug(String(F("HA Brightness Command: ")) + brightness);
    if (!appState.autoMode && appState.lightOn)
    {
        updateLEDs(); // updateLEDs berücksichtigt jetzt appState.manualBrightness
    }
    sender->setBrightness(brightness);

    // Direkt speichern statt verzögerter Speicherung
    saveSettings();
}

void onRGBColorCommand(HALight::RGBColor color, HALight *sender)
{
    // Ignoriere Callbacks während wir Initial States senden
    if (appState.sendingInitialStates) {
        debug(F("Ignoriere RGB Command während Initial States"));
        return;
    }

    uint32_t newColor = pixels.Color(color.red, color.green, color.blue);
    if (newColor == appState.manualColor)
        return;
    appState.manualColor = newColor;
    debug(String(F("HA RGB Command: R=")) + color.red + " G=" + color.green + " B=" + color.blue);
    if (!appState.autoMode && appState.lightOn)
    {
        updateLEDs(); // updateLEDs berücksichtigt jetzt appState.manualColor
    }
    sender->setRGBColor(color);
    
    // Direkt speichern statt verzögerter Speicherung
    saveSettings();
}

void onModeCommand(int8_t index, HASelect *sender) {
    // Ignore mode commands during startup phase
    if (appState.initialStartupPhase) {
        debug(F("Ignoring mode command during startup grace period"));
        // Force HA back to the current state instead of accepting the change
        sender->setState(appState.autoMode ? 0 : 1);
        return;
    }

    bool newMode = (index == 0);
    if (newMode == appState.autoMode) return;
    appState.autoMode = newMode;
    debug(String(F("HA Mode Command: ")) + (appState.autoMode ? "Auto" : "Manual"));
    if (appState.lightOn) {
        updateLEDs();
    }
    sender->setState(index);
    
    // Direkt speichern statt verzögerter Speicherung
    saveSettings();
}

void onUpdateIntervalCommand(HANumeric value, HANumber *sender)
{
    // Ignoriere Callbacks während wir Initial States senden
    if (appState.sendingInitialStates) {
        debug(F("Ignoriere Update Interval Command während Initial States"));
        return;
    }

    float intervalSeconds = value.toFloat();
    intervalSeconds = constrain(intervalSeconds, 10, 7200);
    unsigned long newInterval = (unsigned long)(intervalSeconds * 1000);
    if (newInterval == appState.moodUpdateInterval)
        return;
    appState.moodUpdateInterval = newInterval;
    sender->setState(float(intervalSeconds));
    debug(String(F("Mood Update Interval gesetzt auf: ")) + String(intervalSeconds) + "s");

    // Direkt speichern statt verzögerter Speicherung
    saveSettings();

    // Reset des Zeitgebers für das nächste Update (optional)
    // appState.lastMoodUpdate = millis() - (appState.moodUpdateInterval / 2); // Hälfte des Intervalls
}

// v9.0: onHeadlinesCommand() removed - headlines_per_source not used anymore

void onDHTIntervalCommand(HANumeric value, HANumber *sender)
{
    // Ignoriere Callbacks während wir Initial States senden
    if (appState.sendingInitialStates) {
        debug(F("Ignoriere DHT Interval Command während Initial States"));
        return;
    }

    float intervalSeconds = value.toFloat();
    intervalSeconds = constrain(intervalSeconds, 10, 7200);
    unsigned long newInterval = (unsigned long)(intervalSeconds * 1000);
    if (newInterval == appState.dhtUpdateInterval)
        return;
    appState.dhtUpdateInterval = newInterval;
    sender->setState(float(intervalSeconds));
    debug(String(F("DHT Interval gesetzt auf: ")) + String(intervalSeconds) + "s");

    // Direkt speichern statt verzögerter Speicherung
    saveSettings();
}

void onRefreshButtonPressed(HAButton *sender)
{
    debug(F("Sentiment Refresh über Home Assistant ausgelöst"));

    // Direkt einen HTTP-Abruf starten
    HTTPClient http;
    bool success = false;
    float receivedSentiment = 0.0;

    http.setReuse(false);
    http.setUserAgent("MoodlightClient/1.0");

    appState.isPulsing = true;
    appState.pulseStartTime = millis();

    debug(F("Starte Sentiment HTTP-Abruf (Force-Update)..."));
    // v9.0: headlines_per_source parameter removed - not used by new /api/moodlight/* endpoints
    if (http.begin(wifiClientHTTP, appState.apiUrl))
    {
        http.setTimeout(10000);
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK)
        {
            WiFiClient *stream = http.getStreamPtr();
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, *stream);
            if (!error) {
                if (doc["sentiment"].is<float>()) {
                    receivedSentiment = doc["sentiment"].as<float>();
                    success = true;
                    debug(String(F("Sentiment empfangen (Force-Update): ")) + String(receivedSentiment, 2));
                    appState.lastMoodUpdate = millis();
                } else {
                    debug(F("Fehler: 'sentiment' fehlt/falsch in JSON."));
                }
            }
            else
            {
                debug(String(F("JSON Parsing Fehler: ")) + error.c_str());
            }
        }
        else
        {
            debug(String(F("HTTP Fehler: ")) + String(httpCode));
        }
        http.end();
    }
    else
    {
        debug(String(F("HTTP Verbindungsfehler zu: ")) + String(appState.apiUrl));
        if (wifiClientHTTP.connected())
        {
            wifiClientHTTP.stop();
        }
    }

    if (success)
    {
        handleSentiment(receivedSentiment);
        appState.initialAnalysisDone = true;
    }

    appState.isPulsing = false;
    updateLEDs();
}

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
    // v9.0: haHeadlinesPerSource removed
    uint32_t initialColor = appState.autoMode ? pixels.Color(
                                           getColorDefinition(appState.currentLedIndex).r,
                                           getColorDefinition(appState.currentLedIndex).g,
                                           getColorDefinition(appState.currentLedIndex).b)
                                     : appState.manualColor;
    HALight::RGBColor color;
    color.red = (initialColor >> 16) & 0xFF;
    color.green = (initialColor >> 8) & 0xFF;
    color.blue = initialColor & 0xFF;
    haLight.setRGBColor(color);
    haMode.setState(appState.autoMode ? 0 : 1);

    // Intervalle (aus globalen Variablen)
    haUpdateInterval.setState(float(appState.moodUpdateInterval / 1000.0));
    haDhtInterval.setState(float(appState.dhtUpdateInterval / 1000.0));

    // DHT direkt lesen für aktuelle Werte
    debug(F("Lese aktuelle DHT Werte für initiale Zustände..."));
    float currentTemp = dht.readTemperature();
    float currentHum = dht.readHumidity();
    bool tempValid = !isnan(currentTemp);
    bool humValid = !isnan(currentHum);

    if (tempValid)
    {
        appState.currentTemp = currentTemp; // Update globale Variable
        haTemperature.setValue(floatToString(currentTemp, 1).c_str());
        debug(String(F("  Init Temp: ")) + String(currentTemp, 1) + "C");
    }
    else
    {
        debug(F("  Init Temp: Lesefehler!"));
        // Sende letzten bekannten Wert, falls vorhanden
        if (!isnan(appState.currentTemp))
            haTemperature.setValue(floatToString(appState.currentTemp, 1).c_str());
    }
    if (humValid)
    {
        appState.currentHum = currentHum; // Update globale Variable
        haHumidity.setValue(floatToString(currentHum, 1).c_str());
        debug(String(F("  Init Hum: ")) + String(currentHum, 1) + "%");
    }
    else
    {
        debug(F("  Init Hum: Lesefehler!"));
        // Sende letzten bekannten Wert, falls vorhanden
        if (!isnan(appState.currentHum))
            haHumidity.setValue(floatToString(appState.currentHum, 1).c_str());
    }
    // Setze appState.lastDHTUpdate, da wir gerade gelesen haben (verhindert sofortiges Lesen im Loop)
    appState.lastDHTUpdate = millis();

    // Sentiment (letzter bekannter Wert aus globalen Variablen)
    haSentimentScore.setValue(floatToString(appState.sentimentScore, 2).c_str());
    haSentimentCategory.setValue(appState.sentimentCategory.c_str());
    debug(String(F("  Init Sentiment Score: ")) + String(appState.sentimentScore, 2));
    debug(String(F("  Init Sentiment Category: ")) + appState.sentimentCategory);

    // Initiale Heartbeat-Werte senden
    sendHeartbeat();

    debug(F("Initiale Zustände gesendet."));

    // Flag zurücksetzen - Callbacks sind jetzt wieder aktiv
    appState.sendingInitialStates = false;
}

// Add this function for safer HTTP requests with JSON
bool safeHttpGet(const String &url, JsonDocument &doc) {
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

        // Process valid sentiment value (vor appState.initialAnalysisDone=true, damit MQTT-Werte gesendet werden!)
        handleSentiment(receivedSentiment);
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
    // First check if DHT is enabled
    if (!appState.dhtEnabled)
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
            temp = dht.readTemperature();
            tempValid = !isnan(temp);

            // Small delay between reads for stability
            delay(10);

            hum = dht.readHumidity();
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

// === Verbesserte MQTT Reconnect und Heartbeat ===
// Fix the checkAndReconnectMQTT function - add the missing closing brace
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
                }

                appState.lastMqttReconnectAttempt = currentMillis;

                if (mqtt.isConnected()) {
                    debug(F("MQTT erfolgreich verbunden."));
                    appState.mqttWasConnected = true;
                    mqttReconnectBackoff = 10000; // Reset backoff on success
                    
                    // Schedule initial state sending for next loop cycle
                    // instead of doing it immediately
                    static bool initialStatesPending = true;
                    initialStatesPending = true;
                    
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
            
            // Schedule state sending for next loop cycle
            static bool initialStatesPending = true;
            initialStatesPending = true;
            
            appState.mqttWasConnected = true;
            
            // Update status LED without direct update
            appState.statusLedMode = 0; // Normal mode
            
            if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
                appState.ledUpdatePending = true; // Request full LED update
                xSemaphoreGive(appState.ledMutex);
            }
        }
        
        // Process deferred initial state sending
        static bool initialStatesPending = false;
        if (initialStatesPending && mqtt.isConnected()) {
            // Wait for a safe moment to send states
            yield();
            delay(10);
            sendInitialStates();
            initialStatesPending = false;
        }
    }
}

// === Periodisches System-Logging für bessere Diagnose ===
void logSystemStatus()
{
    unsigned long currentMillis = millis();

    if (currentMillis - appState.lastStatusLog >= STATUS_LOG_INTERVAL)
    {
        debug(F("=== SYSTEM STATUS ==="));
        debug(String(F("Uptime: ")) + String(currentMillis / 1000 / 60) + F(" minutes"));
        debug(String(F("Free Heap: ")) + String(ESP.getFreeHeap()) + F(" bytes"));
        debug(String(F("WiFi Status: ")) + (WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected"));
        debug(String(F("WiFi RSSI: ")) + String(WiFi.RSSI()) + F(" dBm"));
        debug(String(F("MQTT Enabled: ")) + (appState.mqttEnabled ? "Yes" : "No"));
        if (appState.mqttEnabled)
        {
            debug(String(F("MQTT Status: ")) + (mqtt.isConnected() ? "Connected" : "Disconnected"));
        }
        debug(String(F("LED Status: ")) + (appState.lightOn ? "ON" : "OFF") + ", Mode: " + (appState.autoMode ? "Auto" : "Manual"));
        debug(String(F("Current Sentiment: ")) + String(appState.sentimentScore, 2) + F(" (") + appState.sentimentCategory + F(")"));
        debug(String(F("DHT Values: T=")) + String(appState.currentTemp, 1) + F("C, H=") + String(appState.currentHum, 1) + F("%"));
        debug(String(F("Intervals: Mood=")) + String(appState.moodUpdateInterval / 1000) + F("s, DHT=") + String(appState.dhtUpdateInterval / 1000) + F("s"));
        debug(F("===================="));

        appState.lastStatusLog = currentMillis;
    }
}

// === Arduino Setup ===
// Add NTP initialization to setup function
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

    #ifndef CONFIG_FREERTOS_UNICORE
        // Stack-Größe für Loop-Task festlegen für bessere Stabilität
        uint16_t loopTaskStackSize = 16384;
        
        // Handle für den aktuellen Loop-Task holen für Stack-Analyse
        TaskHandle_t loopTask = xTaskGetCurrentTaskHandle();
        if (loopTask != NULL) {
            // Stack-Auslastung überwachen
            UBaseType_t stackSize = uxTaskGetStackHighWaterMark(loopTask);
            debug(String(F("Current loop task stack high water mark: ")) + stackSize);
        }
    #endif
    
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

    // v9.0: CSV repair and archiving tasks removed - stats managed in backend

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
    jsonPool.init();

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

    // v9.0: Archivierungsprozess removed - archiving handled in backend

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

            // REMOVED v9.0: RSS configuration now managed in backend
            // No need to send feed config on startup

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
    if (millis() - lastServerHandle >= 20) {  // Alle 20ms
        server.handleClient();
        lastServerHandle = millis();
    }

    // Prüfen, ob ein Neustart angefordert wurde
    if (appState.rebootNeeded && millis() > appState.rebootTime) {
        debug(F("Ausführen des angeforderten Neustarts..."));
        delay(200);
        ESP.restart();
    }

    // Tägliche Archivierung alter Daten
    // v9.0: Daily archive check removed - archiving handled in backend

    // Startup-Grace-Period beenden nach definierter Zeit
    if (appState.initialStartupPhase && (millis() - appState.startupTime > STARTUP_GRACE_PERIOD)) {
        appState.initialStartupPhase = false;
        debug(F("Initial startup grace period ended, now accepting all commands"));
    }
    
    // MQTT-Loop periodisch ausführen für Verbindungspflege
    static unsigned long lastMqttLoop = 0;
    if (appState.mqttEnabled && WiFi.status() == WL_CONNECTED && (millis() - lastMqttLoop >= 100)) {
        mqtt.loop();
        lastMqttLoop = millis();
    }

    // WiFi- und MQTT-Verbindungen prüfen und wiederherstellen
    static unsigned long lastConnectionCheck = 0;
    if (millis() - lastConnectionCheck >= 2000) {  // Alle 2 Sekunden
        lastConnectionCheck = millis();

        checkAndReconnectWifi();

        if (appState.mqttEnabled) {
            checkAndReconnectMQTT();
        }
    }

    // Anstehende LED-Updates verarbeiten
    processLEDUpdates();

    // Einstellungen speichern, falls geändert
    if (appState.settingsNeedSaving && (millis() - appState.lastSettingsSaved > 2000)) {
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
        if (millis() - lastPulseUpdate >= 30) {  // 30ms zwischen Updates
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
            
            // Statistik-Datei rotieren (24 Dateien)
            static int fileCounter = 0;
            String fileName = "/data/sysstat_" + String(fileCounter++ % 24) + ".json";
            
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
            
            // Neustart in der Nacht planen (3:00-4:00 Uhr)
            if (timeinfo.tm_hour >= 2 && timeinfo.tm_hour < 4) {
                debug(F("Nachtstunden, führe Neustart sofort durch..."));
                appState.rebootNeeded = true;
                appState.rebootTime = currentMillis + 60000;  // 1 Minute Verzögerung
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
            if (timeinfo.tm_hour >= 3 && timeinfo.tm_hour < 4) {
                debug(F("Geplanter Neustart wird ausgeführt..."));
                appState.rebootNeeded = true;
                appState.rebootTime = currentMillis + 30000;  // 30 Sekunden Verzögerung
                
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
        
        if (percentUsed > 85) {
            debug(F("Hohe Dateisystembelegung erkannt - v9.0: Archivierung läuft im Backend"));
            // v9.0: Archiving handled in backend, no local action needed
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
    delay(20);  // 20ms Pause
    
    // Regelmaessiger Systemgesundheitscheck (5 Minuten)
    static unsigned long lastHealthCheck = 0;
    if (millis() - lastHealthCheck > 300000) {
        sysHealth.update();
        memMonitor.update();
        lastHealthCheck = millis();
    }
}