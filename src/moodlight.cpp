// ========================================================
// Moodlight mit OpenAI Sentiment Analyse & Home Assistant
// ========================================================
// Version: 8.2 - Diagnostics, Import, Export, Webinterface, MQTT-Heartbeat, WiFi Manager und Settings-Portal, Farbauswahl, littleFS Dateisystem, RSSFeed, UI Update, ArduinoJSON 7.4.0 Umstellung, CSV Datei optimiert, JSONBuffer Pool
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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "LittleFS.h"
#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>
#include <time.h>
// #define DEBUG_MODE
// #define CONFIG_FREERTOS_UNICORE
#include "config.h"

// 192.168.4.1 - Standard-IP für den Access Point
#define CAPTIVE_PORTAL_IP \
    {192, 168, 4, 1}

    // JSON-Puffer-Pool für HTTP-Antworten
#define JSON_BUFFER_SIZE 16384
#define JSON_BUFFER_COUNT 2  // Anzahl der Puffer im Pool

// DNS Server für den Captive Portal
DNSServer dnsServer;
const byte DNS_PORT = 53;
bool isInConfigMode = false;

// === Farbdefinitionen ===
struct ColorDefinition
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// Konfigurierbare Farben für die Stimmungsskala
uint32_t customColors[5] = {
    0xFF0000, // sehr negativ (Rot)
    0xFFA500, // negativ (Orange)
    0x1E90FF, // neutral (Blau)
    0x545DF0, // positiv (Indigo/Violett-Blau)
    0x8A2BE2  // sehr positiv (Violett)
};

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
    return uint32ToColorDef(customColors[index]);
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
WiFiClient wifiClientHTTP; // Globale Instanz für HTTPClient
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
HANumber haHeadlinesPerSource("headlines_per_source", HANumber::PrecisionP0);
HASensor haSentimentCategory("sentiment_category");
// Heartbeat-Sensoren
HASensor haUptime("uptime");
HASensor haWifiSignal("wifi_signal");
HASensor haSystemStatus("system_status");

// === Webserver für Updates und Logging ===
WebServer server(80);
const int LOG_BUFFER_SIZE = 20;    // Anzahl der zu speichernden Log-Zeilen
String logBuffer[LOG_BUFFER_SIZE]; // Ringpuffer für Logs
int logIndex = 0;                  // Aktuelle Position im Ringpuffer

// === Hardware Setup ===
Adafruit_NeoPixel pixels;
DHT dht(DEFAULT_DHT_PIN, DHT22);
Preferences preferences;

// Add these global variables
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;     // GMT+1 (Adjust for your timezone, in seconds)
const int   daylightOffset_sec = 3600; // +1 hour for DST (summer time)
bool timeInitialized = false;

// === Globale Variablen ===

SemaphoreHandle_t ledMutex = NULL; // Add this for thread safety
const String SOFTWARE_VERSION = MOODLIGHT_FULL_VERSION;
int ledPin = DEFAULT_LED_PIN;
int dhtPin = DEFAULT_DHT_PIN;
int numLeds = DEFAULT_NUM_LEDS;
uint8_t manualBrightness = DEFAULT_LED_BRIGHTNESS;
unsigned long moodUpdateInterval = DEFAULT_MOOD_UPDATE_INTERVAL;
unsigned long dhtUpdateInterval = DEFAULT_DHT_READ_INTERVAL;
String apiUrl = DEFAULT_NEWS_API_URL;
volatile bool ledUpdatePending = false;
volatile uint32_t ledColors[DEFAULT_NUM_LEDS];
volatile uint8_t ledBrightness = DEFAULT_LED_BRIGHTNESS;
volatile bool ledClear = false;
int currentLedIndex = 2;
int lastLedIndex = 2;
int headlines_per_source = 1;
unsigned long lastMoodUpdate = 0;
bool settingsNeedSaving = false;
unsigned long lastSettingsSaved = 0;
bool firstLedShowDone = false;
unsigned long lastDHTUpdate = 0;
bool dhtEnabled = true;
float lastTemp = NAN;
float lastHum = NAN;
float lastSentimentScore = 0.0;
String lastSentimentCategory = "neutral";
bool initialAnalysisDone = false;
bool isPulsing = false;
unsigned long pulseStartTime = 0;
bool autoMode = true;                                    // Default, wird von Preferences überschrieben
bool lightOn = true;                                     // Default, wird von Preferences überschrieben
uint32_t manualColor = pixels.Color(255, 255, 255);      // Default, wird ggf. von Preferences überschrieben
bool initialStartupPhase = true;
unsigned long startupTime = 0;
const unsigned long STARTUP_GRACE_PERIOD = 15000; 

// Neue konfigurierbare Werte
String mqttServer = "";               // MQTT Server
String mqttUser = "";                 // MQTT User
String mqttPassword = "";             // MQTT Password
bool mqttEnabled = false;             // MQTT-Integration aktiviert?

// Zugangsdaten für WiFi
String wifiSSID = "";
String wifiPassword = "";
bool wifiConfigured = false;

// WiFi Reconnect mit exponentiellem Backoff
unsigned long lastWifiCheck = 0;
unsigned long wifiReconnectDelay = 5000;          // Basis-Reconnect-Verzögerung (5 Sekunden)
const unsigned long MAX_RECONNECT_DELAY = 300000; // Maximale Verzögerung (5 Minuten)
int wifiReconnectAttempts = 0;
bool wifiWasConnected = false; // Zu Beginn als false annehmen

// MQTT Reconnect und Heartbeat
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastMqttHeartbeat = 0;
const unsigned long MQTT_HEARTBEAT_INTERVAL = 60000; // 1 Minute
bool mqttWasConnected = false;                       // Um Aktionen nach Reconnect zu steuern

// Fehlertolerante Sentiment-Verarbeitung
unsigned long lastSuccessfulSentimentUpdate = 0;
const unsigned long SENTIMENT_FALLBACK_TIMEOUT = 3600000; // Nach 1 Stunde ohne erfolgreiche Aktualisierung
bool sentimentAPIAvailable = true;
int consecutiveSentimentFailures = 0;
const int MAX_SENTIMENT_FAILURES = 5;

// Status-LED
const int STATUS_LED_INDEX = DEFAULT_NUM_LEDS - 1; // Letzte LED für Status
unsigned long statusLedBlinkStart = 0;
bool statusLedState = false;
int statusLedMode = 0; // 0=Normal, 1=WiFi-Verbindung, 2=API-Fehler, 3=Update, 4=MQTT-Verbindung, 5=AP-Modus

// System status logging
unsigned long lastStatusLog = 0;
const unsigned long STATUS_LOG_INTERVAL = 300000; // 5 Minuten

// Access Point Timeout - Nach dieser Zeit ohne Konfiguration wird der AP-Modus verlassen
const unsigned long AP_TIMEOUT = 300000; // 5 Minuten
unsigned long apModeStartTime = 0;

// Automatischer Reboot nach Konfiguration
bool rebootNeeded = false;
unsigned long rebootTime = 0;
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
    logBuffer[logIndex] = logEntry;  // Copy assignment is safer
    logIndex = (logIndex + 1) % LOG_BUFFER_SIZE;
    
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
    logBuffer[logIndex] = logEntry;
    logIndex = (logIndex + 1) % LOG_BUFFER_SIZE;
    
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
CSVBuffer statsBuffer("/data/stats.csv");
NetworkDiagnostics netDiag;
SystemHealthCheck sysHealth;
TaskManager archiveTask("ArchiveTask", 8192); 
unsigned long lastSystemHealthCheckTime = 0;
const unsigned long HEALTH_CHECK_INTERVAL = 3600000; // 1 Stunde

void archiveOldData();

void archiveTaskFunction(void* parameter) {
    for (;;) {
        // Warte auf Signal vom Haupttask
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        debug(F("Archivierungstask aktiviert"));
        
        // Speicherprüfung vor dem Start
        if (memMonitor.checkHeapBefore("Archivierung", 30000)) {
            archiveOldData();
            debug(F("Archivierung abgeschlossen"));
        } else {
            debug(F("Nicht genügend Speicher für Archivierung"));
        }
        
        // Optional: Task in regelmäßigen Abständen ausführen
        // vTaskDelay(24 * 60 * 60 * 1000 / portTICK_PERIOD_MS); // Tägliche Ausführung
    }
}

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
        if (xSemaphoreTake(mutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
            for (int i = 0; i < JSON_BUFFER_COUNT; i++) {
                if (buffer == buffers[i]) {
                    inUse[i] = false;
                    xSemaphoreGive(mutex);
                    return;
                }
            }
            xSemaphoreGive(mutex);
            // Wenn Buffer nicht im Pool ist, war es ein Heap-Allocation
            delete[] buffer;
        }
    }
};

JsonBufferPool jsonPool;

// Add this function to initialize time via NTP
// Simplified NTP time initialization
void initTime() {
    if (WiFi.status() != WL_CONNECTED) {
        debug(F("NTP: Kann Zeit nicht synchronisieren, kein WLAN"));
        return;
    }

    debug(F("NTP: Synchronisiere Zeit..."));
    
    // Simple time config with fewer parameters
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    
    // Set timezone
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    
    // Wait briefly for time to be set
    delay(1000);
    
    // Check if time is set
    time_t now;
    time(&now);
    
    if (now > 1600000000) { // Timestamp after 2020
        timeInitialized = true;
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        
        char dateStr[64];
        strftime(dateStr, sizeof(dateStr), "%d.%m.%Y %H:%M:%S", &timeinfo);
        debug(String(F("NTP: Zeit synchronisiert - ")) + dateStr);
    } else {
        debug(F("NTP: Zeitsynchronisation fehlgeschlagen"));
    }
}

// === Aktualisiere die LEDs ===
void updateLEDs() {
    if (!lightOn) {
        // Just set the clear flag and return
        if (xSemaphoreTake(ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            ledClear = true;
            ledUpdatePending = true;
            xSemaphoreGive(ledMutex);
        }
        return;
    }

    // Licht soll an sein
    uint32_t colorToShow;
    uint8_t brightnessToShow;

    if (autoMode) {
        currentLedIndex = constrain(currentLedIndex, 0, 4);
        ColorDefinition color = getColorDefinition(currentLedIndex);
        colorToShow = pixels.Color(color.r, color.g, color.b);
        brightnessToShow = DEFAULT_LED_BRIGHTNESS;
    } else {
        colorToShow = manualColor;
        brightnessToShow = manualBrightness;
    }

    // Instead of directly updating LEDs, store the values safely
    if (xSemaphoreTake(ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        // Fill LED buffer
        for (int i = 0; i < numLeds; i++) {
            // Skip status LED if in status mode
            if (i == (numLeds - 1) && statusLedMode != 0) {
                continue;
            }
            ledColors[i] = colorToShow;
        }
        ledBrightness = brightnessToShow;
        ledClear = false;
        ledUpdatePending = true;
        xSemaphoreGive(ledMutex);
    }

    // Debug output optimized
    uint8_t r = (colorToShow >> 16) & 0xFF;
    uint8_t g = (colorToShow >> 8) & 0xFF;
    uint8_t b = colorToShow & 0xFF;

    char debugBuffer[100];
    snprintf(debugBuffer, sizeof(debugBuffer),
             "LEDs update requested: %s B=%d RGB(%d,%d,%d)",
             (autoMode ? "Auto" : "Manual"),
             brightnessToShow, r, g, b);
    debug(debugBuffer);
}

// === Status-LED Funktionen ===
void setStatusLED(int mode) {
    statusLedMode = mode;
    statusLedBlinkStart = millis();
    statusLedState = true;

    // Store LED state but don't update directly
    if (xSemaphoreTake(ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
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
                ledUpdatePending = true;
                xSemaphoreGive(ledMutex);
                return;
        }

        // Only update the status LED
        ledColors[STATUS_LED_INDEX] = statusColor;
        ledUpdatePending = true;
        xSemaphoreGive(ledMutex);
    }
}

void updateStatusLED() {
    // Kein Blinken im Normalmodus
    if (statusLedMode == 0)
        return;

    // Zeit für Blink-Update
    unsigned long currentMillis = millis();

    // Unterschiedliche Blink-Frequenzen für verschiedene Modi
    unsigned long blinkInterval;
    switch (statusLedMode) {
        case 1: blinkInterval = 500; break; // WiFi (schnell)
        case 2: blinkInterval = 300; break; // API-Fehler (sehr schnell)
        case 3: blinkInterval = 1000; break; // Update (langsam)
        case 4: blinkInterval = 700; break; // MQTT (mittel)
        case 5: blinkInterval = 200; break; // AP-Modus (sehr schnell)
        default: blinkInterval = 500;
    }

    // Zeit abgelaufen?
    if (currentMillis - statusLedBlinkStart >= blinkInterval) {
        statusLedState = !statusLedState;
        statusLedBlinkStart = currentMillis;

        if (xSemaphoreTake(ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            if (statusLedState) {
                // LED einschalten mit entsprechender Farbe
                uint32_t statusColor = pixels.Color(0, 0, 0);
                
                switch (statusLedMode) {
                    case 1: statusColor = pixels.Color(0, 0, 255); break; // Blau
                    case 2: statusColor = pixels.Color(255, 0, 0); break; // Rot
                    case 3: statusColor = pixels.Color(0, 255, 0); break; // Grün
                    case 4: statusColor = pixels.Color(0, 255, 255); break; // Cyan
                    case 5: statusColor = pixels.Color(255, 255, 0); break; // Gelb
                }
                
                ledColors[STATUS_LED_INDEX] = statusColor;
            } else {
                // LED ausschalten
                ledColors[STATUS_LED_INDEX] = pixels.Color(0, 0, 0);
            }
            
            ledUpdatePending = true;
            xSemaphoreGive(ledMutex);
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
    
    if (xSemaphoreTake(ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        needsUpdate = ledUpdatePending;
        if (needsUpdate) {
            // Apply the stored LED values
            if (ledClear) {
                pixels.clear();
            } else {
                pixels.setBrightness(ledBrightness);
                
                for (int i = 0; i < numLeds; i++) {
                    pixels.setPixelColor(i, ledColors[i]);
                }
            }
            ledUpdatePending = false;
        }
        xSemaphoreGive(ledMutex);
    }
    
    // Only call show() if we have updates and not in a critical section
    if (needsUpdate) {
        // Don't update LEDs when WiFi or MQTT operations are in progress
        if (!WiFi.isConnected() || (WiFi.isConnected() && mqtt.isConnected()) || !mqttEnabled) {
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

// === Captive Portal Funktionen ===
// Captive Portal Redirect Handler - Leitet alle DNS-Anfragen zur Konfigurations-IP
class CaptiveRequestHandler : public RequestHandler
{
public:
    CaptiveRequestHandler() {}
    virtual ~CaptiveRequestHandler() {}

    bool canHandle(HTTPMethod method, String uri)
    {
        return true; // Fängt alles ab
    }

    bool handle(WebServer &server, HTTPMethod requestMethod, String requestUri) {
        // Don't create new strings in this critical handler
        if (requestUri.startsWith("/setup") || 
            requestUri.startsWith("/wifiscan") || 
            requestUri.startsWith("/style.css") || 
            requestUri.startsWith("/logs") || 
            requestUri.startsWith("/status") || 
            requestUri == "/") {
            return false; // Normal handler
        }
        
        // Use a static URL
        static const char* redirectUrl = "http://192.168.4.1/setup";
        server.sendHeader("Location", redirectUrl, true);
        server.send(302, "text/plain", "");
        return true;
    }
};

// Verarbeite DNS-Anfragen für den Captive Portal
void processDNS()
{
    if (isInConfigMode)
    {
        dnsServer.processNextRequest();
    }
}

bool saveSettingsToFile() {
    if (!LittleFS.exists("/data")) {
        if (!LittleFS.mkdir("/data")) {
            debug(F("Fehler beim Erstellen des /data Verzeichnisses"));
            return false;
        }
    }

    JsonDocument doc;

    // Allgemeine Einstellungen
    doc["moodInterval"] = moodUpdateInterval;
    doc["dhtInterval"] = dhtUpdateInterval;
    doc["autoMode"] = autoMode;
    doc["lightOn"] = lightOn;
    doc["manBright"] = manualBrightness;
    doc["manColor"] = manualColor;
    doc["headlinesPS"] = headlines_per_source;

    // WiFi-Einstellungen
    doc["wifiSSID"] = wifiSSID;
    doc["wifiPass"] = wifiPassword;
    doc["wifiConfigured"] = wifiConfigured;

    // Erweiterte Einstellungen
    doc["apiUrl"] = apiUrl;
    doc["mqttServer"] = mqttServer;
    doc["mqttUser"] = mqttUser;
    doc["mqttPass"] = mqttPassword;
    doc["dhtPin"] = dhtPin;
    doc["dhtEnabled"] = dhtEnabled;
    doc["ledPin"] = ledPin;
    doc["numLeds"] = numLeds;
    doc["mqttEnabled"] = mqttEnabled;

    // Benutzerdefinierte Farben
    for (int i = 0; i < 5; i++) {
        doc["color" + String(i)] = customColors[i];
    }

    String jsonContent;
    serializeJson(doc, jsonContent);
    
    // Verwende die sichere Dateischreibfunktion
    return fileOps.writeFile("/data/settings.json", jsonContent);
}

bool loadSettingsFromFile() {
    if (!LittleFS.exists("/data/settings.json")) {
        return false;
    }

    // Lese die Datei mit der sicheren Lesefunktion
    String jsonContent = fileOps.readFile("/data/settings.json");
    if (jsonContent.isEmpty()) {
        debug(F("Fehler beim Lesen der Einstellungen aus Datei"));
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonContent);
    
    if (error) {
        debug(String(F("JSON-Parsing-Fehler beim Laden der Einstellungen: ")) + error.c_str());
        return false;
    }

    // Einstellungen aus JSON extrahieren
    moodUpdateInterval = doc["moodInterval"] | DEFAULT_MOOD_UPDATE_INTERVAL;
    dhtUpdateInterval = doc["dhtInterval"] | DEFAULT_DHT_READ_INTERVAL;
    headlines_per_source = doc["headlinesPS"] | 1;
    autoMode = doc["autoMode"] | true;
    lightOn = doc["lightOn"] | true;
    manualBrightness = doc["manBright"] | DEFAULT_LED_BRIGHTNESS;
    manualColor = doc["manColor"] | pixels.Color(255, 255, 255);

    // WiFi-Einstellungen
    wifiSSID = doc["wifiSSID"] | "";
    wifiPassword = doc["wifiPass"] | "";
    wifiConfigured = doc["wifiConfigured"] | false;

    // Erweiterte Einstellungen
    apiUrl = doc["apiUrl"] | DEFAULT_NEWS_API_URL;
    mqttServer = doc["mqttServer"] | "";
    mqttUser = doc["mqttUser"] | "";
    mqttPassword = doc["mqttPass"] | "";
    dhtPin = doc["dhtPin"] | DEFAULT_DHT_PIN;
    dhtEnabled = doc["dhtEnabled"] | true;
    ledPin = doc["ledPin"] | DEFAULT_LED_PIN;
    numLeds = doc["numLeds"] | DEFAULT_NUM_LEDS;
    mqttEnabled = doc["mqttEnabled"] | false;

    // Benutzerdefinierte Farben laden
    for (int i = 0; i < 5; i++) {
        String colorKey = "color" + String(i);
        if (doc[colorKey].is<uint32_t>()) {
            customColors[i] = doc[colorKey].as<uint32_t>();
        }
    }

    return true;
}
// === Einstellungen speichern/laden ===
void saveSettings()
{

    // Erst in JSON-Datei speichern (neue Methode)
    saveSettingsToFile();
    // Dann in Preferences speichern (bestehende Methode)
    preferences.begin("moodlight", false);

    // Speichere allgemeine Einstellungen
    preferences.putULong("moodInterval", moodUpdateInterval);
    preferences.putULong("dhtInterval", dhtUpdateInterval);
    preferences.putBool("autoMode", autoMode);
    preferences.putBool("lightOn", lightOn);
    preferences.putUChar("manBright", manualBrightness);
    preferences.putUInt("manColor", manualColor);
    preferences.putInt("headlinesPS", headlines_per_source);

    // Speichere WiFi-Einstellungen
    preferences.putString("wifiSSID", wifiSSID);
    preferences.putString("wifiPass", wifiPassword);
    preferences.putBool("wifiConfigured", wifiConfigured);

    // Speichere erweiterte Einstellungen
    preferences.putString("apiUrl", apiUrl);
    preferences.putString("mqttServer", mqttServer);
    preferences.putString("mqttUser", mqttUser);
    preferences.putString("mqttPass", mqttPassword);
    preferences.putInt("dhtPin", dhtPin);
    preferences.putBool("dhtEnabled", dhtEnabled);
    preferences.putInt("ledPin", ledPin);
    preferences.putInt("numLeds", numLeds);
    preferences.putBool("mqttEnabled", mqttEnabled);

    // Speichere benutzerdefinierte Farben
    preferences.putUInt("color0", customColors[0]);
    preferences.putUInt("color1", customColors[1]);
    preferences.putUInt("color2", customColors[2]);
    preferences.putUInt("color3", customColors[3]);
    preferences.putUInt("color4", customColors[4]);

    preferences.end();
    debug(F("Einstellungen in Preferences gespeichert."));

}

void loadSettings()
{
    // Zuerst versuchen wir aus der JSON-Datei zu laden
    bool fileLoadSuccess = loadSettingsFromFile();

    // Wenn das fehlschlägt, versuchen wir aus Preferences zu laden
    if (!fileLoadSuccess)
    {
        debug(F("Lade Einstellungen aus Preferences als Fallback..."));
        preferences.begin("moodlight", true); // read-only

        // Lade allgemeine Einstellungen
        moodUpdateInterval = DEFAULT_MOOD_UPDATE_INTERVAL;
        dhtUpdateInterval = DEFAULT_DHT_READ_INTERVAL;
        headlines_per_source = 1;
        autoMode = true;
        lightOn = true;
        manualBrightness = DEFAULT_LED_BRIGHTNESS;
        manualColor = pixels.Color(255, 255, 255);

        // Lade WiFi-Einstellungen
        wifiSSID = preferences.getString("wifiSSID", "");
        wifiPassword = preferences.getString("wifiPass", "");
        wifiConfigured = preferences.getBool("wifiConfigured", false);

        // Lade erweiterte Einstellungen
        apiUrl = preferences.getString("apiUrl", DEFAULT_NEWS_API_URL);
        mqttServer = preferences.getString("mqttServer", "");
        mqttUser = preferences.getString("mqttUser", "");
        mqttPassword = preferences.getString("mqttPass", "");
        dhtPin = preferences.getInt("dhtPin", DEFAULT_DHT_PIN);
        dhtEnabled = preferences.getBool("dhtEnabled", true);
        ledPin = preferences.getInt("ledPin", DEFAULT_LED_PIN);
        numLeds = preferences.getInt("numLeds", DEFAULT_NUM_LEDS);
        mqttEnabled = preferences.getBool("mqttEnabled", false);

        // Benutzerdefinierte Farben laden
        customColors[0] = preferences.getUInt("color0", 0xFF0000);
        customColors[1] = preferences.getUInt("color1", 0xFFA500);
        customColors[2] = preferences.getUInt("color2", 0x1E90FF);
        customColors[3] = preferences.getUInt("color3", 0x545DF0);
        customColors[4] = preferences.getUInt("color4", 0x8A2BE2);
        preferences.end();

        // Nach dem Laden aus Preferences, in Datei speichern für den nächsten Ladevorgang
        saveSettingsToFile();
        debug(F("Einstellungen aus Preferences in JSON-Datei migriert"));
    }

    // Log der geladenen Einstellungen
    debug(F("Einstellungen geladen:"));
    debug("  Mood Interval: " + String(moodUpdateInterval / 1000) + "s");
    debug("  DHT Interval: " + String(dhtUpdateInterval / 1000) + "s");
    debug(String(F("  DHT Enabled: ")) + (dhtEnabled ? "ja" : "nein"));
    debug("  Headlines pro Quelle: " + String(headlines_per_source));
    debug(String(F("  AutoMode: ")) + (autoMode ? "true" : "false"));
    debug(String(F("  LightOn: ")) + (lightOn ? "true" : "false"));
    debug(String(F("  WiFi konfiguriert: ")) + (wifiConfigured ? "ja" : "nein"));

    if (wifiConfigured)
    {
        debug(String(F("  WiFi SSID: ")) + wifiSSID);
    }

    debug(String(F("  API URL: ")) + apiUrl);
    debug(String(F("  MQTT Enabled: ")) + (mqttEnabled ? "ja" : "nein"));
    if (mqttEnabled)
    {
        debug(String(F("  MQTT Server: ")) + mqttServer);
    }
}

bool safeWiFiConnect(const String &ssid, const String &password, unsigned long timeout = 15000)
{
    debug(String(F("Safely connecting to WiFi: ")) + ssid);

    // Always disconnect first
    WiFi.disconnect(true);
    delay(100);

    // Start connection with full power
    WiFi.setSleep(false);
    WiFi.begin(ssid.c_str(), password.c_str());

    // Non-blocking wait with timeout and yield
    unsigned long startTime = millis();
    int dotCounter = 0;

    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - startTime > timeout)
        {
            debug(F("WiFi connection timeout"));
            return false;
        }

        delay(50);
        if (++dotCounter % 10 == 0)
        {
            debug(".");
            yield(); // Explicit yield to feed watchdog
        }
    }

    // Explicitly disable power save after connection
    esp_wifi_set_ps(WIFI_PS_NONE);
    debug(String(F("WiFi connected! IP: ")) + WiFi.localIP().toString());
    return true;
}

// === WiFi AP-Modus starten ===
void startAPMode()
{
    debug(F("Starte Access Point Modus..."));

    // LED Animation für AP-Modus
    pixels.fill(pixels.Color(255, 255, 0)); // Gelb
    pixels.show();
    delay(500);

    // AP-Modus konfigurieren
    WiFi.mode(WIFI_AP);
    WiFi.softAP(DEFAULT_AP_NAME, DEFAULT_AP_PASSWORD);

    IPAddress IP = CAPTIVE_PORTAL_IP;
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(IP, IP, subnet);

    delay(500);

    // DNS-Server für Captive Portal starten
    dnsServer.start(DNS_PORT, "*", IP);
    isInConfigMode = true;

    debug("AP gestartet mit IP " + WiFi.softAPIP().toString());
    debug(String(F("SSID: ")) + DEFAULT_AP_NAME);

    // Captive Portal Request Handler hinzufügen
    server.addHandler(new CaptiveRequestHandler());

    // Status LED für AP-Modus
    setStatusLED(5);

    // Merke Zeit für Timeout
    apModeStartTime = millis();
}

// === WiFi Station-Modus starten ===
bool startWiFiStation()
{
    if (!wifiConfigured || wifiSSID.isEmpty())
    {
        debug(F("Keine WiFi-Konfiguration vorhanden."));
        return false;
    }

    debug(String(F("Verbinde mit WiFi: ")) + wifiSSID);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    // Use the safer WiFi connect method
    bool connected = safeWiFiConnect(wifiSSID, wifiPassword, 15000);

    if (connected)
    {
        wifiWasConnected = true;
        
        // Synchronize time via NTP after successful WiFi connection
        initTime();
    }
    else
    {
        debug(F("WiFi Verbindungs-Timeout! Fahre trotzdem fort."));
    }

    return connected;
}

// === WiFi scannen für Setup-Seite ===
String scanWiFiNetworks()
{
    debug(F("Scanne WiFi-Netzwerke..."));

    int n = WiFi.scanNetworks();
    debug(String(F("Scan abgeschlossen: ")) + n + F(" Netzwerke gefunden"));

    JsonDocument doc; // Größer für viele WiFi-Netzwerke
    JsonArray networks = doc["networks"].to<JsonArray>();

    for (int i = 0; i < n; i++)
    {
        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        network["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

        // Kurze Pause zwischen Einträgen
        delay(10);
    }

    String jsonResult;
    serializeJson(doc, jsonResult);
    return jsonResult;
}

// Hilfsfunktion zum Hinzufügen eines Feeds
void addFeed(JsonArray &feeds, const char *name, const char *url)
{
    JsonObject feed = feeds.add<JsonObject>();
    feed["name"] = name;
    feed["url"] = url;
    feed["enabled"] = true;
}

// Standardwerte für RSS-Feeds speichern
void saveDefaultRssFeeds()
{
    JsonDocument doc;
    JsonArray feeds = doc["feeds"].to<JsonArray>();

    // Standardfeeds aus app.py
    addFeed(feeds, "Zeit", "https://newsfeed.zeit.de/index");
    addFeed(feeds, "Tagesschau", "https://www.tagesschau.de/xml/rss2");
    addFeed(feeds, "Sueddeutsche", "https://rss.sueddeutsche.de/rss/Alles");
    addFeed(feeds, "FAZ", "https://www.faz.net/rss/aktuell/");
    addFeed(feeds, "Die Welt", "https://www.welt.de/feeds/latest.rss");
    addFeed(feeds, "Handelsblatt", "https://www.handelsblatt.com/contentexport/feed/schlagzeilen");
    addFeed(feeds, "n-tv", "https://www.n-tv.de/rss");
    addFeed(feeds, "Focus", "https://rss.focus.de/fol/XML/rss_folnews.xml");
    addFeed(feeds, "Stern", "https://www.stern.de/feed/standard/alle-nachrichten/");
    addFeed(feeds, "Telekom", "https://www.t-online.de/feed.rss");
    addFeed(feeds, "TAZ", "https://taz.de/!p4608;rss/");
    addFeed(feeds, "Deutschlandfunk", "https://www.deutschlandfunk.de/nachrichten-100.rss");

    // JSON in Datei speichern
    if (!LittleFS.exists("/data"))
    {
        LittleFS.mkdir("/data");
    }

    File file = LittleFS.open("/data/feeds.json", "w");
    if (!file)
    {
        debug(F("Fehler beim Erstellen der RSS-Feed-Datei"));
        return;
    }

    serializeJson(doc, file);
    file.close();
    debug(F("Standard-RSS-Feeds gespeichert"));
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
    if (!LittleFS.exists("/data/feeds.json")) {
        debug(F("Creating default feeds.json..."));
        saveDefaultRssFeeds();
    }

    if (!LittleFS.exists("/data/stats.csv")) {
        debug(F("Creating default stats.csv..."));
    }

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
    doc["mqtt"] = mqttEnabled && mqtt.isConnected() ? "Connected" : (mqttEnabled ? "Disconnected" : "Disabled");

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
    doc["sentiment"] = String(lastSentimentScore, 2) + " (" + lastSentimentCategory + ")";
    doc["dhtEnabled"] = dhtEnabled;
    doc["dht"] = isnan(lastTemp) ? "N/A" : String(lastTemp, 1) + "°C / " + String(lastHum, 1) + "%";
    doc["mode"] = autoMode ? "Auto" : "Manual";
    doc["lightOn"] = lightOn;
    doc["brightness"] = manualBrightness;
    doc["headlines"] = headlines_per_source;
    doc["version"] = SOFTWARE_VERSION;

    // LED-Farbe als Hex
    uint32_t currentColor;
    if (autoMode)
    {
        currentLedIndex = constrain(currentLedIndex, 0, 4);
        ColorDefinition color = getColorDefinition(currentLedIndex);
        currentColor = pixels.Color(color.r, color.g, color.b);
    }
    else
    {
        currentColor = manualColor;
    }

    uint8_t r = (currentColor >> 16) & 0xFF;
    uint8_t g = (currentColor >> 8) & 0xFF;
    uint8_t b = currentColor & 0xFF;
    char hexColor[8];
    sprintf(hexColor, "#%02X%02X%02X", r, g, b);
    doc["ledColor"] = hexColor;

    // Status-LED Info
    if (statusLedMode != 0)
    {
        char statusLedColor[8] = "#000000";
        switch (statusLedMode)
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
        doc["statusLedMode"] = statusLedMode;
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
void handleApiDeleteDataPoint() {
    if (!server.hasArg("timestamp")) {
        server.send(400, "application/json", "{\"error\":\"Missing timestamp parameter\"}");
        return;
    }

    String targetTimestamp = server.arg("timestamp");
    debug(String(F("Versuche Datenpunkt zu löschen: ")) + targetTimestamp);

    if (!LittleFS.exists("/data/stats.csv")) {
        server.send(404, "application/json", "{\"error\":\"Keine Statistikdaten vorhanden\"}");
        return;
    }

    // Open original file
    File sourceFile = LittleFS.open("/data/stats.csv", "r");
    if (!sourceFile) {
        server.send(500, "application/json", "{\"error\":\"Fehler beim Öffnen der Statistikdatei\"}");
        return;
    }

    // Create temporary file
    File tempFile = LittleFS.open("/data/stats_temp.csv", "w");
    if (!tempFile) {
        sourceFile.close();
        server.send(500, "application/json", "{\"error\":\"Fehler beim Erstellen der temporären Datei\"}");
        return;
    }

    // Copy header
    String header = sourceFile.readStringUntil('\n');
    tempFile.println(header);

    // Copy all lines except the one to delete
    bool foundMatch = false;
    while (sourceFile.available()) {
        String line = sourceFile.readStringUntil('\n');
        if (line.length() > 0) {
            // Check if this is the line to delete
            int firstComma = line.indexOf(',');
            if (firstComma > 0) {
                String lineTimestamp = line.substring(0, firstComma);
                if (lineTimestamp != targetTimestamp) {
                    tempFile.println(line);
                } else {
                    foundMatch = true;
                    debug(String(F("Datenpunkt gefunden und übersprungen: ")) + lineTimestamp);
                }
            } else {
                // If no comma found, preserve the line (could be malformed)
                tempFile.println(line);
            }
        }
    }

    // Close both files
    sourceFile.close();
    tempFile.close();

    // Replace original with temp file
    if (foundMatch) {
        if (LittleFS.remove("/data/stats.csv")) {
            if (LittleFS.rename("/data/stats_temp.csv", "/data/stats.csv")) {
                server.send(200, "application/json", "{\"success\":true,\"message\":\"Datenpunkt erfolgreich gelöscht\"}");
                debug(F("Datenpunkt erfolgreich gelöscht"));
            } else {
                server.send(500, "application/json", "{\"error\":\"Fehler beim Umbenennen der temporären Datei\"}");
                debug(F("Fehler beim Umbenennen der temporären Datei"));
            }
        } else {
            LittleFS.remove("/data/stats_temp.csv");
            server.send(500, "application/json", "{\"error\":\"Fehler beim Löschen der Original-Datei\"}");
            debug(F("Fehler beim Löschen der Original-Datei"));
        }
    } else {
        LittleFS.remove("/data/stats_temp.csv");
        server.send(404, "application/json", "{\"error\":\"Datenpunkt nicht gefunden\"}");
        debug(F("Datenpunkt nicht gefunden"));
    }
}

// Handle resetting all stats data - FIXED
void handleApiResetAllData() {
    debug(F("Versuche alle Statistikdaten zurückzusetzen"));

    if (!LittleFS.exists("/data/stats.csv")) {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Keine Daten vorhanden\"}");
        return;
    }

    // Delete the file and create a new one with just the header
    if (!LittleFS.remove("/data/stats.csv")) {
        server.send(500, "application/json", "{\"error\":\"Fehler beim Löschen der Datei\"}");
        return;
    }
    
    File file = LittleFS.open("/data/stats.csv", "w");
    if (!file) {
        server.send(500, "application/json", "{\"error\":\"Fehler beim Erstellen der neuen Datei\"}");
        return;
    }

    // Write header - CHANGED: Removed category from header
    if (file.println("timestamp,sentiment")) {
        file.close();
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Alle Daten wurden zurückgesetzt\"}");
        debug(F("Alle Statistikdaten wurden zurückgesetzt"));
    } else {
        file.close();
        server.send(500, "application/json", "{\"error\":\"Fehler beim Schreiben des Headers\"}");
        debug(F("Fehler beim Schreiben des Headers"));
    }
}

  // Liste der Archive abrufen
  void getArchivesList(JsonArray &archives) {
    if (!LittleFS.exists("/data/archives")) {
      return;
    }
    
    File root = LittleFS.open("/data/archives");
    if (!root || !root.isDirectory()) {
      return;
    }
    
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory() && String(file.name()).endsWith(".csv")) {
        JsonObject archive = archives.add<JsonObject>();
        archive["name"] = String(file.name());
        archive["size"] = file.size();
        
        // Versuche Jahr/Monat aus dem Dateinamen zu parsen (Format: stats_YYYY_MM.csv)
        String filename = String(file.name());
        int startPos = filename.indexOf("stats_");
        if (startPos >= 0) {
          startPos += 6; // "stats_" überspringen
          int year = filename.substring(startPos, startPos + 4).toInt();
          int month = filename.substring(startPos + 5, startPos + 7).toInt();
          
          if (year > 0 && month > 0 && month <= 12) {
            char dateStr[32];
            sprintf(dateStr, "%04d-%02d", year, month);
            archive["period"] = dateStr;
          }
        }
        
        // Anzahl der Datensätze ermitteln
        int recordCount = 0;
        String path = "/data/archives/" + String(file.name());
        File archiveFile = LittleFS.open(path, "r");
        if (archiveFile) {
          // Header überspringen
          archiveFile.readStringUntil('\n');
          
          while (archiveFile.available()) {
            String line = archiveFile.readStringUntil('\n');
            if (line.length() > 0) {
              recordCount++;
            }
          }
          archiveFile.close();
        }
        archive["records"] = recordCount;
      }
      file = root.openNextFile();
    }
  }
  
// Funktion zur Archivierung von Daten älter als 90 Tage
void archiveOldData() {
    // Prüfe zuerst, ob genügend Speicher verfügbar ist
    if (!memMonitor.checkHeapBefore("Archivierung", 30000)) {
        debug(F("Zu wenig Speicher für Archivierung, überspringe..."));
        return;
    }

    if (!LittleFS.exists("/data/stats.csv")) {
        debug(F("Keine Statistikdaten zum Archivieren vorhanden"));
        return;
    }
    
    // Heutiges Datum für das Archiv
    time_t now;
    time(&now);
    
    // Prüfen, ob Zeit verfügbar, ansonsten Epoche-Zeit verwenden
    if (now < 1600000000) { // Zeitstempel vor 2020
        debug(F("Zeit nicht synchronisiert für Archivierung, verwende Epoche"));
    }
    
    time_t cutoffDate = now - (90 * 24 * 3600); // 90 Tage zurück
    
    // Archivverzeichnis erstellen falls nötig
    if (!LittleFS.exists("/data/archives")) {
        if (!LittleFS.mkdir("/data/archives")) {
            debug(F("Fehler beim Erstellen des Archivverzeichnisses"));
            return;
        }
    }
    
    // Statistikdatei öffnen mit sicherer Operation
    String statsContent = fileOps.readFile("/data/stats.csv");
    if (statsContent.isEmpty()) {
        debug(F("Fehler beim Lesen der Statistikdatei oder Datei leer"));
        return;
    }
    
    // Archivdatei erstellen - Monat-Jahr als Dateiname
    char archiveFileName[64];
    struct tm *timeinfo = localtime(&now);
    strftime(archiveFileName, sizeof(archiveFileName), "/data/archives/stats_%Y_%m.csv", timeinfo);
    
    // Verarbeite die CSV-Daten
    String header;
    String currentData;
    String archiveData;
    
    // Teile den Inhalt in Zeilen auf
    int lineStart = 0;
    int lineEnd = statsContent.indexOf('\n');
    if (lineEnd < 0) {
        debug(F("Keine vollständigen Zeilen in Statistikdatei gefunden"));
        return;
    }
    
    // Header extrahieren
    header = statsContent.substring(lineStart, lineEnd + 1);
    lineStart = lineEnd + 1;
    
    // Prüfen, ob Archiv bereits existiert
    bool archiveExists = LittleFS.exists(archiveFileName);
    
    int currentCount = 0;
    int archivedCount = 0;
    
    // Zeilen verarbeiten
    while (lineStart < statsContent.length()) {
        lineEnd = statsContent.indexOf('\n', lineStart);
        if (lineEnd < 0) lineEnd = statsContent.length();
        
        String line = statsContent.substring(lineStart, lineEnd);
        
        // Timestamp extrahieren
        int firstComma = line.indexOf(',');
        if (firstComma > 0) {
            String timestampStr = line.substring(0, firstComma);
            time_t lineTime = atol(timestampStr.c_str());
            
            if (lineTime >= cutoffDate) {
                // In aktueller Datei behalten
                currentData += line + "\n";
                currentCount++;
            } else {
                // Ins Archiv verschieben
                archiveData += line + "\n";
                archivedCount++;
            }
        }
        
        lineStart = lineEnd + 1;
    }
    
    // Schreibe Archivdaten
    if (archivedCount > 0) {
        String archiveContent = archiveExists ? archiveData : header + archiveData;
        
        if (fileOps.writeFile(archiveFileName, archiveContent)) {
            debug(String(F("Archiviert: ")) + archivedCount + F(" Datenpunkte in ") + archiveFileName);
        } else {
            debug(String(F("Fehler beim Schreiben des Archivs: ")) + archiveFileName);
            return;
        }
    }
    
    // Schreibe aktuelle Daten zurück
    if (fileOps.writeFile("/data/stats.csv", header + currentData)) {
        debug(String(F("Archivierung abgeschlossen. ")) + currentCount + 
              F(" Datenpunkte behalten, ") + archivedCount + F(" Datenpunkte archiviert."));
    } else {
        debug(F("Fehler beim Aktualisieren der Statistikdatei nach Archivierung"));
    }
}

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

  int countStatsRecords();
  
  // API-Endpunkt für Speicherinformationen
  void handleApiStorageInfo() {
    uint64_t totalBytes, usedBytes, freeBytes;
    getStorageInfo(totalBytes, usedBytes, freeBytes);
    
    JsonDocument doc;
    doc["total"] = totalBytes;
    doc["used"] = usedBytes;
    doc["free"] = freeBytes;
    doc["percentUsed"] = (totalBytes > 0) ? ((float)usedBytes / totalBytes * 100) : 0;
    
    // Statistikdateigröße abrufen
    if (LittleFS.exists("/data/stats.csv")) {
      File statsFile = LittleFS.open("/data/stats.csv", "r");
      if (statsFile) {
        doc["statsSize"] = statsFile.size();
        statsFile.close();
      }
    }
    
    // Anzahl der Datensätze
    int recordCount = countStatsRecords();
    doc["recordCount"] = recordCount;
    
    // Archivinformationen
    JsonArray archives = doc["archives"].to<JsonArray>();
    getArchivesList(archives);
    
    char* jsonBuffer = jsonPool.acquire();
    size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    server.send(200, "application/json", jsonBuffer);
    jsonPool.release(jsonBuffer);
  }
  
  // Anzahl der Datensätze in der Statistikdatei zählen
  int countStatsRecords() {
    if (!LittleFS.exists("/data/stats.csv")) {
      return 0;
    }
    
    File statsFile = LittleFS.open("/data/stats.csv", "r");
    if (!statsFile) {
      return 0;
    }
    
    // Header überspringen
    statsFile.readStringUntil('\n');
    
    int count = 0;
    while (statsFile.available()) {
      String line = statsFile.readStringUntil('\n');
      if (line.length() > 0) {
        count++;
      }
    }
    
    statsFile.close();
    return count;
  }

  // Archiv-Verwaltungs-Endpunkte initialisieren
void setupArchiveEndpoints() {
    // Archive auflisten
    server.on("/api/archives", HTTP_GET, []() {
      JsonDocument doc;
      JsonArray archives = doc["archives"].to<JsonArray>();
      getArchivesList(archives);
      
      char* jsonBuffer = jsonPool.acquire();
      size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
      server.send(200, "application/json", jsonBuffer);
      jsonPool.release(jsonBuffer);
    });
    
 // Fügen Sie diese API-Endpunkte in Ihre setupWebServer()-Funktion ein:

// Metrics endpoint für die Visualisierung der Systemdaten
server.on("/api/system/metrics", HTTP_GET, []() {
    JsonDocument doc;
    
    // Memory metrics
    doc["heap"] = ESP.getFreeHeap();
    doc["maxBlock"] = ESP.getMaxAllocHeap();
    doc["minHeap"] = memMonitor.getLowestHeap();
    
    // File system metrics
    uint64_t total, used, free;
    getStorageInfo(total, used, free);
    doc["fsTotal"] = (unsigned long)total;
    doc["fsUsed"] = (unsigned long)used;
    doc["fsFree"] = (unsigned long)(total - used);
    doc["fsPercent"] = (float)used * 100.0 / total;
    
    // System uptime
    doc["uptime"] = millis() / 1000;
    
    // WiFi metrics
    doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
    if (WiFi.status() == WL_CONNECTED) {
        doc["rssi"] = WiFi.RSSI();
        doc["ssid"] = WiFi.SSID();
        doc["channel"] = WiFi.channel();
        doc["ip"] = WiFi.localIP().toString();
    }
    
    // MQTT metrics
    doc["mqttEnabled"] = mqttEnabled;
    doc["mqttConnected"] = mqttEnabled && mqtt.isConnected();
    
    // CPU temperature (ESP32 specific)
    doc["temperature"] = temperatureRead();
    
    // Sentiment metrics
    doc["sentiment"] = lastSentimentScore;
    doc["sentimentCategory"] = lastSentimentCategory;
    
    // System status assessments
    bool memoryOk = ESP.getFreeHeap() > 30000;
    bool fragmentationOk = (float)ESP.getMaxAllocHeap() / ESP.getFreeHeap() > 0.7;
    bool filesystemOk = ((float)used * 100.0 / total) < 80.0;
    bool wifiOk = WiFi.status() == WL_CONNECTED && WiFi.RSSI() > -80;
    bool mqttOk = !mqttEnabled || (mqttEnabled && mqtt.isConnected());
    
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

// Endpunkt für vollständige Systemdiagnose
server.on("/api/system/diagnose", HTTP_GET, []() {
    debug(F("Vollständige Systemdiagnose angefordert"));
    
    // Memory-Diagnose
    memMonitor.diagnose();
    
    // Netzwerk-Diagnose
    netDiag.fullAnalysis();
    
    // System-Gesundheitscheck
    sysHealth.performFullCheck();
    
    // Dateisystem-Info
    fileOps.listDir("/", 1);
    
    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = F("Diagnose abgeschlossen");
    
    char* jsonBuffer = jsonPool.acquire();
    size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    server.send(200, "application/json", jsonBuffer);
    jsonPool.release(jsonBuffer);
});

// Endpunkt für Dateisystemreinigung
server.on("/api/system/cleanup", HTTP_GET, []() {
    debug(F("Dateisystem-Bereinigung angefordert"));
    
    int cleanedFiles = 0;
    
    // Temporäre Dateien löschen
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
    
    // Temporäre Dateien in /data
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

server.on("/diagnostics", HTTP_GET, []() {
    handleStaticFile("/diagnostics.html");
});

    // Archivdaten abrufen
    server.on("/api/archives/data", HTTP_GET, []() {
        if (!server.hasArg("name")) {
          server.send(400, "application/json", "{\"error\":\"Archivname fehlt\"}");
          return;
        }
        
        String archiveName = server.arg("name");
        // Sicherheitscheck um Directory Traversal zu verhindern
        if (archiveName.indexOf("..") >= 0 || archiveName.indexOf("/") >= 0) {
          server.send(400, "application/json", "{\"error\":\"Ungültiger Archivname\"}");
          return;
        }
        
        String path = "/data/archives/" + archiveName;
        if (!LittleFS.exists(path)) {
          server.send(404, "application/json", "{\"error\":\"Archiv nicht gefunden\"}");
          return;
        }
        
        File archiveFile = LittleFS.open(path, "r");
        if (!archiveFile) {
          server.send(500, "application/json", "{\"error\":\"Fehler beim Öffnen des Archivs\"}");
          return;
        }
        
        // Benötigte Dokumentgröße berechnen
        size_t fileSize = archiveFile.size();
        size_t jsonCapacity = fileSize * 2; // Schätzung: JSON ist etwa 2x so groß wie CSV
        if (jsonCapacity < 4096) jsonCapacity = 4096; // Minimale Größe
        if (jsonCapacity > 32768) jsonCapacity = 32768; // Begrenzung auf 32KB, um Speicherprobleme zu vermeiden
        
        JsonDocument doc;
        JsonArray data = doc["data"].to<JsonArray>();
        
        // Header überspringen
        String header = archiveFile.readStringUntil('\n');
        
        // CSV-Daten lesen
        while (archiveFile.available()) {
          String line = archiveFile.readStringUntil('\n');
          if (line.length() > 0) {
            int firstComma = line.indexOf(',');
            
            if (firstComma > 0) {
              String timestampStr = line.substring(0, firstComma);
              String sentimentStr = line.substring(firstComma + 1);
              
              long timestamp = atol(timestampStr.c_str());
              float sentiment = atof(sentimentStr.c_str());
              
              // Determine category from sentiment value
              String category;
              if (sentiment >= 0.30) category = "sehr positiv";
              else if (sentiment >= 0.10) category = "positiv";
              else if (sentiment >= -0.20) category = "neutral";
              else if (sentiment >= -0.50) category = "negativ";
              else category = "sehr negativ";
              
              JsonObject entry = data.add<JsonObject>();
              entry["timestamp"] = timestamp;
              entry["sentiment"] = sentiment;
              entry["category"] = category;  // Add category dynamically
            }
          }
        }
        
        archiveFile.close();
        
        char* jsonBuffer = jsonPool.acquire();
        size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
        server.send(200, "application/json", jsonBuffer);
        jsonPool.release(jsonBuffer);
      });
    
    // Archiv löschen
    server.on("/api/archives/delete", HTTP_GET, []() {
      if (!server.hasArg("name")) {
        server.send(400, "application/json", "{\"error\":\"Archivname fehlt\"}");
        return;
      }
      
      String archiveName = server.arg("name");
      // Sicherheitscheck um Directory Traversal zu verhindern
      if (archiveName.indexOf("..") >= 0 || archiveName.indexOf("/") >= 0) {
        server.send(400, "application/json", "{\"error\":\"Ungültiger Archivname\"}");
        return;
      }
      
      String path = "/data/archives/" + archiveName;
      if (!LittleFS.exists(path)) {
        server.send(404, "application/json", "{\"error\":\"Archiv nicht gefunden\"}");
        return;
      }
      
      if (LittleFS.remove(path)) {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Archiv gelöscht\"}");
        debug(String(F("Archiv gelöscht: ")) + archiveName);
      } else {
        server.send(500, "application/json", "{\"error\":\"Fehler beim Löschen des Archivs\"}");
        debug(String(F("Fehler beim Löschen des Archivs: ")) + archiveName);
      }
    });
    
    // Archivierungsprozess manuell starten
    server.on("/api/archives/process", HTTP_GET, []() {
      archiveOldData();
      server.send(200, "application/json", "{\"success\":true,\"message\":\"Archivierungsprozess abgeschlossen\"}");
    });
  }

  void handleApiStats() {
    if (!LittleFS.exists("/data/stats.csv")) {
        server.send(404, "application/json", "{\"error\":\"Keine Statistikdaten vorhanden\"}");
        return;
    }

    // Statt alle Datenpunkte zu zählen, begrenzen wir die Anzahl von vornherein
    const int MAX_POINTS = 1000; // Stark reduziert von 1000 auf 250
    
    File file = LittleFS.open("/data/stats.csv", "r");
    if (!file) {
        server.send(500, "application/json", "{\"error\":\"Fehler beim Öffnen der Statistikdatei\"}");
        return;
    }
    
    // Header überspringen
    String header = file.readStringUntil('\n');
    
    // Stream-basierte Verarbeitung mit kleinen Puffern
    // Wir erstellen das JSON-Dokument direkt während des Lesens
    String jsonStart = "{\"data\":[";
    String jsonEnd = "]}";
    String jsonResponse = jsonStart;
    
    int pointCount = 0;
    while (file.available() && pointCount < MAX_POINTS) {
        String line = file.readStringUntil('\n');
        if (line.length() > 0) {
            int firstComma = line.indexOf(',');
            
            if (firstComma > 0) {
                // Manuelle JSON-Erstellung statt JsonDocument für bessere Speichereffizienz
                if (pointCount > 0) {
                    jsonResponse += ",";
                }
                
                String timestampStr = line.substring(0, firstComma);
                String sentimentStr = line.substring(firstComma + 1);

                long timestamp = atol(timestampStr.c_str());
                float sentiment = atof(sentimentStr.c_str());
                
                // Kategorie bestimmen
                String category;
                if (sentiment >= 0.30) category = "sehr positiv";
                else if (sentiment >= 0.10) category = "positiv";
                else if (sentiment >= -0.20) category = "neutral";
                else if (sentiment >= -0.50) category = "negativ";
                else category = "sehr negativ";

                // Direktes Zusammenstellen des JSON-Objekts als String
                jsonResponse += "{\"timestamp\":";
                jsonResponse += timestamp;
                jsonResponse += ",\"sentiment\":";
                jsonResponse += String(sentiment, 2); // 2 Dezimalstellen
                jsonResponse += ",\"category\":\"";
                jsonResponse += category;
                jsonResponse += "\"}";
                
                pointCount++;
            }
        }
    }
    
    // Anzahl der Datensätze weiterzählen für Metadaten
    int totalPoints = pointCount;
    while (file.available()) {
        file.readStringUntil('\n');
        totalPoints++;
    }
    
    file.close();
    
    // JSON fertigstellen
    jsonResponse += jsonEnd;
    
    // Sende die Antwort direkt ohne ArduinoJSON
    server.send(200, "application/json", jsonResponse);
    debug(String(F("Stats-JSON erfolgreich gesendet: ")) + jsonResponse.length() + F(" Bytes, ") + pointCount + F(" von ") + totalPoints + F(" Datenpunkten"));
}

// RSS-Feeds laden
void loadRssFeeds()
{
    if (!LittleFS.exists("/data/feeds.json"))
    {
        debug(F("Keine gespeicherten RSS-Feeds gefunden, verwende Standardwerte"));
        saveDefaultRssFeeds();
        return;
    }

    File file = LittleFS.open("/data/feeds.json", "r");
    if (!file)
    {
        debug(F("Fehler beim Öffnen der RSS-Feed-Datei"));
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        debug(String(F("JSON Parsing Fehler: ")) + error.c_str());
        return;
    }

    debug(F("RSS-Feeds erfolgreich geladen"));
}

bool sendRSSConfigToAPI()
{
    if (!LittleFS.exists("/data/feeds.json"))
    {
        debug(F("Keine RSS-Feed-Konfiguration zum Senden vorhanden"));
        return false;
    }

    File file = LittleFS.open("/data/feeds.json", "r");
    if (!file)
    {
        debug(F("Fehler beim Öffnen der Feed-Datei"));
        return false;
    }

    String feedConfig = file.readString();
    file.close();

    // Stelle sicher, dass die JSON-Daten valide sind
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, feedConfig);
    if (error)
    {
        debug(String(F("Feed-JSON-Parsing-Fehler: ")) + error.c_str());
        return false;
    }

    // Nur aktivierte Feeds senden
    JsonArray feeds = doc["feeds"].to<JsonArray>();
    JsonDocument filteredDoc;
    
    for (JsonObject feed : feeds) {
        if (feed["enabled"].as<bool>()) {
            String name = feed["name"].as<String>();
            String url = feed["url"].as<String>();
            filteredDoc[name] = url; // Direkt ins Dokument schreiben
        }
    }

    // Sende an die API
    HTTPClient http;
    http.setReuse(false);
    http.setUserAgent("MoodlightClient/1.0");

    // Parse die Basis-URL
    String apiBaseUrl = apiUrl;

    // Wenn die URL einen Pfad hat, extrahiere die Basis
    int pathStart = apiUrl.indexOf('/', 8); // Nach http(s)://
    if (pathStart > 0)
    {
        apiBaseUrl = apiUrl.substring(0, pathStart);
    }

    // Feed-Konfigurationsendpunkt
    String configUrl = apiBaseUrl + "/api/feedconfig";

    debug(String(F("Sende RSS-Konfiguration an: ")) + configUrl);

    if (http.begin(wifiClientHTTP, configUrl))
    {
        http.addHeader("Content-Type", "application/json");

        String requestBody;
        serializeJson(filteredDoc, requestBody);

        int httpCode = http.POST(requestBody);

        if (httpCode == HTTP_CODE_OK)
        {
            debug(F("RSS-Konfiguration erfolgreich gesendet"));
            http.end();
            return true;
        }
        else
        {
            debug(String(F("RSS-Konfiguration senden fehlgeschlagen: ")) + httpCode);
            http.end();
            return false;
        }
    }
    else
    {
        debug(F("HTTP-Begin fehlgeschlagen"));
        return false;
    }
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
    if (abs(sentimentScore - lastSentimentScore) >= 0.01 || !initialAnalysisDone)
    {
        String haValue = floatToString(sentimentScore, 2);

        if (mqttEnabled && mqtt.isConnected())
        {
            haSentimentScore.setValue(haValue.c_str());
        }

        lastSentimentScore = sentimentScore;
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
    if (categoryText != lastSentimentCategory || !initialAnalysisDone)
    {
        if (mqttEnabled && mqtt.isConnected())
        {
            haSentimentCategory.setValue(categoryText.c_str());
        }

        lastSentimentCategory = categoryText;
        debug("Neue Sentiment Kategorie: " + categoryText);
    }

    // Aktualisiere LED Index & LEDs nur bei Änderung
    if (newLedIndex != currentLedIndex || !initialAnalysisDone)
    { // Auch beim ersten Mal aktualisieren
        debug("LED Index geändert von " + String(currentLedIndex) + " zu " + String(newLedIndex) + " (" + categoryText + ")");
        currentLedIndex = newLedIndex;
        lastLedIndex = currentLedIndex;
        if (autoMode && lightOn)
        {
            updateLEDs(); // Ruft optimierte Funktion auf
        }
    }

    // API ist erreichbar
    sentimentAPIAvailable = true;
    consecutiveSentimentFailures = 0;
    lastSuccessfulSentimentUpdate = millis();
}

// Safer string handling function
void formatString(char *buffer, size_t maxLen, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, maxLen, format, args);
    va_end(args);
}

void saveSentimentStats(float sentiment) {
    // Validate sentiment value with strict bounds checking
    if (isnan(sentiment) || isinf(sentiment) || sentiment < -1.0f || sentiment > 1.0f) {
        debug(String(F("FEHLER: Ungültiger Sentiment-Wert für CSV: ")) + sentiment);
        return;
    }
    
    // Get current time with error handling
    time_t now;
    if (!time(&now) || now < 1600000000) {  // Timestamp vor 2020 ist ungültig
        debug(F("FEHLER: Ungültiger Zeitstempel für CSV"));
        now = millis() / 1000 + 1600000000;  // Fallback-Zeit
    }
    
    // Verwende CSV-Puffer mit zusätzlicher Validierung
    if (statsBuffer.add(now, sentiment)) {
        static unsigned long lastBufferLog = 0;
        if (millis() - lastBufferLog > 60000) { // Log nur einmal pro Minute
            debug(String(F("Sentiment zum CSV-Puffer hinzugefügt: ")) + sentiment);
            lastBufferLog = millis();
        }
    } else {
        debug(String(F("Fehler beim Hinzufügen zu CSV-Puffer: ")) + sentiment);
        
        // Fallback: Direktes Schreiben in Datei als Notlösung
        if (!LittleFS.exists("/data")) {
            if (!LittleFS.mkdir("/data")) {
                debug(F("Konnte /data-Verzeichnis nicht erstellen"));
                return;
            }
        }
        
        // Prüfe, ob Datei existiert und öffne sie entsprechend
        bool fileExists = LittleFS.exists("/data/stats.csv");
        
        // Zuerst einen String erstellen, um den Inhalt zu validieren
        char lineBuffer[64];
        snprintf(lineBuffer, sizeof(lineBuffer), "%ld,%.2f\n", (long)now, sentiment);
        
        // Überprüfe die Gültigkeit der CSV-Zeile
        if (strlen(lineBuffer) < 5) {  // Mindestlänge für gültige Zeile
            debug(F("FEHLER: Generierte CSV-Zeile ist zu kurz"));
            return;
        }
        
        File file;
        if (fileExists) {
            file = LittleFS.open("/data/stats.csv", "a");
        } else {
            file = LittleFS.open("/data/stats.csv", "w");
        }
        
        if (!file) {
            debug(F("FEHLER: Konnte CSV-Datei nicht öffnen"));
            return;
        }
        
        // Write header for new files
        if (!fileExists) {
            file.println("timestamp,sentiment");
        }
        
        // Write data
        size_t bytesWritten = file.print(lineBuffer);
        if (bytesWritten != strlen(lineBuffer)) {
            debug(F("FEHLER: Schreibfehler bei CSV-Zeile"));
        }
        file.close();
        
        debug(F("Sentiment direkt in CSV-Datei geschrieben (Fallback)"));
    }
}

// API-Handler für Feeds
void handleApiGetFeeds()
{
    if (!LittleFS.exists("/data/feeds.json"))
    {
        saveDefaultRssFeeds();
    }

    if (LittleFS.exists("/data/feeds.json"))
    {
        File file = LittleFS.open("/data/feeds.json", "r");
        if (!file)
        {
            server.send(500, "application/json", "{\"error\":\"Fehler beim Öffnen der RSS-Feed-Datei\"}");
            return;
        }

        String jsonContent = file.readString();
        file.close();
        server.send(200, "application/json", jsonContent);
    }
    else
    {
        server.send(404, "application/json", "{\"error\":\"RSS-Feed-Datei nicht gefunden\"}");
    }
}

void handleApiSaveFeeds()
{
    String jsonStr = server.arg("plain");

    if (jsonStr.length() == 0)
    {
        server.send(400, "application/json", "{\"error\":\"Leere Anfrage\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error)
    {
        server.send(400, "application/json", "{\"error\":\"JSON Parsing Fehler\"}");
        return;
    }

    if (!LittleFS.exists("/data"))
    {
        LittleFS.mkdir("/data");
    }

    File file = LittleFS.open("/data/feeds.json", "w");
    if (!file)
    {
        server.send(500, "application/json", "{\"error\":\"Fehler beim Erstellen der RSS-Feed-Datei\"}");
        return;
    }

    serializeJson(doc, file);
    file.close();

    debug(F("RSS-Feeds gespeichert"));

    // RSS-Feed-Konfiguration direkt an API senden
    if (WiFi.status() == WL_CONNECTED)
    {
        if (sendRSSConfigToAPI())
        {
            debug(F("Aktualisierte RSS-Feed-Konfiguration erfolgreich an API gesendet"));
        }
        else
        {
            debug(F("Aktualisierte RSS-Feed-Konfiguration konnte nicht an API gesendet werden"));
        }
    }

    server.send(200, "application/json", "{\"success\":true}");
}

// Validiere und importiere CSV-Datei
bool validateAndImportCSV(const String& filePath) {
    File importFile = LittleFS.open(filePath, "r");
    if (!importFile) {
        debug(String(F("Fehler beim Öffnen der Importdatei: ")) + filePath);
        return false;
    }
    
    // Header prüfen
    String header = importFile.readStringUntil('\n');
    header.trim();
    if (header != "timestamp,sentiment") {
        debug(F("Ungültiger CSV-Header. Erwartet: 'timestamp,sentiment'"));
        importFile.close();
        return false;
    }
    
    // Datenzähler für Validierung
    int validLines = 0;
    int invalidLines = 0;
    
    // Temporäre validierte Datei erstellen
    File validatedFile = LittleFS.open("/data/stats_validated.csv", "w");
    if (!validatedFile) {
        debug(F("Fehler beim Erstellen der validierten Datei"));
        importFile.close();
        return false;
    }
    
    // Header schreiben
    validatedFile.println("timestamp,sentiment");
    
    // Zeilen validieren
    while (importFile.available()) {
        String line = importFile.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0) continue;  // Leere Zeilen überspringen
        
        // Zeile parsen
        int commaPos = line.indexOf(',');
        if (commaPos <= 0) {
            invalidLines++;
            continue;  // Ungültige Zeile
        }
        
        String timestampStr = line.substring(0, commaPos);
        String sentimentStr = line.substring(commaPos + 1);
        
        // Timestamp validieren
        time_t timestamp = atol(timestampStr.c_str());
        if (timestamp <= 0) {
            invalidLines++;
            continue;  // Ungültiger Timestamp
        }
        
        // Sentiment validieren
        float sentiment;
        try {
            sentiment = atof(sentimentStr.c_str());
        } catch (...) {
            invalidLines++;
            continue;  // Konvertierungsfehler
        }
        
        // Sentiment-Bereich prüfen
        if (sentiment < -1.0 || sentiment > 1.0) {
            invalidLines++;
            continue;  // Sentiment außerhalb des gültigen Bereichs
        }
        
        // Gültige Zeile in validierte Datei schreiben
        validatedFile.print(timestamp);
        validatedFile.print(",");
        validatedFile.println(sentiment, 2);  // Maximal 2 Nachkommastellen
        
        validLines++;
    }
    
    importFile.close();
    validatedFile.close();
    
    debug(String(F("CSV-Validierung abgeschlossen: ")) + validLines + F(" gültige Zeilen, ") + invalidLines + F(" ungültige Zeilen"));
    
    // Validierte Datei in die Hauptdatei verschieben
    if (validLines > 0) {
        if (LittleFS.exists("/data/stats.csv")) {
            LittleFS.remove("/data/stats.csv");
        }
        
        if (fileOps.moveFile("/data/stats_validated.csv", "/data/stats.csv")) {
            debug(F("Validierte Datei wurde erfolgreich übernommen"));
            return true;
        } else {
            debug(F("Fehler beim Übernehmen der validierten Datei"));
            return false;
        }
    } else {
        debug(F("Keine gültigen Daten gefunden, Import abgebrochen"));
        LittleFS.remove("/data/stats_validated.csv");
        return false;
    }
}

// Validiere und importiere Einstellungen
bool validateAndImportSettings(const String& jsonContent) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonContent);
    
    if (error) {
        debug(String(F("JSON-Parsing-Fehler: ")) + error.c_str());
        return false;
    }
    
    // Grundlegende Validierung: Prüfe, ob mindestens einige erwartete Felder vorhanden sind
    // Mit isNull() und ! kann geprüft werden, ob der Schlüssel existiert und nicht null ist
    if (doc["moodInterval"].isNull() || doc["autoMode"].isNull()) {
        debug(F("Ungültiges Settings-Format: Erforderliche Felder fehlen"));
        return false;
    }
    
    // Einstellungen übernehmen - mit Validierung und sinnvollen Grenzwerten
    
    // Mood-Intervall (10 Sekunden bis 2 Stunden)
    if (!doc["moodInterval"].isNull()) {
        unsigned long interval = doc["moodInterval"].as<unsigned long>() * 1000;
        moodUpdateInterval = constrain(interval, 10000UL, 7200000UL);
    }
    
    // DHT-Intervall (10 Sekunden bis 1 Stunde)
    if (!doc["dhtInterval"].isNull()) {
        unsigned long interval = doc["dhtInterval"].as<unsigned long>() * 1000;
        dhtUpdateInterval = constrain(interval, 10000UL, 3600000UL);
    }
    
    // Boolean-Einstellungen
    if (!doc["autoMode"].isNull()) autoMode = doc["autoMode"].as<bool>();
    if (!doc["lightOn"].isNull()) lightOn = doc["lightOn"].as<bool>();
    if (!doc["dhtEnabled"].isNull()) dhtEnabled = doc["dhtEnabled"].as<bool>();
    if (!doc["wifiConfigured"].isNull()) wifiConfigured = doc["wifiConfigured"].as<bool>();
    if (!doc["mqttEnabled"].isNull()) mqttEnabled = doc["mqttEnabled"].as<bool>();
    
    // Numerische Einstellungen mit Grenzwerten
    if (!doc["manBright"].isNull()) manualBrightness = constrain(doc["manBright"].as<uint8_t>(), 10, 255);
    if (!doc["headlinesPS"].isNull()) headlines_per_source = constrain(doc["headlinesPS"].as<int>(), 1, 10);
    if (!doc["ledPin"].isNull()) ledPin = constrain(doc["ledPin"].as<int>(), 0, 39);
    if (!doc["dhtPin"].isNull()) dhtPin = constrain(doc["dhtPin"].as<int>(), 0, 39);
    if (!doc["numLeds"].isNull()) numLeds = constrain(doc["numLeds"].as<int>(), 1, 300);
    
    // String-Einstellungen mit Validierung
    if (!doc["wifiSSID"].isNull()) wifiSSID = doc["wifiSSID"].as<String>();
    if (!doc["wifiPass"].isNull()) wifiPassword = doc["wifiPass"].as<String>();
    if (!doc["apiUrl"].isNull()) {
        String url = doc["apiUrl"].as<String>();
        if (url.startsWith("http://") || url.startsWith("https://")) {
            apiUrl = url;
        }
    }
    if (!doc["mqttServer"].isNull()) mqttServer = doc["mqttServer"].as<String>();
    if (!doc["mqttUser"].isNull()) mqttUser = doc["mqttUser"].as<String>();
    if (!doc["mqttPass"].isNull()) mqttPassword = doc["mqttPass"].as<String>();
    
    // Manuelle Farbe
    if (!doc["manColor"].isNull()) manualColor = doc["manColor"].as<uint32_t>();
    
    // Benutzerdefinierte Farben laden
    for (int i = 0; i < 5; i++) {
        String colorKey = "color" + String(i);
        if (!doc[colorKey].isNull()) {
            customColors[i] = doc[colorKey].as<uint32_t>();
        }
    }
    
    debug(F("Einstellungen erfolgreich validiert und importiert"));
    return true;
}

bool repairStatsCSV() {
    debug(F("Starte Reparatur der Statistik-CSV-Datei..."));
    
    if (!LittleFS.exists("/data/stats.csv")) {
        debug(F("Keine Statistikdatei zum Reparieren vorhanden"));
        return false;
    }
    
    // Erstelle Backup der Originaldatei
    if (!fileOps.copyFile("/data/stats.csv", "/data/stats.csv.bak")) {
        debug(F("Konnte kein Backup der Statistikdatei erstellen"));
        // Fahre trotzdem fort mit dem Reparaturversuch
    }
    
    // Öffne die Statistikdatei
    File sourceFile = LittleFS.open("/data/stats.csv", "r");
    if (!sourceFile) {
        debug(F("Fehler beim Öffnen der Statistikdatei"));
        return false;
    }
    
    // Erstelle eine temporäre reparierte Datei
    File repairedFile = LittleFS.open("/data/stats_repaired.csv", "w");
    if (!repairedFile) {
        debug(F("Fehler beim Erstellen der reparierten Datei"));
        sourceFile.close();
        return false;
    }
    
    // Header lesen und schreiben
    String header = sourceFile.readStringUntil('\n');
    header.trim();
    
    // Überprüfe Header-Format
    if (header != "timestamp,sentiment") {
        debug(F("Ungültiger CSV-Header, setze Standard-Header"));
        repairedFile.println("timestamp,sentiment");
    } else {
        repairedFile.println(header);
    }
    
    // Zeilenzähler für die Statistik
    int totalLines = 0;
    int validLines = 0;
    int repairedLines = 0;
    int droppedLines = 0;
    
    // Letzte gültige Zeit für Reihenfolgenprüfung
    time_t lastValidTime = 0;
    
    // Zeile für Zeile verarbeiten
    while (sourceFile.available()) {
        String line = sourceFile.readStringUntil('\n');
        line.trim();
        totalLines++;
        
        if (line.length() == 0) continue; // Leere Zeilen überspringen
        
        // Formatprüfung: Muss ein Komma enthalten
        int commaPos = line.indexOf(',');
        if (commaPos <= 0) {
            debug(String(F("Zeile ")) + totalLines + F(": Kein gültiges Komma gefunden, überspringe"));
            droppedLines++;
            continue;
        }
        
        // Timestamp und Sentiment extrahieren
        String timestampStr = line.substring(0, commaPos);
        String sentimentStr = line.substring(commaPos + 1);
        
        // Timestamp parsen und validieren
        time_t timestamp;
        try {
            timestamp = atol(timestampStr.c_str());
        } catch (...) {
            debug(String(F("Zeile ")) + totalLines + F(": Timestamp konnte nicht geparst werden, überspringe"));
            droppedLines++;
            continue;
        }
        
        // Prüfe auf ungültige oder zukünftige Timestamps
        time_t currentTime = time(NULL);
        if (timestamp <= 0 || timestamp > currentTime + 86400) { // 1 Tag Zukunftspuffer
            debug(String(F("Zeile ")) + totalLines + F(": Ungültiger Zeitstempel: ") + timestamp);
            droppedLines++;
            continue;
        }
        
        // Prüfe auf Zeitreihenfolge (optional)
        if (lastValidTime > 0 && timestamp < lastValidTime) {
            debug(String(F("Zeile ")) + totalLines + F(": Zeitstempel in falscher Reihenfolge, korrigiere"));
            timestamp = lastValidTime + 1; // Korrigiere zu 1 Sekunde nach dem letzten
            repairedLines++;
        }
        
        // Sentiment parsen und validieren
        float sentiment;
        try {
            sentiment = atof(sentimentStr.c_str());
        } catch (...) {
            debug(String(F("Zeile ")) + totalLines + F(": Sentiment konnte nicht geparst werden, überspringe"));
            droppedLines++;
            continue;
        }
        
        // Sentiment-Bereich prüfen und ggf. korrigieren
        if (isnan(sentiment) || isinf(sentiment)) {
            debug(String(F("Zeile ")) + totalLines + F(": Sentiment ist NaN oder Inf, überspringe"));
            droppedLines++;
            continue;
        } else if (sentiment < -1.0 || sentiment > 1.0) {
            debug(String(F("Zeile ")) + totalLines + F(": Sentiment außerhalb des gültigen Bereichs, korrigiere"));
            sentiment = constrain(sentiment, -1.0f, 1.0f);
            repairedLines++;
        }
        
        // Schreibe reparierte Zeile
        repairedFile.print(timestamp);
        repairedFile.print(",");
        repairedFile.println(sentiment, 2); // 2 Nachkommastellen
        
        validLines++;
        lastValidTime = timestamp;
    }
    
    // Dateien schließen
    sourceFile.close();
    repairedFile.close();
    
    debug(String(F("CSV-Reparatur abgeschlossen: ")) + 
          validLines + F(" gültige Zeilen, ") + 
          repairedLines + F(" reparierte Zeilen, ") + 
          droppedLines + F(" verworfene Zeilen von insgesamt ") + 
          totalLines + F(" Zeilen"));
    
    // Ersetze die Originaldatei mit der reparierten Version, wenn sie gültige Daten enthält
    if (validLines > 0) {
        if (LittleFS.remove("/data/stats.csv")) {
            if (fileOps.moveFile("/data/stats_repaired.csv", "/data/stats.csv")) {
                debug(F("Reparierte Datei erfolgreich übernommen"));
                return true;
            } else {
                debug(F("Fehler beim Umbennen der reparierten Datei"));
                // Versuche Backup wiederherzustellen
                if (LittleFS.exists("/data/stats.csv.bak")) {
                    fileOps.copyFile("/data/stats.csv.bak", "/data/stats.csv");
                }
                return false;
            }
        } else {
            debug(F("Fehler beim Löschen der alten Datei"));
            LittleFS.remove("/data/stats_repaired.csv");
            return false;
        }
    } else {
        debug(F("Keine gültigen Daten in der reparierten Datei, behalte Original"));
        LittleFS.remove("/data/stats_repaired.csv");
        return false;
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

    // API-Endpunkte für dynamische Daten
    server.on("/api/status", HTTP_GET, handleApiStatus);
    server.on("/api/stats", HTTP_GET, handleApiStats);
    server.on("/api/feeds", HTTP_GET, handleApiGetFeeds);
    server.on("/api/feeds", HTTP_POST, handleApiSaveFeeds);

    server.on("/api/storage", HTTP_GET, handleApiStorageInfo);
setupArchiveEndpoints();

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
    bool success = repairStatsCSV();
    
    JsonDocument doc;
    doc["success"] = success;
    doc["message"] = success ? F("CSV-Datei erfolgreich repariert") : F("Konnte CSV-Datei nicht reparieren");
    
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
        
        doc["ledPin"] = ledPin;
        doc["dhtPin"] = dhtPin;
        doc["numLeds"] = numLeds;
        
        char* jsonBuffer = jsonPool.acquire();
        size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
        server.send(200, "application/json", jsonBuffer);
        jsonPool.release(jsonBuffer);
      });

      // Im Abschnitt setupWebServer() folgende Endpunkte hinzufügen:

// CSV-Datei herunterladen
server.on("/api/export/stats", HTTP_GET, []() {
    if (!LittleFS.exists("/data/stats.csv")) {
        server.send(404, "text/plain", "Keine Statistikdaten vorhanden");
        return;
    }
    
    File file = LittleFS.open("/data/stats.csv", "r");
    if (!file) {
        server.send(500, "text/plain", "Fehler beim Öffnen der Statistikdatei");
        return;
    }
    
    server.sendHeader("Content-Disposition", "attachment; filename=moodlight_stats.csv");
    server.streamFile(file, "text/csv");
    file.close();
    debug(F("Statistikdaten wurden exportiert"));
});

// Einstellungen herunterladen
server.on("/api/export/settings", HTTP_GET, []() {
    JsonDocument doc;
    
    // Allgemeine Einstellungen
    doc["moodInterval"] = moodUpdateInterval / 1000;
    doc["dhtInterval"] = dhtUpdateInterval / 1000;
    doc["autoMode"] = autoMode;
    doc["lightOn"] = lightOn;
    doc["manBright"] = manualBrightness;
    doc["manColor"] = manualColor;
    doc["headlinesPS"] = headlines_per_source;
    
    // WiFi-Einstellungen
    doc["wifiSSID"] = wifiSSID;
    doc["wifiPass"] = wifiPassword;
    doc["wifiConfigured"] = wifiConfigured;
    
    // Erweiterte Einstellungen
    doc["apiUrl"] = apiUrl;
    doc["mqttServer"] = mqttServer;
    doc["mqttUser"] = mqttUser;
    doc["mqttPass"] = mqttPassword;
    doc["dhtPin"] = dhtPin;
    doc["dhtEnabled"] = dhtEnabled;
    doc["ledPin"] = ledPin;
    doc["numLeds"] = numLeds;
    doc["mqttEnabled"] = mqttEnabled;
    
    // Benutzerdefinierte Farben
    for (int i = 0; i < 5; i++) {
        doc["color" + String(i)] = customColors[i];
    }
    
    String jsonContent;
    serializeJson(doc, jsonContent);
    
    server.sendHeader("Content-Disposition", "attachment; filename=moodlight_settings.json");
    server.send(200, "application/json", jsonContent);
    debug(F("Einstellungen wurden exportiert"));
});

// CSV-Import
server.on("/api/import/stats", HTTP_POST, []() {
    server.send(200, "text/plain", "CSV-Upload gestartet");
}, []() {
    HTTPUpload& upload = server.upload();
    static File uploadFile;
    
    if (upload.status == UPLOAD_FILE_START) {
        debug("CSV-Import: " + upload.filename);
        
        // Backup der aktuellen Datei erstellen
        if (LittleFS.exists("/data/stats.csv")) {
            if (!fileOps.copyFile("/data/stats.csv", "/data/stats.csv.bak")) {
                debug(F("Konnte kein Backup der Statistikdatei erstellen"));
            }
        }
        
        // Neue Datei öffnen
        uploadFile = LittleFS.open("/data/stats_import.csv", "w");
        if (!uploadFile) {
            debug(F("Fehler beim Erstellen der temporären Importdatei"));
            return;
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
            size_t written = uploadFile.write(upload.buf, upload.currentSize);
            if (written != upload.currentSize) {
                debug(F("Fehler beim Schreiben der Importdatei"));
            }
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            uploadFile.close();
            debug(F("CSV-Import abgeschlossen, validiere Datei..."));
            
            // CSV-Datei validieren und importieren
            if (validateAndImportCSV("/data/stats_import.csv")) {
                debug(F("CSV-Datei erfolgreich importiert"));
            } else {
                debug(F("Fehler beim Importieren der CSV-Datei, stelle Backup wieder her"));
                if (LittleFS.exists("/data/stats.csv.bak")) {
                    LittleFS.remove("/data/stats.csv");
                    fileOps.copyFile("/data/stats.csv.bak", "/data/stats.csv");
                }
            }
            
            // Temporäre Dateien löschen
            LittleFS.remove("/data/stats_import.csv");
            LittleFS.remove("/data/stats.csv.bak");
        }
    }
});

// Einstellungs-Import
server.on("/api/import/settings", HTTP_POST, []() {
    server.send(200, "text/plain", "Einstellungs-Upload gestartet");
}, []() {
    HTTPUpload& upload = server.upload();
    static String jsonContent;
    
    if (upload.status == UPLOAD_FILE_START) {
        debug("Einstellungs-Import: " + upload.filename);
        jsonContent = "";
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Datei im Speicher sammeln
        for (size_t i = 0; i < upload.currentSize; i++) {
            jsonContent += (char)upload.buf[i];
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        debug(F("Einstellungs-Import abgeschlossen, validiere Daten..."));
        
        // Einstellungen validieren und importieren
        if (validateAndImportSettings(jsonContent)) {
            debug(F("Einstellungen erfolgreich importiert"));
            
            // Einstellungen speichern
            saveSettings();
            settingsNeedSaving = false;
            
            // Reboot planen
            rebootNeeded = true;
            rebootTime = millis() + REBOOT_DELAY;
        } else {
            debug(F("Fehler beim Importieren der Einstellungen"));
        }
    }
});
      
    // Neue API-Endpunkte für Einstellungen
server.on("/api/settings/api", HTTP_GET, []() {
    JsonDocument doc;
    
    doc["apiUrl"] = apiUrl;
    doc["moodInterval"] = moodUpdateInterval / 1000;
    doc["dhtInterval"] = dhtUpdateInterval / 1000;
    doc["headlinesPerSource"] = headlines_per_source;
    doc["dhtEnabled"] = dhtEnabled;
    
    char* jsonBuffer = jsonPool.acquire();
    size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
    server.send(200, "application/json", jsonBuffer);
    jsonPool.release(jsonBuffer);
  });
  
  server.on("/api/settings/mqtt", HTTP_GET, []() {
    JsonDocument doc;
    
    doc["enabled"] = mqttEnabled;
    doc["server"] = mqttServer;
    doc["user"] = mqttUser;
    doc["pass"] = mqttPassword;
    
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
      uint32_t color = customColors[i];
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
    wifiSSID = doc["ssid"].as<String>();
    wifiPassword = doc["pass"].as<String>();
    wifiConfigured = true;

    // Einstellungen speichern
    settingsNeedSaving = true;
    lastSettingsSaved = millis();

    // Reboot planen
    rebootNeeded = true;
    rebootTime = millis() + REBOOT_DELAY;

    server.send(200, "text/plain", "OK");
    debug(F("Neue WiFi-Einstellungen gespeichert, Reboot geplant")); });

    // WiFi zurücksetzen
    server.on("/resetwifi", HTTP_POST, []()
              {
    wifiSSID = "";
    wifiPassword = "";
    wifiConfigured = false;

    // Einstellungen speichern
    settingsNeedSaving = true;
    lastSettingsSaved = millis();

    // Reboot planen
    rebootNeeded = true;
    rebootTime = millis() + REBOOT_DELAY;

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
    mqttEnabled = doc["enabled"].as<bool>();
    mqttServer = doc["server"].as<String>();
    mqttUser = doc["user"].as<String>();
    mqttPassword = doc["pass"].as<String>();

    // Einstellungen speichern
    settingsNeedSaving = true;
    lastSettingsSaved = millis();

    // Reboot planen
    rebootNeeded = true;
    rebootTime = millis() + REBOOT_DELAY;

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
    if (doc["apiUrl"].is<float>()) {
      String newApiUrl = doc["apiUrl"].as<String>();
      if (newApiUrl != apiUrl) {
        apiUrl = newApiUrl;
        changed = true;
        debug(String(F("API URL geändert zu: ")) + apiUrl);
      }
    }
    if (doc["moodInterval"].is<float>()) {
      unsigned long newMoodInterval = 1000UL * doc["moodInterval"].as<unsigned long>();
      newMoodInterval = constrain(newMoodInterval, 10000, 7200000);  // Mind. 10s, Max 2h
      if (newMoodInterval != moodUpdateInterval) {
        moodUpdateInterval = newMoodInterval;
        changed = true;
        debug(String(F("Mood Interval geändert zu: ")) + String(moodUpdateInterval / 1000) + F("s"));
      }
    }
    if (doc["dhtEnabled"].is<float>()) {
      bool newDhtEnabled = doc["dhtEnabled"].as<bool>();
      if (newDhtEnabled != dhtEnabled) {
        dhtEnabled = newDhtEnabled;
        changed = true;
        debug(String(F("DHT Enabled geändert zu: ")) + (dhtEnabled ? "ja" : "nein"));
      }
    }
    if (doc["headlinesPerSource"].is<float>()) {
      int newHeadlines = doc["headlinesPerSource"].as<int>();
      newHeadlines = constrain(newHeadlines, 1, 10);
      if (newHeadlines != headlines_per_source) {
        headlines_per_source = newHeadlines;
        lastMoodUpdate = 0;  // Erzwinge Sentiment-Update bei nächster Gelegenheit
        changed = true;
        debug(String(F("Headlines pro Quelle geändert zu: ")) + String(headlines_per_source));
      }
    }
    // NEU: DHT Intervall hier verarbeiten
    if (doc["dhtInterval"].is<float>()) {
      unsigned long newDhtInterval = 1000UL * doc["dhtInterval"].as<unsigned long>();
      newDhtInterval = constrain(newDhtInterval, 10000, 3600000);  // Mind. 10s, Max 1h
      if (newDhtInterval != dhtUpdateInterval) {
        dhtUpdateInterval = newDhtInterval;
        changed = true;
        debug(String(F("DHT Interval geändert zu: ")) + String(dhtUpdateInterval / 1000) + F("s"));
      }
    }

    // Nur speichern und HA updaten, wenn sich tatsächlich etwas geändert hat
    if (changed) {
      debug(F("API/Intervall-Einstellungen geändert. Speichere und aktualisiere HA..."));
      // Einstellungen speichern
      settingsNeedSaving = true;
      lastSettingsSaved = millis();

      // HA Entitäten aktualisieren, falls MQTT verbunden
      if (mqttEnabled && mqtt.isConnected()) {
        // API Update Interval
        haUpdateInterval.setState(float(moodUpdateInterval / 1000.0));
        debug(String(F("  HA: haUpdateInterval auf ")) + String(moodUpdateInterval / 1000.0) + F("s gesetzt."));

        // Headlines pro Quelle
        haHeadlinesPerSource.setState(float(headlines_per_source));
        debug(String(F("  HA: haHeadlinesPerSource auf ")) + String(headlines_per_source) + F(" gesetzt."));

        // DHT Update Interval
        haDhtInterval.setState(float(dhtUpdateInterval / 1000.0));
        debug(String(F("  HA: haDhtInterval auf ")) + String(dhtUpdateInterval / 1000.0) + F("s gesetzt."));
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
            customColors[index] = rgb;
            index++;
        }
    }

      // Einstellungen speichern
      settingsNeedSaving = true;
      lastSettingsSaved = millis();

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
    int testHeadlines = doc["headlinesPerSource"].as<int>();

    // API testen
    HTTPClient http;
    http.setReuse(false);
    http.setUserAgent("MoodlightClient/1.0");

    debug(String(F("Teste API URL: ")) + testApiUrl);
    String testUrl = testApiUrl;
    if (testUrl.indexOf('?') >= 0) {
      testUrl += "&headlines_per_source=" + String(testHeadlines);
    } else {
      testUrl += "?headlines_per_source=" + String(testHeadlines);
    }

    if (http.begin(wifiClientHTTP, testUrl)) {
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
    if (doc["ledPin"].is<int>() && doc["ledPin"].as<int>() != ledPin) {
        ledPin = doc["ledPin"].as<int>();
        needsReboot = true;  // Pin-Änderung erfordert Neustart
    }
    if (doc["dhtPin"].is<int>() && doc["dhtPin"].as<int>() != dhtPin) {
        dhtPin = doc["dhtPin"].as<int>();
        needsReboot = true;  // Pin-Änderung erfordert Neustart
    }

    if (doc["numLeds"].is<int>() && doc["numLeds"].as<int>() != numLeds) {
        numLeds = doc["numLeds"].as<int>();
        needsReboot = true;  // LED-Anzahl-Änderung erfordert Neustart
    }

    // DHT Intervall wird hier NICHT mehr verarbeitet

    if (needsReboot) {
      // Einstellungen speichern (nur wenn relevant)
      settingsNeedSaving = true;
      lastSettingsSaved = millis();  // Speichert die geänderten Pins/LEDs

      // Reboot planen (ist für Pin/LED-Änderungen notwendig)
      rebootNeeded = true;
      rebootTime = millis() + REBOOT_DELAY;

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
    wifiSSID = "";
    wifiPassword = "";
    wifiConfigured = false;
    mqttEnabled = false;
    mqttServer = "";
    mqttUser = "";
    mqttPassword = "";
    apiUrl = DEFAULT_NEWS_API_URL;
    moodUpdateInterval = DEFAULT_MOOD_UPDATE_INTERVAL;
    dhtUpdateInterval = DEFAULT_DHT_READ_INTERVAL;
    ledPin = DEFAULT_LED_PIN;
    dhtPin = DEFAULT_DHT_PIN;
    numLeds = DEFAULT_NUM_LEDS;
    headlines_per_source = 1;

    // Reboot planen
    rebootNeeded = true;
    rebootTime = millis() + REBOOT_DELAY;

    server.send(200, "text/plain", "OK");
    debug(F("Factory Reset durchgeführt, Reboot geplant")); });

    // Log-Anzeige
    server.on("/logs", HTTP_GET, []()
              {
    String logs = "";
    for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
      int idx = (logIndex + i) % LOG_BUFFER_SIZE;
      if (logBuffer[idx].length() > 0) {
        logs += logBuffer[idx] + "<br>";
      }
    }
    server.send(200, "text/html", logs); });

    // Status-Endpunkt für AJAX-Aktualisierungen
    server.on("/status", HTTP_GET, []()
              {
    JsonDocument doc;

    doc["wifi"] = WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected";
    doc["mqtt"] = mqttEnabled && mqtt.isConnected() ? "Connected" : (mqttEnabled ? "Disconnected" : "Disabled");

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
    doc["sentiment"] = String(lastSentimentScore, 2) + " (" + lastSentimentCategory + ")";
    doc["dhtEnabled"] = dhtEnabled;
    doc["dht"] = isnan(lastTemp) ? "N/A" : String(lastTemp, 1) + "°C / " + String(lastHum, 1) + "%";
    doc["mode"] = autoMode ? "Auto" : "Manual";
    doc["lightOn"] = lightOn;
    doc["brightness"] = manualBrightness;
    doc["headlines"] = headlines_per_source;

    // LED-Farbe als Hex holen
    uint32_t currentColor;
    if (autoMode) {
      currentLedIndex = constrain(currentLedIndex, 0, 4);
      ColorDefinition color = getColorDefinition(currentLedIndex);
      currentColor = pixels.Color(color.r, color.g, color.b);
    } else {
      currentColor = manualColor;
    }

    uint8_t r = (currentColor >> 16) & 0xFF;
    uint8_t g = (currentColor >> 8) & 0xFF;
    uint8_t b = currentColor & 0xFF;
    char hexColor[8];
    sprintf(hexColor, "#%02X%02X%02X", r, g, b);
    doc["ledColor"] = hexColor;

    // Status-LED Info
    if (statusLedMode != 0) {
      char statusLedColor[8] = "#000000";
      switch (statusLedMode) {
        case 1: strcpy(statusLedColor, "#0000FF"); break;  // WiFi - Blau
        case 2: strcpy(statusLedColor, "#FF0000"); break;  // API - Rot
        case 3: strcpy(statusLedColor, "#00FF00"); break;  // Update - Grün
        case 4: strcpy(statusLedColor, "#00FFFF"); break;  // MQTT - Cyan
        case 5: strcpy(statusLedColor, "#FFFF00"); break;  // AP - Gelb
      }
      doc["statusLedMode"] = statusLedMode;
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

    isPulsing = true;
    pulseStartTime = millis();

    debug(F("Starte Sentiment HTTP-Abruf (Force-Update)..."));
    if (http.begin(wifiClientHTTP, apiUrl + String("?headlines_per_source=") + String(headlines_per_source))) {
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
                lastMoodUpdate = millis();
                initialAnalysisDone = true;
            } else {
                debug(F("Fehler: 'sentiment' fehlt/falsch in JSON."));
            }
        } else {
            debug(String(F("JSON Parsing Fehler: ")) + error.c_str());
        }
      } else {
        debug(String(F("HTTP Fehler: ")) + String(httpCode));
        consecutiveSentimentFailures++;
      }
      http.end();
    }

    // Verarbeite das empfangene Sentiment
    if (success) {
      handleSentiment(receivedSentiment);
      if (statusLedMode == 2) {
        setStatusLED(0);  // Normalmodus
      }

      saveSentimentStats(receivedSentiment);

    } else {
      debug(String(F("Force-Update fehlgeschlagen")));
    }

    // Aktualisierung abschließen
    isPulsing = false;
    updateLEDs();
    debug(F("Force-Update abgeschlossen."));
});
    // toggle-light Endpunkt
    server.on("/toggle-light", HTTP_GET, []()
              {
    lightOn = !lightOn;

    // Erst die Antwort senden
    server.send(200, "text/plain", "OK");

    // Kleine Pause einfügen
    delay(50);

    // Dann LEDs aktualisieren
    if (lightOn) {
      updateLEDs();
    } else {
      pixels.clear();
      pixels.show();
    }

    // Kleine Pause vor dem Speichern
    delay(50);

    // Zum Schluss die Einstellungen speichern
    settingsNeedSaving = true;
    lastSettingsSaved = millis();

    // Home Assistant aktualisieren
    if (mqttEnabled && mqtt.isConnected()) {
      haLight.setState(lightOn);
    }

    debug(String(F("Licht über Web umgeschaltet: ")) + (lightOn ? "AN" : "AUS")); });

    // toggle-mode Endpunkt
    server.on("/toggle-mode", HTTP_GET, []()
              {
    autoMode = !autoMode;
    // Home Assistant aktualisieren, wenn aktiviert
    if (mqttEnabled && mqtt.isConnected()) {
      haMode.setState(autoMode ? 0 : 1);
    }

    // LEDs aktualisieren, wenn Licht an ist
    if (lightOn) {
      updateLEDs();
    }

    // Einstellung speichern
    settingsNeedSaving = true;
    lastSettingsSaved = millis();

    server.send(200, "text/plain", "OK");
    debug(String(F("Modus über Web umgeschaltet: ")) + (autoMode ? "Auto" : "Manual")); });

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
      manualColor = pixels.Color(r, g, b);

      // LEDs aktualisieren, wenn im manuellen Modus und Licht an
      if (!autoMode && lightOn) {
        updateLEDs();
      }

      // Home Assistant aktualisieren, wenn aktiviert
      if (mqttEnabled && mqtt.isConnected()) {
        HALight::RGBColor color;
        color.red = r;
        color.green = g;
        color.blue = b;
        haLight.setRGBColor(color);
      }

      // Einstellung speichern
      settingsNeedSaving = true;
      lastSettingsSaved = millis();

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
      manualBrightness = brightness;

      // LEDs aktualisieren, wenn im manuellen Modus und Licht an
      if (!autoMode && lightOn) {
        updateLEDs();
      }

      // Home Assistant aktualisieren, wenn aktiviert
      if (mqttEnabled && mqtt.isConnected()) {
        haLight.setBrightness(brightness);
      }

      // Einstellung speichern
      settingsNeedSaving = true;
      lastSettingsSaved = millis();

      server.send(200, "text/plain", "OK");
      debug(String(F("Helligkeit über Web gesetzt: ")) + brightness);
    } else {
      server.send(400, "text/plain", "Missing value parameter");
    } });

    // set-headlines Endpunkt
    server.on("/set-headlines", HTTP_GET, []()
              {
    if (server.hasArg("value")) {
      int headlines = server.arg("value").toInt();
      headlines = constrain(headlines, 1, 10);
      headlines_per_source = headlines;

      // Save to preferences
      preferences.begin("moodlight", false);
      preferences.putInt("headlinesPS", headlines);
      preferences.end();

      server.send(200, "text/plain", "OK");
      debug(String(F("Headlines pro Quelle geändert auf ")) + headlines);

      // Force sentiment update next cycle
      lastMoodUpdate = 0;
    } else {
      server.send(400, "text/plain", "Missing value");
    } });

    server.on("/api/settings/all", HTTP_GET, []() {
        JsonDocument doc;
        
        // Allgemeine Einstellungen
        doc["moodInterval"] = moodUpdateInterval / 1000;
        doc["dhtInterval"] = dhtUpdateInterval / 1000;
        doc["autoMode"] = autoMode;
        doc["lightOn"] = lightOn;
        doc["manBright"] = manualBrightness;
        
        // Farbe als HEX
        char hexColor[10];
        sprintf(hexColor, "#%06X", manualColor);
        doc["manColor"] = hexColor;
        
        doc["headlinesPS"] = headlines_per_source;
      
        // WiFi-Einstellungen (Passwort maskiert)
        doc["wifiSSID"] = wifiSSID;
        doc["wifiConfigured"] = wifiConfigured;
      
        // Erweiterte Einstellungen (Passwort maskiert)
        doc["apiUrl"] = apiUrl;
        doc["mqttServer"] = mqttServer;
        doc["mqttUser"] = mqttUser;
        doc["dhtPin"] = dhtPin;
        doc["dhtEnabled"] = dhtEnabled;
        doc["ledPin"] = ledPin;
        doc["numLeds"] = numLeds;
        doc["mqttEnabled"] = mqttEnabled;
        
        // Farben
        JsonArray colors = doc["colors"].to<JsonArray>();
        for (int i = 0; i < 5; i++) {
          char hexColor[10];
          sprintf(hexColor, "#%06X", customColors[i]);
          colors.add(hexColor);
        }
        
        // Dateisystem-Informationen
if (LittleFS.begin()) {
  size_t totalBytes = LittleFS.totalBytes();
  size_t usedBytes = LittleFS.usedBytes();
  doc["fsTotal"] = totalBytes;
  doc["fsUsed"] = usedBytes;
  doc["hasSettings"] = LittleFS.exists("/data/settings.json");
  doc["hasStats"] = LittleFS.exists("/data/stats.csv");
  doc["hasFeeds"] = LittleFS.exists("/data/feeds.json");
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
    uint8_t targetBrightness = autoMode ? DEFAULT_LED_BRIGHTNESS : manualBrightness;

    // Debug pulsing state periodically
    static unsigned long lastPulseDebug = 0;
    if (millis() - lastPulseDebug >= 10000)
    { // Every 10 seconds
        if (isPulsing)
        {
            debug(String(F("Pulse Status: Aktiv - Laufzeit: ")) + String((millis() - pulseStartTime) / 1000) + "s");
        }
        lastPulseDebug = millis();
    }

    if (!isPulsing || !lightOn)
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
    unsigned long elapsedTime = currentTime - pulseStartTime;

    // Auto-disable pulsing after timeout (3 cycles)
    if (elapsedTime > DEFAULT_WAVE_DURATION * 3)
    {
        debug(F("Pulse: Timeout - Auto-disable nach 3 Zyklen"));
        isPulsing = false;
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
    if (state == lightOn)
        return;
    lightOn = state;
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
    if (brightness == manualBrightness)
        return;
    manualBrightness = brightness;
    debug(String(F("HA Brightness Command: ")) + brightness);
    if (!autoMode && lightOn)
    {
        updateLEDs(); // updateLEDs berücksichtigt jetzt manualBrightness
    }
    sender->setBrightness(brightness);
    
    // Direkt speichern statt verzögerter Speicherung
    saveSettings();
}

void onRGBColorCommand(HALight::RGBColor color, HALight *sender)
{
    uint32_t newColor = pixels.Color(color.red, color.green, color.blue);
    if (newColor == manualColor)
        return;
    manualColor = newColor;
    debug(String(F("HA RGB Command: R=")) + color.red + " G=" + color.green + " B=" + color.blue);
    if (!autoMode && lightOn)
    {
        updateLEDs(); // updateLEDs berücksichtigt jetzt manualColor
    }
    sender->setRGBColor(color);
    
    // Direkt speichern statt verzögerter Speicherung
    saveSettings();
}

void onModeCommand(int8_t index, HASelect *sender) {
    // Ignore mode commands during startup phase
    if (initialStartupPhase) {
        debug(F("Ignoring mode command during startup grace period"));
        // Force HA back to the current state instead of accepting the change
        sender->setState(autoMode ? 0 : 1);
        return;
    }

    bool newMode = (index == 0);
    if (newMode == autoMode) return;
    autoMode = newMode;
    debug(String(F("HA Mode Command: ")) + (autoMode ? "Auto" : "Manual"));
    if (lightOn) {
        updateLEDs();
    }
    sender->setState(index);
    
    // Direkt speichern statt verzögerter Speicherung
    saveSettings();
}

void onUpdateIntervalCommand(HANumeric value, HANumber *sender)
{
    float intervalSeconds = value.toFloat();
    intervalSeconds = constrain(intervalSeconds, 10, 7200);
    unsigned long newInterval = (unsigned long)(intervalSeconds * 1000);
    if (newInterval == moodUpdateInterval)
        return;
    moodUpdateInterval = newInterval;
    sender->setState(float(intervalSeconds));
    debug(String(F("Mood Update Interval gesetzt auf: ")) + String(intervalSeconds) + "s");
    
    // Direkt speichern statt verzögerter Speicherung
    saveSettings();
    
    // Reset des Zeitgebers für das nächste Update (optional)
    // lastMoodUpdate = millis() - (moodUpdateInterval / 2); // Hälfte des Intervalls
}

void onHeadlinesCommand(HANumeric value, HANumber *sender)
{
    int newHeadlines = value.toInt8();             // Bereich 1-10 passt in int8_t
    newHeadlines = constrain(newHeadlines, 1, 10); // Bereich 1-10
    if (newHeadlines == headlines_per_source)
        return;

    headlines_per_source = newHeadlines;
    sender->setState(float(newHeadlines)); // Verwende float statt int
    debug(String(F("Headlines pro Quelle gesetzt auf: ")) + String(newHeadlines));

    // Direkt speichern statt verzögerter Speicherung
    saveSettings();

    // Erzwinge Sentiment-Update im nächsten Zyklus
    lastMoodUpdate = 0;
}

void onDHTIntervalCommand(HANumeric value, HANumber *sender)
{
    float intervalSeconds = value.toFloat();
    intervalSeconds = constrain(intervalSeconds, 10, 7200);
    unsigned long newInterval = (unsigned long)(intervalSeconds * 1000);
    if (newInterval == dhtUpdateInterval)
        return;
    dhtUpdateInterval = newInterval;
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

    isPulsing = true;
    pulseStartTime = millis();

    debug(F("Starte Sentiment HTTP-Abruf (Force-Update)..."));
    if (http.begin(wifiClientHTTP, apiUrl + String("?headlines_per_source=") + String(headlines_per_source)))
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
                    lastMoodUpdate = millis();
                    initialAnalysisDone = true;
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
        debug(String(F("HTTP Verbindungsfehler zu: ")) + String(apiUrl));
        if (wifiClientHTTP.connected())
        {
            wifiClientHTTP.stop();
        }
    }

    if (success)
    {
        handleSentiment(receivedSentiment);
    }

    isPulsing = false;
    updateLEDs();
}

// === MQTT Heartbeat ===
void sendHeartbeat()
{
    if (!mqttEnabled || !mqtt.isConnected())
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
    if (!sentimentAPIAvailable)
    {
        status = "Sentiment API nicht erreichbar";
    }

    // Temperatur/Luftfeuchtigkeit Status
    if (isnan(lastTemp) || isnan(lastHum))
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
    lastMqttHeartbeat = millis();
}
// === Home Assistant Setup Routine ===
void setupHA()
{
    if (!mqttEnabled || mqttServer.isEmpty())
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

    if (dhtEnabled)
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

    // Headlines pro Quelle
    haHeadlinesPerSource.setName("Headlines pro Quelle");
    haHeadlinesPerSource.setMin(1);
    haHeadlinesPerSource.setMax(10);
    haHeadlinesPerSource.setStep(1);
    haHeadlinesPerSource.setIcon("mdi:newspaper");
    haHeadlinesPerSource.onCommand(onHeadlinesCommand);
    haHeadlinesPerSource.setRetain(true);

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
    if (!mqttEnabled || !mqtt.isConnected())
    {
        debug(F("MQTT not connected, skipping initial states."));
        return;
    }
    debug(F("Sende initiale Zustände an HA..."));

    // Licht & Modus (aus globalen Variablen)
    haLight.setState(lightOn);
    haLight.setBrightness(manualBrightness);
    haHeadlinesPerSource.setState(float(headlines_per_source));
    uint32_t initialColor = autoMode ? pixels.Color(
                                           getColorDefinition(currentLedIndex).r,
                                           getColorDefinition(currentLedIndex).g,
                                           getColorDefinition(currentLedIndex).b)
                                     : manualColor;
    HALight::RGBColor color;
    color.red = (initialColor >> 16) & 0xFF;
    color.green = (initialColor >> 8) & 0xFF;
    color.blue = initialColor & 0xFF;
    haLight.setRGBColor(color);
    haMode.setState(autoMode ? 0 : 1);

    // Intervalle (aus globalen Variablen)
    haUpdateInterval.setState(float(moodUpdateInterval / 1000.0));
    haDhtInterval.setState(float(dhtUpdateInterval / 1000.0));

    // DHT direkt lesen für aktuelle Werte
    debug(F("Lese aktuelle DHT Werte für initiale Zustände..."));
    float currentTemp = dht.readTemperature();
    float currentHum = dht.readHumidity();
    bool tempValid = !isnan(currentTemp);
    bool humValid = !isnan(currentHum);

    if (tempValid)
    {
        lastTemp = currentTemp; // Update globale Variable
        haTemperature.setValue(floatToString(currentTemp, 1).c_str());
        debug(String(F("  Init Temp: ")) + String(currentTemp, 1) + "C");
    }
    else
    {
        debug(F("  Init Temp: Lesefehler!"));
        // Sende letzten bekannten Wert, falls vorhanden
        if (!isnan(lastTemp))
            haTemperature.setValue(floatToString(lastTemp, 1).c_str());
    }
    if (humValid)
    {
        lastHum = currentHum; // Update globale Variable
        haHumidity.setValue(floatToString(currentHum, 1).c_str());
        debug(String(F("  Init Hum: ")) + String(currentHum, 1) + "%");
    }
    else
    {
        debug(F("  Init Hum: Lesefehler!"));
        // Sende letzten bekannten Wert, falls vorhanden
        if (!isnan(lastHum))
            haHumidity.setValue(floatToString(lastHum, 1).c_str());
    }
    // Setze lastDHTUpdate, da wir gerade gelesen haben (verhindert sofortiges Lesen im Loop)
    lastDHTUpdate = millis();

    // Sentiment (letzter bekannter Wert aus globalen Variablen)
    haSentimentScore.setValue(floatToString(lastSentimentScore, 2).c_str());
    haSentimentCategory.setValue(lastSentimentCategory.c_str());
    debug(String(F("  Init Sentiment Score: ")) + String(lastSentimentScore, 2));
    debug(String(F("  Init Sentiment Category: ")) + lastSentimentCategory);

    // Initiale Heartbeat-Werte senden
    sendHeartbeat();

    debug(F("Initiale Zustände gesendet."));
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
        debug(String(F("Sentiment Interval Status: ")) + String(currentMillis - lastMoodUpdate) + F("/") + String(moodUpdateInterval) + F("ms"));
        lastIntervalDebug = currentMillis;
    }

    // Check for API timeout logic
    if (sentimentAPIAvailable && lastSuccessfulSentimentUpdate > 0 && currentMillis - lastSuccessfulSentimentUpdate > SENTIMENT_FALLBACK_TIMEOUT)
    {
        debug(F("API-Timeout: Kein erfolgreicher Sentiment-Abruf seit über einer Stunde."));
        debug(F("Wechsel in Neutral-Modus."));
        sentimentAPIAvailable = false;
        handleSentiment(0.0);
        setStatusLED(2);
    }

    // Avoid concurrent updates
    if (isUpdating)
        return;

    // Check if it's time for an update
    if (!(currentMillis - lastMoodUpdate >= moodUpdateInterval || !initialAnalysisDone))
        return;

    debug(F("Starte Sentiment-Abruf..."));
    isUpdating = true;
    isPulsing = true;
    pulseStartTime = currentMillis;

    // Create URL with headlines parameter
    String requestUrl = apiUrl;
    if (requestUrl.indexOf('?') >= 0)
    {
        requestUrl += "&headlines_per_source=" + String(headlines_per_source);
    }
    else
    {
        requestUrl += "?headlines_per_source=" + String(headlines_per_source);
    }

    // Use static document to avoid heap fragmentation
    static JsonDocument doc;
    doc.clear();

    // Use the safe HTTP request function
    bool success = safeHttpGet(requestUrl, doc);

    // Always update timing state
    lastMoodUpdate = currentMillis;
    initialAnalysisDone = true;

    if (success && doc["sentiment"].is<float>())
    {
        float receivedSentiment = doc["sentiment"].as<float>();
        debug(String(F("Sentiment empfangen: ")) + String(receivedSentiment, 2));

        // Process valid sentiment value
        handleSentiment(receivedSentiment);
        lastSuccessfulSentimentUpdate = currentMillis;

        // Reset error tracking
        consecutiveSentimentFailures = 0;
        sentimentAPIAvailable = true;

        // Reset status LED if there was a previous API error
        if (statusLedMode == 2)
        {
            setStatusLED(0);
        }

        saveSentimentStats(receivedSentiment);
    }
    else
    {
        debug(F("Sentiment Update fehlgeschlagen"));
        consecutiveSentimentFailures++;

        // Error handling for consecutive failures
        if (consecutiveSentimentFailures >= MAX_SENTIMENT_FAILURES && sentimentAPIAvailable)
        {
            sentimentAPIAvailable = false;
            debug(F("API nicht erreichbar nach mehreren Fehlversuchen. Wechsel in Neutral-Modus."));

            // Set neutral mood if no previous value exists
            if (!initialAnalysisDone)
            {
                handleSentiment(0.0);
            }

            // Set status LED to API error
            setStatusLED(2);
        }

        // Update HA values with last known value anyway
        if (mqttEnabled && mqtt.isConnected())
        {
            haSentimentScore.setValue(floatToString(lastSentimentScore, 2).c_str());
            haSentimentCategory.setValue(lastSentimentCategory.c_str());
        }
    }

    // Always clean up
    isPulsing = false;
    isUpdating = false;
    updateLEDs();

    debug(String(F("Sentiment Update abgeschlossen. Nächstes Update in ")) + String(moodUpdateInterval / 1000) + F(" Sekunden."));
}

// === Lese DHT Sensor und sende an HA ===
void readAndPublishDHT()
{
    // First check if DHT is enabled
    if (!dhtEnabled)
    {
        return; // Skip DHT processing entirely
    }

    if (millis() - lastDHTUpdate >= dhtUpdateInterval)
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
            bool tempChanged = abs(temp - lastTemp) >= 0.1; // 0.1°C Schwelle
            if (tempChanged || isnan(lastTemp))
            {
                lastTemp = temp;
                if (mqttEnabled && mqtt.isConnected())
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
            bool humChanged = abs(hum - lastHum) >= 0.5; // 0.5% Schwelle
            if (humChanged || isnan(lastHum))
            {
                lastHum = hum;
                if (mqttEnabled && mqtt.isConnected())
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

        lastDHTUpdate = millis();
        debug(String(F("DHT Lesezyklus beendet. Nächstes Update in ")) + String(dhtUpdateInterval / 1000) + F(" Sekunden."));
    }
}

// === Selbstheilende WiFi-Verbindung mit exponentiellem Backoff ===
// Update this function to initialize time after successful WiFi reconnection
void checkAndReconnectWifi()
{
    // Don't try to reconnect if we're in AP mode
    if (isInConfigMode)
    {
        return;
    }

    unsigned long currentMillis = millis();

    // Only check WiFi every 5 seconds to reduce load
    static unsigned long lastWifiStatusCheck = 0;
    if (currentMillis - lastWifiStatusCheck < 5000)
    {
        return;
    }
    lastWifiStatusCheck = currentMillis;

    if (WiFi.status() != WL_CONNECTED)
    {
        if (wifiWasConnected)
        {
            debug(F("WiFi Verbindung verloren! Starte Reconnect-Prozess..."));
            wifiWasConnected = false;
            wifiReconnectAttempts = 0;
            wifiReconnectDelay = 5000;
            setStatusLED(1);
        }

        // Check if it's time for a new reconnect attempt
        if (currentMillis - lastWifiCheck >= wifiReconnectDelay)
        {
            lastWifiCheck = currentMillis;
            wifiReconnectAttempts++;

            // Only try to connect if we have SSID
            if (wifiConfigured && !wifiSSID.isEmpty())
            {
                debug(String(F("WiFi Reconnect Versuch #")) + wifiReconnectAttempts + String(F(" mit Delay ")) + String(wifiReconnectDelay / 1000) + "s");

                // Try reconnect
                WiFi.disconnect(true);
                delay(100);
                WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

                // Short non-blocking wait time
                unsigned long reconnectStart = millis();
                int attemptCount = 0;
                while (WiFi.status() != WL_CONNECTED && millis() - reconnectStart < 3000)
                {
                    delay(50);
                    if (++attemptCount % 10 == 0)
                    {
                        yield(); // Explicit yield every 10 iterations (500ms)
                    }
                }

                if (WiFi.status() == WL_CONNECTED)
                {
                    debug(F("WiFi erfolgreich wieder verbunden!"));
                    wifiWasConnected = true;
                    wifiReconnectAttempts = 0;
                    wifiReconnectDelay = 5000;

                    // Explicitly disable power save mode after connection
                    esp_wifi_set_ps(WIFI_PS_NONE);

                    // Initialize time after successful reconnection if not already done
                    if (!timeInitialized) {
                        initTime();
                    }

                    setStatusLED(0);
                }
                else
                {
                    // Exponential backoff - with max limit
                    wifiReconnectDelay = min(wifiReconnectDelay * 2, MAX_RECONNECT_DELAY);
                    debug(String(F("WiFi Reconnect fehlgeschlagen. Nächster Versuch in ")) + String(wifiReconnectDelay / 1000) + "s");
                }
            }
        }
    }
    else if (!wifiWasConnected)
    {
        // WiFi is now connected but was disconnected before
        debug(F("WiFi ist wieder verbunden."));
        wifiWasConnected = true;
        wifiReconnectAttempts = 0;
        wifiReconnectDelay = 5000;

        // Explicitly disable power save mode after connection
        esp_wifi_set_ps(WIFI_PS_NONE);

        // Initialize time after successful reconnection if not already done
        if (!timeInitialized) {
            initTime();
        }

        setStatusLED(0);
    }
    yield();
    delay(1);
}

// === Verbesserte MQTT Reconnect und Heartbeat ===
// Fix the checkAndReconnectMQTT function - add the missing closing brace
void checkAndReconnectMQTT() {
    if (!mqttEnabled || mqttServer.isEmpty())
        return;

    unsigned long currentMillis = millis();

    // Heartbeat senden, wenn verbunden
    if (WiFi.status() == WL_CONNECTED && mqtt.isConnected()) {
        if (currentMillis - lastMqttHeartbeat >= MQTT_HEARTBEAT_INTERVAL) {
            sendHeartbeat();
        }
    }

    // Reduce reconnection attempts frequency
    static unsigned long mqttReconnectBackoff = 10000; // Start with 10 seconds
    
    // Reconnect-Logik
    if (WiFi.status() == WL_CONNECTED) {
        if (!mqtt.isConnected()) {
            if (currentMillis - lastMqttReconnectAttempt > mqttReconnectBackoff) {
                debug(F("MQTT nicht verbunden. Versuche Reconnect..."));
                
                // Update status LED indicator without direct LED update
                if (statusLedMode != 4) {
                    statusLedMode = 4; // MQTT mode
                    statusLedBlinkStart = currentMillis;
                    statusLedState = true;
                    
                    if (xSemaphoreTake(ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
                        ledColors[STATUS_LED_INDEX] = pixels.Color(0, 255, 255); // Cyan
                        ledUpdatePending = true;
                        xSemaphoreGive(ledMutex);
                    }
                }

                // Ensure we're not in an interrupt context
                yield();
                delay(10);
                
                mqtt.disconnect();
                delay(100);

                // MQTT neu starten
                mqtt.begin(mqttServer.c_str(), mqttUser.c_str(), mqttPassword.c_str());

                // Kurz auf Verbindung warten (mit mqtt.loop()!)
                unsigned long mqttStart = millis();
                while (!mqtt.isConnected() && millis() - mqttStart < 3000) {
                    mqtt.loop(); // WICHTIG: mqtt.loop() hier aufrufen!
                    delay(100);
                }

                lastMqttReconnectAttempt = currentMillis;

                if (mqtt.isConnected()) {
                    debug(F("MQTT erfolgreich verbunden."));
                    mqttWasConnected = true;
                    mqttReconnectBackoff = 10000; // Reset backoff on success
                    
                    // Schedule initial state sending for next loop cycle
                    // instead of doing it immediately
                    static bool initialStatesPending = true;
                    initialStatesPending = true;
                    
                    // Update status LED without direct update
                    statusLedMode = 0; // Normal mode
                    
                    if (xSemaphoreTake(ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
                        ledUpdatePending = true; // Request full LED update
                        xSemaphoreGive(ledMutex);
                    }
                } else {
                    debug(F("MQTT Reconnect fehlgeschlagen. Erhöhe Backoff."));
                    // Increase backoff time exponentially up to 5 minutes
                    mqttReconnectBackoff = min(mqttReconnectBackoff * 2, 300000UL);
                }
            }
        } else if (!mqttWasConnected) {
            debug(F("MQTT wieder verbunden. Sende Zustände..."));
            
            // Schedule state sending for next loop cycle
            static bool initialStatesPending = true;
            initialStatesPending = true;
            
            mqttWasConnected = true;
            
            // Update status LED without direct update
            statusLedMode = 0; // Normal mode
            
            if (xSemaphoreTake(ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
                ledUpdatePending = true; // Request full LED update
                xSemaphoreGive(ledMutex);
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

    if (currentMillis - lastStatusLog >= STATUS_LOG_INTERVAL)
    {
        debug(F("=== SYSTEM STATUS ==="));
        debug(String(F("Uptime: ")) + String(currentMillis / 1000 / 60) + F(" minutes"));
        debug(String(F("Free Heap: ")) + String(ESP.getFreeHeap()) + F(" bytes"));
        debug(String(F("WiFi Status: ")) + (WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected"));
        debug(String(F("WiFi RSSI: ")) + String(WiFi.RSSI()) + F(" dBm"));
        debug(String(F("MQTT Enabled: ")) + (mqttEnabled ? "Yes" : "No"));
        if (mqttEnabled)
        {
            debug(String(F("MQTT Status: ")) + (mqtt.isConnected() ? "Connected" : "Disconnected"));
        }
        debug(String(F("LED Status: ")) + (lightOn ? "ON" : "OFF") + ", Mode: " + (autoMode ? "Auto" : "Manual"));
        debug(String(F("Current Sentiment: ")) + String(lastSentimentScore, 2) + F(" (") + lastSentimentCategory + F(")"));
        debug(String(F("DHT Values: T=")) + String(lastTemp, 1) + F("C, H=") + String(lastHum, 1) + F("%"));
        debug(String(F("Intervals: Mood=")) + String(moodUpdateInterval / 1000) + F("s, DHT=") + String(dhtUpdateInterval / 1000) + F("s"));
        debug(F("===================="));

        lastStatusLog = currentMillis;
    }
}

// Modifizierte Funktion für AP-Modus mit korrekter Server-Initialisierung
void startAPModeWithServer()
{
    debug(F("Starte Access Point Modus..."));

    // Status-LED via Serial signalisieren, da pixels.show() vermieden wird
    debug(F("AP Mode activated (visual LED indicator skipped during init)."));
    // pixels.fill(pixels.Color(255, 255, 0));  // Gelb
    // pixels.show(); // <-- Vermeiden während Setup-Phase

    // AP-Modus konfigurieren
    WiFi.mode(WIFI_AP);
    delay(300); // Längere Verzögerung für Modus-Wechsel

    WiFi.softAP(DEFAULT_AP_NAME, DEFAULT_AP_PASSWORD);

    IPAddress IP = CAPTIVE_PORTAL_IP;
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(IP, IP, subnet);

    delay(500);

    // WiFi-Station-Modus explizit deaktivieren
    WiFi.disconnect(true); // Disconnect STA mode if it was somehow active

    // *** ESP-IDF Power Save im AP Modus deaktivieren ***
    esp_wifi_set_ps(WIFI_PS_NONE);
    debug(F("ESP-IDF WiFi Power Save explicitly disabled (AP)."));

    // Jetzt erst den Webserver starten
    server.begin();
    debug(F("Webserver im AP-Modus gestartet"));

    // Setup komplettes Webserver-Routing (ist bereits global erfolgt)
    // setupWebServer(); // Nicht nochmal aufrufen!

    // DNS-Server starten
    dnsServer.start(DNS_PORT, "*", IP);
    isInConfigMode = true; // Dies markiert, dass wir im AP-Modus sind

    debug("AP gestartet mit IP " + WiFi.softAPIP().toString());
    debug(String(F("SSID: ")) + DEFAULT_AP_NAME);

    // Captive Portal Request Handler hinzufügen
    server.addHandler(new CaptiveRequestHandler());

    // Status LED für AP-Modus (wird in loop aktualisiert)
    setStatusLED(5);

    // Merke Zeit für Timeout
    apModeStartTime = millis();
}


// === Arduino Setup ===
// Add NTP initialization to setup function
void setup() {
    // Initialisiere serielle Kommunikation für Debug-Ausgaben
    Serial.begin(115200);
    
    // Watchdog-Timer initialisieren mit 30 Sekunden Timeout, ohne automatischen Reset
    esp_task_wdt_init(30, false);

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

    debug(F("Prüfe und repariere CSV-Datei falls nötig..."));
    if (LittleFS.exists("/data/stats.csv")) {
        repairStatsCSV();
    }

    // Archivierungstask erstellen (läuft als separate Task)
    bool archiveTaskStarted = archiveTask.begin(archiveTaskFunction);
    if (archiveTaskStarted) {
        debug(F("Archivierungstask gestartet"));
    } else {
        debug(F("Konnte Archivierungstask nicht starten"));
    }

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
    
    // CSV-Puffer initialisieren für Statistikdaten
    if (statsBuffer.begin()) {
        debug(F("CSV-Puffer für Statistiken initialisiert"));
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
    ledMutex = xSemaphoreCreateMutex();
    if (ledMutex == NULL) {
        debug(F("Failed to create LED mutex!"));
    }

    // DHT-Sensor initialisieren (falls verwendet)
    dht.begin();

    // Archivierungsprozess starten
    if (archiveTask.isRunning()) {
        archiveTask.execute();
        debug(F("Archivierungsauftrag in die Warteschlange gestellt"));
    } else {
        // Direktes Archivieren als Fallback
        archiveOldData();
    }

    // Webserver-Routen definieren (Server wird später gestartet)
    setupWebServer();

    // WiFi-Verbindung herstellen (falls konfiguriert)
    if (wifiConfigured && !wifiSSID.isEmpty()) {
        debug(String(F("Vorhandene WiFi-Konfiguration gefunden: ")) + wifiSSID);

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

            // RSS-Feed-Konfiguration an API senden
            if (sendRSSConfigToAPI()) {
                debug(F("RSS-Feed-Konfiguration erfolgreich an API gesendet"));
            } else {
                debug(F("RSS-Feed-Konfiguration konnte nicht an API gesendet werden"));
            }

            // MQTT einrichten für Home Assistant Integration
            if (mqttEnabled && !mqttServer.isEmpty()) {
                debug(F("MQTT Konfiguration gefunden, starte verzögerte Initialisierung..."));
                delay(500);

                setupHA();  // Home Assistant Entities konfigurieren

                // MQTT-Verbindung mit Timeout-Sicherheit aufbauen
                debug(F("Versuche MQTT zu initialisieren..."));
                unsigned long mqttStartTime = millis();
                bool mqttInitSuccess = false;

                try {
                    // MQTT-Verbindung starten (nicht-blockierend)
                    mqtt.begin(mqttServer.c_str(), mqttUser.c_str(), mqttPassword.c_str());

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
    pixels = Adafruit_NeoPixel(numLeds, ledPin, NEO_GRB + NEO_KHZ800);
    pixels.begin();
    pixels.setBrightness(DEFAULT_LED_BRIGHTNESS);
    debug(F("NeoPixel basic init done (begin/setBrightness)."));

    debug(F("Setup abgeschlossen."));
    
    // Startwerte für Betriebsparameter setzen
    startupTime = millis();
    initialStartupPhase = true;
    debug(F("Startup grace period active - ignoring mode changes for 15 seconds"));

    // Marker für den Start der Loop
    Serial.println("=========== Loop Start ===========");
}

// === Arduino Loop ===
void loop() {
    // Watchdog regelmäßig füttern, um automatischen Neustart zu verhindern
    watchdog.autoFeed();
    
    // Erste LED-Initialisierung nach dem Setup
    if (!firstLedShowDone) {
        // LED-Puffer mit Nullen initialisieren für sauberen Start
        if (xSemaphoreTake(ledMutex, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < numLeds; i++) {
                ledColors[i] = 0;
            }
            ledClear = true;
            ledUpdatePending = true;
            xSemaphoreGive(ledMutex);
        }
        
        // LEDs werden durch processLEDUpdates aktualisiert
        firstLedShowDone = true;
        debug(F("First LED update scheduled"));
    }

    // DNS-Anfragen verarbeiten, falls im Access-Point-Modus
    if (isInConfigMode) {
        dnsServer.processNextRequest();

        // Timeout für AP-Modus prüfen (Neustart nach bestimmter Zeit)
        if (millis() - apModeStartTime > AP_TIMEOUT) {
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
    if (rebootNeeded && millis() > rebootTime) {
        debug(F("Ausführen des angeforderten Neustarts..."));
        delay(200);
        ESP.restart();
    }

    // Tägliche Archivierung alter Daten
    static unsigned long lastArchiveCheck = 0;
    if (millis() - lastArchiveCheck >= 24 * 60 * 60 * 1000) {  // Einmal täglich
        archiveOldData();
        lastArchiveCheck = millis();
    }
    
    // Startup-Grace-Period beenden nach definierter Zeit
    if (initialStartupPhase && (millis() - startupTime > STARTUP_GRACE_PERIOD)) {
        initialStartupPhase = false;
        debug(F("Initial startup grace period ended, now accepting all commands"));
    }
    
    // MQTT-Loop periodisch ausführen für Verbindungspflege
    static unsigned long lastMqttLoop = 0;
    if (mqttEnabled && WiFi.status() == WL_CONNECTED && (millis() - lastMqttLoop >= 100)) {
        mqtt.loop();
        lastMqttLoop = millis();
    }

    // WiFi- und MQTT-Verbindungen prüfen und wiederherstellen
    static unsigned long lastConnectionCheck = 0;
    if (millis() - lastConnectionCheck >= 2000) {  // Alle 2 Sekunden
        lastConnectionCheck = millis();

        checkAndReconnectWifi();

        if (mqttEnabled) {
            checkAndReconnectMQTT();
        }
    }

    // Anstehende LED-Updates verarbeiten
    processLEDUpdates();

    // Einstellungen speichern, falls geändert
    if (settingsNeedSaving && (millis() - lastSettingsSaved > 2000)) {
        debug(F("Verzögerte Speicherung ausführen..."));
        saveSettings();
        settingsNeedSaving = false;
        lastSettingsSaved = millis();
    }

    // Sentiment-Aktualisierung und DHT-Sensor auslesen bei aktiver WiFi-Verbindung
    if (WiFi.status() == WL_CONNECTED) {
        if (autoMode) {
            getSentiment();  // Weltlage-Stimmung abrufen
        }
        readAndPublishDHT();  // DHT-Sensor auslesen
    } else if (isPulsing) {
        // Pulsieren stoppen, wenn WiFi nicht verbunden
        isPulsing = false;
        
        // LED-Helligkeit zurücksetzen
        if (xSemaphoreTake(ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            ledBrightness = autoMode ? DEFAULT_LED_BRIGHTNESS : manualBrightness;
            ledUpdatePending = true;
            xSemaphoreGive(ledMutex);
        }
    }

    // Status-LED aktualisieren (blinken, je nach Modus)
    updateStatusLED();
    
    // Pulsieren der LEDs verarbeiten
    if (isPulsing && lightOn) {
        static unsigned long lastPulseUpdate = 0;
        if (millis() - lastPulseUpdate >= 30) {  // 30ms zwischen Updates
            unsigned long currentTime = millis();
            unsigned long elapsedTime = currentTime - pulseStartTime;
            
            // Auto-deaktivieren des Pulsierens nach Timeout
            if (elapsedTime > DEFAULT_WAVE_DURATION * 3) {
                debug(F("Pulse: Timeout - Auto-disable nach 3 Zyklen"));
                isPulsing = false;
                
                if (xSemaphoreTake(ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
                    ledBrightness = autoMode ? DEFAULT_LED_BRIGHTNESS : manualBrightness;
                    ledUpdatePending = true;
                    xSemaphoreGive(ledMutex);
                }
            } else {
                // Sinuswelle für sanftes Pulsieren berechnen
                float progress = fmod((float)elapsedTime, (float)DEFAULT_WAVE_DURATION) / (float)DEFAULT_WAVE_DURATION;
                float easedValue = (sin(progress * 2.0 * PI - PI / 2.0) + 1.0) / 2.0;
                
                // Helligkeit skalieren
                uint8_t targetBrightness = autoMode ? DEFAULT_LED_BRIGHTNESS : manualBrightness;
                uint8_t minPulseBright = (DEFAULT_WAVE_MIN_BRIGHTNESS < targetBrightness / 2) ? DEFAULT_WAVE_MIN_BRIGHTNESS : (targetBrightness / 2);
                uint8_t maxPulseBright = (DEFAULT_WAVE_MAX_BRIGHTNESS < targetBrightness) ? DEFAULT_WAVE_MAX_BRIGHTNESS : targetBrightness;
                
                int brightness = minPulseBright + (int)(easedValue * (maxPulseBright - minPulseBright));
                
                // LED-Helligkeit sicher aktualisieren
                if (xSemaphoreTake(ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
                    ledBrightness = constrain(brightness, 0, 255);
                    ledUpdatePending = true;
                    xSemaphoreGive(ledMutex);
                }
            }
            lastPulseUpdate = millis();
        }
    }

    // System-Gesundheitsüberprüfung alle HEALTH_CHECK_INTERVAL (1 Stunde)
    unsigned long currentMillis = millis();
    if (currentMillis - lastSystemHealthCheckTime >= HEALTH_CHECK_INTERVAL) {
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
                rebootNeeded = true;
                rebootTime = currentMillis + 60000;  // 1 Minute Verzögerung
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
                rebootNeeded = true;
                rebootTime = currentMillis + 30000;  // 30 Sekunden Verzögerung
                
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
            debug(F("Hohe Dateisystembelegung erkannt - starte automatische Archivierung"));
            
            // Archivierung über Task oder direkt starten
            if (archiveTask.isRunning()) {
                archiveTask.execute();
            } else {
                archiveOldData();
            }
        }
        
        lastSystemHealthCheckTime = currentMillis;
    }

    // Regelmäßige Statusprotokollierung
    static unsigned long lastStatusLog = 0;
    if (millis() - lastStatusLog >= STATUS_LOG_INTERVAL) {
        logSystemStatus();
        lastStatusLog = millis();
    }

    // System etwas Zeit geben - verhindert 100% CPU-Auslastung
    yield();
    delay(20);  // 20ms Pause
    
    // Speichernutzung überwachen
    memMonitor.update();
    
    // Regelmäßiger Systemgesundheitscheck
    static unsigned long lastHealthCheck = 0;
    if (millis() - lastHealthCheck > 300000) {  // Alle 5 Minuten
        sysHealth.update();
        lastHealthCheck = millis();
        
        // Neustart empfehlen bei kritischen Problemen
        if (sysHealth.isRestartRecommended()) {
            debug(F("Systemdiagnose empfiehlt Neustart. Plane Neustart in 60 Sekunden..."));
            rebootNeeded = true;
            rebootTime = millis() + 60000;
        }
    }
}