// ========================================================
// Web-Server Modul
// ========================================================
// Alle Web-Server-Funktionen, API-Handler, File-Handler,
// Upload-Handler, JsonBufferPool und Datei-Hilfsfunktionen.
// Extrahiert aus moodlight.cpp (Plan 07-06).

#include "web_server.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "LittleFS.h"
#include <Adafruit_NeoPixel.h>
#include "MoodlightUtils.h"
#include <Preferences.h>
#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>

#include "led_controller.h"
#include "mqtt_handler.h"
#include "sensor_manager.h"

// === Externe Globals aus moodlight.cpp ===
extern AppState appState;
extern WebServer server;
extern Adafruit_NeoPixel pixels;
extern Preferences preferences;
extern WiFiClient wifiClientHTTP;
extern const String SOFTWARE_VERSION;
extern const int LOG_BUFFER_SIZE;
extern const unsigned long REBOOT_DELAY;
extern const unsigned long STATUS_LOG_INTERVAL;

// === Externe Funktionen aus moodlight.cpp ===
extern void debug(const String &message);
extern void debug(const __FlashStringHelper *message);
extern String floatToString(float value, int decimalPlaces);

// === Externe Funktionen aus anderen Modulen ===
extern void saveSettings();
extern void loadSettings();
extern void updateLEDs();
extern void setStatusLED(int mode);
extern void handleSentiment(float score);
extern void getSentiment();
extern bool fetchBackendStatistics(JsonDocument &doc, int hours);
extern int mapSentimentToLED(float score);
extern String scanWiFiNetworks();

// === Externe Utility-Instanzen aus moodlight.cpp ===
extern MemoryMonitor memMonitor;
extern NetworkDiagnostics netDiag;
extern SystemHealthCheck sysHealth;
extern SafeFileOps fileOps;

// ===== JSON-Puffer-Pool =====
#define JSON_BUFFER_SIZE 16384
#define JSON_BUFFER_COUNT 2

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

void initJsonPool() {
    jsonPool.init();
}

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

// ===== Datei-Hilfsfunktionen =====

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

// ===== Dateisystem-Initialisierung =====

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

    // REMOVED v9.0: RSS feeds now managed in backend
    // REMOVED v9.0: Stats now managed in backend, no local CSV needed

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

// ===== Versions-Abfragen =====

String getCurrentUiVersion() {
    if (LittleFS.exists("/ui-version.txt")) {
        File versionFile = LittleFS.open("/ui-version.txt", "r");
        if (versionFile) {
            String version = versionFile.readString();
            versionFile.close();
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
            version.trim();
            return version;
        }
    }

    // Default to the SOFTWARE_VERSION if file doesn't exist
    return String(SOFTWARE_VERSION);
}

// ===== Speicherinformationen =====

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

// ===== Statische Dateien =====

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

// ===== UI-Upload Handler =====

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

// ===== API-Handler =====

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
    } else {
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

// ===== UPDATED IN v9.0: Stats from Backend =====
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

// ===== System-Logging =====

void logSystemStatus() {
    unsigned long currentMillis = millis();

    if (currentMillis - appState.lastStatusLog >= STATUS_LOG_INTERVAL) {
        debug(F("=== SYSTEM STATUS ==="));
        debug(String(F("Uptime: ")) + String(currentMillis / 1000 / 60) + F(" minutes"));
        debug(String(F("Free Heap: ")) + String(ESP.getFreeHeap()) + F(" bytes"));
        debug(String(F("WiFi Status: ")) + (WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected"));
        debug(String(F("WiFi RSSI: ")) + String(WiFi.RSSI()) + F(" dBm"));
        debug(String(F("MQTT Enabled: ")) + (appState.mqttEnabled ? "Yes" : "No"));
        if (appState.mqttEnabled) {
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

// ===== Web-Server Setup =====

void setupWebServer() {
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
    server.on("/wifiscan", HTTP_GET, []() {
        String jsonResult = scanWiFiNetworks();
        server.send(200, "application/json", jsonResult);
    });

    // WiFi Einstellungen speichern
    server.on("/savewifi", HTTP_POST, []() {
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
        debug(F("Neue WiFi-Einstellungen gespeichert, Reboot geplant"));
    });

    // WiFi zurücksetzen
    server.on("/resetwifi", HTTP_POST, []() {
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
        debug(F("WiFi-Einstellungen zurückgesetzt, Reboot geplant"));
    });

    // MQTT Einstellungen speichern
    server.on("/savemqtt", HTTP_POST, []() {
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
        debug(F("MQTT-Einstellungen gespeichert, Reboot geplant"));
    });

    // API und Intervall Einstellungen speichern (ohne Neustart)
    server.on("/saveapi", HTTP_POST, []() {
        String jsonStr = server.arg("plain");
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, jsonStr);

        if (error) {
            debug(String(F("JSON Parsing Fehler: ")) + error.c_str());
            server.send(400, "text/plain", "JSON Parsing Fehler");
            return;
        }

        bool changed = false;

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
            server.send(200, "text/plain", "Keine Änderungen");
        }
    });

    // Farben speichern
    server.on("/savecolors", HTTP_POST, []() {
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
        }
    });

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
    server.on("/savehardware", HTTP_POST, []() {
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
        }
    });

    // Factory Reset - Alle Einstellungen zurücksetzen
    server.on("/factoryreset", HTTP_POST, []() {
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
        debug(F("Factory Reset durchgeführt, Reboot geplant"));
    });

    // Log-Anzeige
    server.on("/logs", HTTP_GET, []() {
        String logs = "";
        for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
            int idx = (appState.logIndex + i) % LOG_BUFFER_SIZE;
            if (appState.logBuffer[idx].length() > 0) {
                logs += appState.logBuffer[idx] + "<br>";
            }
        }
        server.send(200, "text/html", logs);
    });

    // Status-Endpunkt für AJAX-Aktualisierungen
    server.on("/status", HTTP_GET, []() {
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
        // v9.0: headlines_per_source parameter removed
        if (http.begin(wifiClientHTTP, appState.apiUrl)) {
            http.setTimeout(10000);
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
    server.on("/toggle-light", HTTP_GET, []() {
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

        debug(String(F("Licht über Web umgeschaltet: ")) + (appState.lightOn ? "AN" : "AUS"));
    });

    // toggle-mode Endpunkt
    server.on("/toggle-mode", HTTP_GET, []() {
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
        debug(String(F("Modus über Web umgeschaltet: ")) + (appState.autoMode ? "Auto" : "Manual"));
    });

    // set-color Endpunkt
    server.on("/set-color", HTTP_GET, []() {
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
        }
    });

    // set-brightness Endpunkt
    server.on("/set-brightness", HTTP_GET, []() {
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
        }
    });

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
            char hexColorArr[10];
            sprintf(hexColorArr, "#%06X", appState.customColors[i]);
            colors.add(hexColorArr);
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
    server.on("/restart", HTTP_GET, []() {
        server.send(200, "text/html", "<html><body><h1>Restarting...</h1><p>Device will restart in a few seconds.</p><script>setTimeout(function(){window.location.href='/';}, 10000);</script></body></html>");
        delay(1000);
        ESP.restart();
    });

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
    server.onNotFound([]() {
        String path = server.uri();
        handleStaticFile(path);
    });

    server.begin();
    debug(F("Webserver gestartet"));
}
