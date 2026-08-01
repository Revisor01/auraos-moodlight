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
#include <time.h>
#include "esp_idf_version.h"
#include "esp_task_wdt.h"
#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>

#include "led_controller.h"
#include "mqtt_handler.h"
#include "sensor_manager.h"

// === Externe Globals aus moodlight.cpp ===
extern AppState appState;
extern Adafruit_NeoPixel pixels;
extern Preferences preferences;
extern const String SOFTWARE_VERSION;

#include "debug.h"

// Hardware-Instanz — definiert in diesem Modul
WebServer server(80);

// === Externe Funktionen aus anderen Modulen ===
extern void saveSettings();
extern void loadSettings();
extern void updateLEDs();
extern void setStatusLED(int mode);
extern void handleSentiment(float score, const String &apiCategory);
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
// 4096 reicht fuer alle Pool-Antworten (Status ~2 KB, serializeJson-Aufrufe
// werden gegen dieses Limit geprueft) — 16384 war unnoetig grosszuegig (A-MITTEL Flash/RAM)
#define JSON_BUFFER_SIZE 4096
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

// A-NIEDRIG: JsonBufferGuard entfernt — ungenutzt, kein Aufrufer im Code

// ===== Datei-Hilfsfunktionen =====

// Helper function to copy a file
// A-NIEDRIG: char[64]-Zwischenpuffer entfernt — String::c_str() direkt verwendet
// (Puffer haette laengere Pfade stillschweigend abgeschnitten)
bool copyFile(const String& source, const String& destination) {
    if (!LittleFS.exists(source)) {
        debug("Quelldatei existiert nicht: " + source);
        return false;
    }

    File sourceFile = LittleFS.open(source, "r");
    if (!sourceFile) {
        debug("Quelldatei konnte nicht geöffnet werden: " + source);
        return false;
    }

    File destFile = LittleFS.open(destination, "w");
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

    // /firmware-version.txt wird nicht mehr angelegt: die Firmware-Version kommt
    // ausschliesslich aus SOFTWARE_VERSION. Eine Datei im Flash ueberlebt einen
    // USB-Flash und meldet danach eine veraltete Version.
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
    // Immer die einkompilierte Version melden — sie ist die einzige Quelle, die
    // nicht veralten kann. Zuvor wurde /firmware-version.txt aus dem Flash
    // gelesen; diese Datei wird aber nur beim OTA-Update geschrieben und blieb
    // nach einem USB-Flash auf dem alten Stand. Ergebnis: /api/firmware-version
    // meldete 9.14, waehrend /api/status (das SOFTWARE_VERSION nutzt) korrekt
    // 9.15 auswies.
    return String(SOFTWARE_VERSION);
}

// ===== Speicherinformationen =====

void getStorageInfo(uint64_t &totalBytes, uint64_t &usedBytes, uint64_t &freeBytes) {
    // A-NIEDRIG: LittleFS.begin()-Aufruf entfernt — FS ist bereits ueber initFS() gemountet
    totalBytes = LittleFS.totalBytes();
    usedBytes = LittleFS.usedBytes();
    freeBytes = totalBytes - usedBytes;
}

// ===== Statische Dateien =====

void handleStaticFile(String path) {
    if (path.endsWith("/")) path += "index.html";

    String contentType = "text/html";
    bool isCacheable = false; // CSS/JS duerfen lange gecacht werden, HTML nicht
    if (path.endsWith(".css")) { contentType = "text/css"; isCacheable = true; }
    else if (path.endsWith(".js")) { contentType = "application/javascript"; isCacheable = true; }
    else if (path.endsWith(".json")) contentType = "application/json";
    else if (path.endsWith(".png")) { contentType = "image/png"; isCacheable = true; }
    else if (path.endsWith(".jpg")) { contentType = "image/jpeg"; isCacheable = true; }
    else if (path.endsWith(".ico")) { contentType = "image/x-icon"; isCacheable = true; }

    if (!path.startsWith("/")) {
        path = "/" + path;
    }

    // Direkt oeffnen statt exists()+open() — vermeidet doppelten Dateisystem-Zugriff (A-NIEDRIG)
    File file = LittleFS.open(path, "r");
    if (file) {
        if (isCacheable) {
            server.sendHeader("Cache-Control", "public, max-age=86400");
        } else {
            server.sendHeader("Cache-Control", "no-cache");
        }
        server.streamFile(file, contentType);
        file.close();
    } else {
        server.send(404, "text/plain", "Datei nicht gefunden: " + path);
    }
}

// ===== UI-Upload Handler =====

// Statisches Erfolgs-/Fehlerflag ueber Extraktion, Kopieren und Platzpruefung hinweg (A-HOCH-4)
static bool uiUploadSuccess = false;
static String uiUploadError = "";
static File uiUploadFile;

void handleUiUpload() {
    HTTPUpload& upload = server.upload();
    static String uploadPath;
    static bool isTgzFile = false;
    static String extractedVersion;

    if (upload.status == UPLOAD_FILE_START) {
        uiUploadSuccess = false;
        uiUploadError = "";
        extractedVersion = "";
        uploadPath = "";

        String filename = upload.filename;
        Serial.printf("UI Upload: %s\n", filename.c_str());
        debug(String(F("UI Upload gestartet: ")) + filename);

        // Check if it's a TGZ file
        isTgzFile = filename.endsWith(".tgz") || filename.endsWith(".tar.gz");
        if (!isTgzFile) {
            debug(F("Fehler: Keine TGZ-Datei"));
            uiUploadError = "Keine TGZ-Datei";
            return;
        }

        // Platzpruefung vor Upload-Start — freie Bytes muessen mind. 3x Content-Length sein
        // (Original-TGZ + entpackte Dateien + Backup)
        int contentLength = server.clientContentLength();
        if (contentLength > 0) {
            uint64_t total, used, free;
            getStorageInfo(total, used, free);
            if (free < (uint64_t)contentLength * 3) {
                debug(String(F("Fehler: Nicht genuegend freier Speicherplatz fuer Upload (")) +
                      String((unsigned long)free) + F(" frei, ") + String((unsigned long)contentLength * 3) + F(" benoetigt)"));
                uiUploadError = "Nicht genuegend freier Speicherplatz";
                isTgzFile = false;
                return;
            }
        }

        // Extract version — nur bei UI-Praefix (Firmware-Dateien haben anderes Namensschema)
        if (filename.startsWith("UI-") && filename.length() > 3) {
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
                uiUploadError = "Konnte /temp nicht erstellen";
                return;
            }
        }

        if (!LittleFS.exists("/extract")) {
            debug(F("Erstelle Verzeichnis /extract"));
            if (!LittleFS.mkdir("/extract")) {
                debug(F("Fehler beim Erstellen des /extract Verzeichnisses"));
                uiUploadError = "Konnte /extract nicht erstellen";
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

        // Datei-Handle EINMAL pro Upload oeffnen (nicht bei jedem WRITE neu) — A-MITTEL Upload-Robustheit
        uiUploadFile = LittleFS.open(uploadPath, "w");
        if (!uiUploadFile) {
            debug(F("Fehler beim Erstellen der TGZ-Datei"));
            uiUploadError = "Konnte TGZ-Datei nicht erstellen";
            return;
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE && isTgzFile) {
        if (uiUploadFile) {
            size_t bytesWritten = uiUploadFile.write(upload.buf, upload.currentSize);
            if (bytesWritten != upload.currentSize) {
                debug(String(F("Fehler beim Schreiben: Nur ")) + bytesWritten + F(" von ") + upload.currentSize + F(" Bytes geschrieben"));
                uiUploadError = "Fehler beim Schreiben der Upload-Datei";
            }
        } else {
            debug(F("Fehler: TGZ-Datei-Handle nicht offen zum Schreiben"));
            uiUploadError = "Upload-Datei nicht offen";
        }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED) {
        debug(F("UI Upload abgebrochen"));
        if (uiUploadFile) {
            uiUploadFile.close();
        }
        if (uploadPath.length() > 0 && LittleFS.exists(uploadPath)) {
            LittleFS.remove(uploadPath);
        }
        isTgzFile = false;
        uploadPath = "";
        uiUploadError = "Upload abgebrochen";
    }
    else if (upload.status == UPLOAD_FILE_END && isTgzFile) {
        if (uiUploadFile) {
            uiUploadFile.close();
        }

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

            // /extract nach erfolgreicher Installation rekursiv leeren
            debug(F("Leere /extract nach erfolgreicher Installation"));
            deleteDir("/extract");
            LittleFS.mkdir("/extract");
            LittleFS.mkdir("/extract/css");
            LittleFS.mkdir("/extract/js");

            debug(F("UI-Update erfolgreich!"));
            uiUploadSuccess = true;
        } else {
            int errorCode = TARGZUnpacker->tarGzGetError();
            debug(String(F("Extraktion fehlgeschlagen mit Fehler: ")) + errorCode);

            switch (errorCode) {
                case -1: uiUploadError = "Allgemeiner TAR-Lesefehler"; break;
                case -2: uiUploadError = "Nicht genuegend Speicher fuer TAR-Extraktion"; break;
                case -3: uiUploadError = "TAR-Header fehlerhaft"; break;
                case -4: uiUploadError = "TAR-Datei fehlerhaft"; break;
                case -5: uiUploadError = "Fehler beim Schreiben der extrahierten Dateien"; break;
                default: uiUploadError = "Unbekannter Fehlercode " + String(errorCode); break;
            }
            debug(String(F("Fehler: ")) + uiUploadError);
        }

        // Clean up
        debug(F("Aufräumen nach UI-Update"));
        LittleFS.remove(uploadPath);
        uploadPath = "";

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
    snprintf(uptimeStr, sizeof(uptimeStr), "%dd %dh %dm %ds", days, hours, minutes, seconds);
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
    snprintf(hexColor, sizeof(hexColor), "#%02X%02X%02X", r, g, b);
    doc["ledColor"] = hexColor;

    // Perzentil-Daten für Dashboard
    if (appState.initialAnalysisDone) {
        doc["percentile"] = appState.percentile;
        doc["ledIndex"] = appState.currentLedIndex;
        doc["headlinesAnalyzed"] = appState.headlinesAnalyzed;
        JsonObject thresholds = doc["thresholds"].to<JsonObject>();
        thresholds["p20"] = appState.thresholdP20;
        thresholds["p40"] = appState.thresholdP40;
        thresholds["p60"] = appState.thresholdP60;
        thresholds["p80"] = appState.thresholdP80;
        thresholds["fallback"] = appState.thresholdFallback;
        // LED-Farben für dynamische Perzentil-Grafik
        JsonArray colors = doc["ledColors"].to<JsonArray>();
        for (int i = 0; i < 5; i++) {
            char hex[8];
            snprintf(hex, sizeof(hex), "#%06X", appState.customColors[i] & 0xFFFFFF);
            colors.add(hex);
        }
        JsonObject historical = doc["historical"].to<JsonObject>();
        historical["min"] = appState.histMin;
        historical["max"] = appState.histMax;
        historical["median"] = appState.histMedian;
        historical["count"] = appState.histCount;
    }

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
    int hours = 168;
    if (server.hasArg("hours")) {
        hours = server.arg("hours").toInt();
    }
    hours = constrain(hours, 1, 720);  // A-HOCH-3: Clamp gegen OOM bei grossen Backend-Antworten

    JsonDocument doc;
    if (fetchBackendStatistics(doc, hours)) {
        // String statt Pool-Buffer — History-Daten koennen >16KB sein
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
        debug(String(F("Stats from backend sent: ")) + response.length() + F(" bytes"));
    } else {
        debug(F("Backend statistics fetch failed, sending 503"));
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
    // Statische Dateien aus LittleFS
    server.on("/", HTTP_GET, []()
              { handleStaticFile("/index.html"); });

    server.on("/setup", HTTP_GET, []()
              { handleStaticFile("/setup.html"); });

    server.on("/api/restart-counter", HTTP_GET, []() {
        Preferences prefs;
        prefs.begin("syshealth", true);
        unsigned long restarts = prefs.getULong("restarts", 0);
        prefs.end();
        server.send(200, "application/json", "{\"restarts\":" + String(restarts) + "}");
    });

    server.on("/api/reset-restart-counter", HTTP_GET, []() {
        Preferences prefs;
        prefs.begin("syshealth", false);
        prefs.putULong("restarts", 0);
        prefs.end();
        server.send(200, "application/json", "{\"status\":\"ok\",\"restarts\":0}");
        debug(F("Reboot-Counter zurückgesetzt"));
    });

    server.on("/mood", HTTP_GET, []()
              { handleStaticFile("/mood.html"); });

    // System diagnostics API endpoints (diagnostics page removed, APIs kept for debugging)
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
        doc["fsPercent"] = (total > 0) ? ((float)used * 100.0 / total) : 0;

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
        bool filesystemOk = (total == 0) || (((float)used * 100.0 / total) < 80.0);
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

        // A-MITTEL: advance-then-delete-Muster (wie handleUiUpload Z. 429-434) —
        // openNextFile() MUSS vor dem Loeschen/Schliessen der aktuellen Datei erfolgen,
        // sonst wird der Verzeichnis-Iterator auf einem bereits geschlossenen Handle
        // fortgesetzt (undefiniertes Verhalten, ueberspringt oder wiederholt Eintraege).
        if (LittleFS.exists("/temp")) {
            File root = LittleFS.open("/temp");
            if (root && root.isDirectory()) {
                File file = root.openNextFile();
                while (file) {
                    String filePath = String(file.path());
                    file = root.openNextFile(); // Naechste Datei VOR dem Loeschen holen
                    if (LittleFS.remove(filePath)) {
                        cleanedFiles++;
                        debug(String(F("Gelöscht: ")) + filePath);
                    }
                }
            }
        }

        if (LittleFS.exists("/data")) {
            File dataDir = LittleFS.open("/data");
            if (dataDir && dataDir.isDirectory()) {
                File file = dataDir.openNextFile();
                while (file) {
                    String filePath = String(file.path());
                    file = dataDir.openNextFile(); // Naechste Datei VOR dem Loeschen holen
                    // /data/settings.json.bak NICHT loeschen — Recovery-Backup
                    if (filePath.endsWith(".bak")) {
                        continue;
                    }
                    if (filePath.endsWith(".tmp")) {
                        if (LittleFS.remove(filePath)) {
                            cleanedFiles++;
                            debug(String(F("Gelöscht: ")) + filePath);
                        }
                    }
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
        doc["freeSpacePercent"] = (total > 0) ? ((float)free * 100.0 / total) : 0;

        char* jsonBuffer = jsonPool.acquire();
        size_t len = serializeJson(doc, jsonBuffer, JSON_BUFFER_SIZE);
        server.send(200, "application/json", jsonBuffer);
        jsonPool.release(jsonBuffer);
    });

    // API-Endpunkte für dynamische Daten
    server.on("/api/status", HTTP_GET, handleApiStatus);
    server.on("/api/stats", HTTP_GET, handleApiStats);
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
            server.sendHeader("Cache-Control", "public, max-age=86400");
            server.streamFile(file, "image/x-icon");
            file.close();
        } else {
            server.sendHeader("Cache-Control", "public, max-age=86400");
            server.send(204); // No content
        }
    });

    // For CSS and JS files that might be missing
    server.on("/css/style.css", HTTP_GET, []() { handleStaticFile("/css/style.css"); });
    server.on("/css/mood.css", HTTP_GET, []() { handleStaticFile("/css/mood.css"); });
    server.on("/js/script.js", HTTP_GET, []() { handleStaticFile("/js/script.js"); });
    server.on("/js/mood.js", HTTP_GET, []() { handleStaticFile("/js/mood.js"); });
    server.on("/js/setup.js", HTTP_GET, []() { handleStaticFile("/js/setup.js"); });

    server.on("/ui-upload", HTTP_POST,
        []() {
            // A-HOCH-4: Completion-Handler wertet das statische Erfolgs-/Fehlerflag aus
            // statt bedingungslos 200 zu senden
            if (uiUploadSuccess) {
                server.send(200, "text/html", "<html><body><h1>UI Update Complete</h1><a href='/setup'>Return to Setup</a></body></html>");
            } else {
                String errMsg = uiUploadError.length() > 0 ? uiUploadError : "Unbekannter Fehler";
                server.send(500, "text/plain; charset=utf-8", "UI-Update fehlgeschlagen: " + errMsg);
            }
        },
        handleUiUpload
    );

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
    // A-NIEDRIG: /api/export/settings entfernt — verwaist, keine UI-Referenz

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
            snprintf(hexColor, sizeof(hexColor), "#%06X", color & 0xFFFFFF);
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
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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

        // Werte aus JSON extrahieren und validieren
        String newSsid = doc["ssid"].as<String>();
        String newPass = doc["pass"].as<String>();

        if (newSsid.length() < 1 || newSsid.length() > 32) {
            debug(F("Ungueltige SSID-Laenge"));
            server.send(400, "text/plain", "SSID muss 1-32 Zeichen lang sein");
            return;
        }
        if (newPass.length() > 63) {
            debug(F("Ungueltige WiFi-Passwort-Laenge"));
            server.send(400, "text/plain", "Passwort darf maximal 63 Zeichen lang sein");
            return;
        }

        appState.wifiSSID = newSsid;
        appState.wifiPassword = newPass;
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

        bool changed = false;

        // Werte aus JSON extrahieren — nur vorhandene Keys uebernehmen
        if (doc["enabled"].is<bool>()) {
            bool newEnabled = doc["enabled"].as<bool>();
            if (newEnabled != appState.mqttEnabled) {
                appState.mqttEnabled = newEnabled;
                changed = true;
            }
        }
        if (doc["server"].is<const char*>()) {
            String newServer = doc["server"].as<String>();
            if (newServer != appState.mqttServer) {
                appState.mqttServer = newServer;
                changed = true;
            }
        }
        if (doc["user"].is<const char*>()) {
            String newUser = doc["user"].as<String>();
            if (newUser != appState.mqttUser) {
                appState.mqttUser = newUser;
                changed = true;
            }
        }
        // Passwort nur uebernehmen wenn nicht leer/maskiert — sonst wuerde die
        // Maske "****" das echte Passwort ueberschreiben (A-HOCH-2)
        if (doc["pass"].is<const char*>()) {
            String newPass = doc["pass"].as<String>();
            if (newPass != "****" && newPass.length() > 0 && newPass != appState.mqttPassword) {
                appState.mqttPassword = newPass;
                changed = true;
            }
        }

        if (changed) {
            // Einstellungen speichern
            appState.settingsNeedSaving = true;
            appState.lastSettingsSaved = millis();

            // Reboot planen — nur bei echter Aenderung
            appState.rebootNeeded = true;
            appState.rebootTime = millis() + REBOOT_DELAY;

            server.send(200, "text/plain", "OK");
            debug(F("MQTT-Einstellungen gespeichert, Reboot geplant"));
        } else {
            debug(F("MQTT-Einstellungen: Keine Aenderungen erkannt."));
            server.send(200, "text/plain; charset=utf-8", "Keine Änderungen");
        }
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
            if (newApiUrl.length() == 0 || !newApiUrl.startsWith("http")) {
                debug(F("Ungueltige API-URL abgelehnt"));
                server.send(400, "text/plain", "API-URL muss mit http/https beginnen");
                return;
            }
            if (newApiUrl != appState.apiUrl) {
                appState.apiUrl = newApiUrl;
                appState.lastMoodUpdate = 0;  // Erzwinge Sentiment-Update bei nächster Gelegenheit
                changed = true;
                debug(String(F("API URL geändert zu: ")) + appState.apiUrl);
            }
        }
        if (doc["moodInterval"].is<float>()) {
            unsigned long moodSeconds = constrain(doc["moodInterval"].as<unsigned long>(), 10UL, 7200UL);  // Mind. 10s, Max 2h — vor der Multiplikation clampen (32-Bit-Overflow-Schutz)
            unsigned long newMoodInterval = 1000UL * moodSeconds;
            if (newMoodInterval != appState.moodUpdateInterval) {
                appState.moodUpdateInterval = newMoodInterval;
                changed = true;
                debug(String(F("Mood Interval geändert zu: ")) + String(appState.moodUpdateInterval / 1000) + F("s"));
            }
        }
        if (doc["dhtEnabled"].is<bool>() || doc["dhtEnabled"].is<int>() || doc["dhtEnabled"].is<float>()) {
            bool newDhtEnabled = doc["dhtEnabled"].is<bool>()
                ? doc["dhtEnabled"].as<bool>()
                : (doc["dhtEnabled"].as<float>() != 0);
            if (newDhtEnabled != appState.dhtEnabled) {
                appState.dhtEnabled = newDhtEnabled;
                changed = true;
                debug(String(F("DHT Enabled geändert zu: ")) + (appState.dhtEnabled ? "ja" : "nein"));
            }
        }
        // v9.0: headlinesPerSource removed - only for legacy API endpoints

        // NEU: DHT Intervall hier verarbeiten
        if (doc["dhtInterval"].is<float>()) {
            unsigned long dhtSeconds = constrain(doc["dhtInterval"].as<unsigned long>(), 10UL, 3600UL);  // Mind. 10s, Max 1h — vor der Multiplikation clampen (32-Bit-Overflow-Schutz)
            unsigned long newDhtInterval = 1000UL * dhtSeconds;
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
            server.send(200, "text/plain; charset=utf-8", "Keine Änderungen");
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

        // Farben aus JSON extrahieren — in temporaeres Array parsen, nur bei
        // vollstaendigem Erfolg komplett uebernehmen (kein Teilzustand bei Parse-Fehler)
        if (doc["colors"].is<JsonArray>()) {
            JsonArray colorArray = doc["colors"].as<JsonArray>();
            uint32_t tempColors[5];
            int index = 0;

            bool parseError = false;
            for (JsonVariant colorValue : colorArray) {
                if (index < 5 && colorValue.is<String>()) {
                    String hexColor = colorValue.as<String>();
                    const char* hexStr = hexColor.c_str();
                    if (hexStr[0] == '#') hexStr++; // führendes '#' überspringen
                    uint32_t rgb = 0;
                    if (sscanf(hexStr, "%x", &rgb) != 1) {
                        debug(String(F("Ungültiger Farbwert ignoriert: ")) + hexColor);
                        parseError = true;
                        index++;
                        continue;
                    }
                    tempColors[index] = rgb & 0xFFFFFF;
                    index++;
                }
            }

            if (parseError || index != 5) {
                server.send(400, "text/plain; charset=utf-8", "Ungültiger Farbwert");
                return;
            }

            // Nur komplett uebernehmen
            for (int i = 0; i < 5; i++) {
                appState.customColors[i] = tempColors[i];
            }

            // Einstellungen speichern
            appState.settingsNeedSaving = true;
            appState.lastSettingsSaved = millis();

            server.send(200, "text/plain", "OK");
            debug(F("Farbeinstellungen gespeichert."));
        } else {
            server.send(400, "text/plain; charset=utf-8", "Ungültiges Farbformat");
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
        String rejectedPins = "";

        // Werte aus JSON extrahieren und prüfen, ob Änderung vorliegt
        // Pins 6-11 sind intern mit dem Flash verbunden — deren Nutzung würde das Gerät bricken
        if (doc["ledPin"].is<int>()) {
            int newLedPin = doc["ledPin"].as<int>();
            if (newLedPin >= 0 && newLedPin <= 39 && !(newLedPin >= 6 && newLedPin <= 11)) {
                if (newLedPin != appState.ledPin) {
                    appState.ledPin = newLedPin;
                    needsReboot = true;  // Pin-Änderung erfordert Neustart
                }
            } else {
                debug(String(F("Ungültiger LED-Pin ignoriert: ")) + newLedPin);
                rejectedPins += "ledPin=" + String(newLedPin) + " ";
            }
        }
        if (doc["dhtPin"].is<int>()) {
            int newDhtPin = doc["dhtPin"].as<int>();
            if (newDhtPin >= 0 && newDhtPin <= 39 && !(newDhtPin >= 6 && newDhtPin <= 11)) {
                if (newDhtPin != appState.dhtPin) {
                    appState.dhtPin = newDhtPin;
                    needsReboot = true;  // Pin-Änderung erfordert Neustart
                }
            } else {
                debug(String(F("Ungültiger DHT-Pin ignoriert: ")) + newDhtPin);
                rejectedPins += "dhtPin=" + String(newDhtPin) + " ";
            }
        }

        if (doc["numLeds"].is<int>()) {
            int newNumLeds = constrain(doc["numLeds"].as<int>(), 1, MAX_LEDS);
            if (newNumLeds != appState.numLeds) {
                appState.numLeds = newNumLeds;
                appState.statusLedIndex = appState.numLeds - 1;
                needsReboot = true;  // LED-Anzahl-Änderung erfordert Neustart
            }
        }

        // DHT Intervall wird hier NICHT mehr verarbeitet

        if (rejectedPins.length() > 0 && !needsReboot) {
            // Nur abgelehnte Pins, keine sonstige Aenderung — Fehler statt stillem Ignorieren melden
            server.send(400, "text/plain; charset=utf-8", "Ungueltige Pin-Werte abgelehnt: " + rejectedPins);
            return;
        }

        if (needsReboot) {
            // Einstellungen speichern (nur wenn relevant)
            appState.settingsNeedSaving = true;
            appState.lastSettingsSaved = millis();  // Speichert die geänderten Pins/LEDs

            // Reboot planen (ist für Pin/LED-Änderungen notwendig)
            appState.rebootNeeded = true;
            appState.rebootTime = millis() + REBOOT_DELAY;

            String response = "OK";
            if (rejectedPins.length() > 0) {
                response += " (Ungueltige Pins ignoriert: " + rejectedPins + ")";
            }
            server.send(200, "text/plain; charset=utf-8", response);
            debug(F("Hardware Pin/LED-Einstellungen gespeichert, Reboot geplant"));
        } else {
            debug(F("Hardware Pin/LED-Einstellungen: Keine Änderungen erkannt."));
            server.send(200, "text/plain; charset=utf-8", "Keine Änderungen");
        }
    });

    // Factory Reset - Alle Einstellungen zurücksetzen
    server.on("/factoryreset", HTTP_POST, []() {
        debug(F("Factory Reset angefordert"));

        // Preferences komplett löschen
        preferences.begin("moodlight", false);
        preferences.clear();
        preferences.end();

        // Persistierte JSON-Einstellungen löschen — sonst lädt loadSettings()
        // beim nächsten Boot bevorzugt die alte Datei und der Reset greift nicht
        if (LittleFS.exists("/data/settings.json")) {
            LittleFS.remove("/data/settings.json");
        }
        if (LittleFS.exists("/data/settings.json.bak")) {
            LittleFS.remove("/data/settings.json.bak");
        }

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
        logs.reserve(LOG_BUFFER_SIZE * 200); // Pre-allokieren um Fragmentierung zu vermeiden
        for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
            int idx = (appState.logIndex + i) % LOG_BUFFER_SIZE;
            if (appState.logBuffer[idx][0] != '\0') {
                logs += appState.logBuffer[idx];
                logs += "\n";
            }
        }
        server.send(200, "text/plain; charset=utf-8", logs);
    });

    // /status entfernt — Duplikat von /api/status, kein Aufrufer (A-NIEDRIG)

    // Force-Refresh fuer Sentiment — nutzt den gleichen Flag-Mechanismus wie HA-Button
    server.on("/refresh", HTTP_GET, []() {
        debug(F("Force-Update via Web — setze Flag fuer naechsten Loop"));
        // Antwort sofort senden
        server.send(200, "text/plain", "Refresh initiated");
        // Flag setzen, loop() fuehrt den Abruf sicher aus (wie beim HA-Button)
        appState.mqttRefreshPending = true;
    });

    // toggle-light Endpunkt
    server.on("/toggle-light", HTTP_GET, []() {
        appState.lightOn = !appState.lightOn;

        // Antwort senden
        server.send(200, "text/plain", "OK");

        // LED-Update ueber Mutex-geschuetzten Pfad (NICHT pixels.show() direkt!)
        updateLEDs();

        // Verzoegerte Speicherung
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
            const char* hexStr = hexColor.c_str();
            if (hexStr[0] == '#') hexStr++; // führendes '#' überspringen
            uint32_t rgb = 0;
            if (sscanf(hexStr, "%x", &rgb) != 1) {
                server.send(400, "text/plain; charset=utf-8", "Ungültiger Hex-Farbwert");
                return;
            }
            rgb &= 0xFFFFFF;

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
        snprintf(hexColor, sizeof(hexColor), "#%06X", appState.manualColor & 0xFFFFFF);
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
            snprintf(hexColorArr, sizeof(hexColorArr), "#%06X", appState.customColors[i] & 0xFFFFFF);
            colors.add(hexColorArr);
        }

        // Dateisystem-Informationen — LittleFS.begin() entfernt, FS ist bereits gemountet (A-NIEDRIG)
        {
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
            if (Update.hasError()) {
                server.send(500, "text/html", "<html><body><h1>Update Failed!</h1><a href='/'>Return to Homepage</a></body></html>");
                // Kein Restart bei fehlgeschlagenem Update
            } else {
                server.send(200, "text/html", "<html><body><h1>Update Successful!</h1><p>Device is restarting...</p><script>setTimeout(function(){window.location.href='/';}, 10000);</script></body></html>");
                delay(1000);
                ESP.restart();
            }
        },
        []() {
            HTTPUpload &upload = server.upload();
            static String extractedVersion = "";
            static bool magicByteChecked = false;

            if (upload.status == UPLOAD_FILE_START) {
                String filename = upload.filename;
                debug("Update: " + filename);
                magicByteChecked = false;

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
                // Bricking-Vorsorge: erster Chunk muss mit dem ESP32-Firmware-Magic-Byte
                // 0xE9 beginnen — verhindert das Flashen offensichtlich falscher Dateien
                if (!magicByteChecked) {
                    magicByteChecked = true;
                    if (upload.currentSize == 0 || upload.buf[0] != 0xE9) {
                        debug(F("ERROR: Ungültige Firmware-Datei (Magic Byte 0xE9 fehlt) - Update abgebrochen"));
                        Update.abort();
                        return;
                    }
                }
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
            else if (upload.status == UPLOAD_FILE_ABORTED) {
                debug(F("Firmware-Update abgebrochen"));
                Update.abort();
                setStatusLED(0);
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

    // server.begin() wird NICHT hier aufgerufen — das passiert in
    // connectWiFiAndStartServices() (STA) oder startAPModeWithServer() (AP)
    // nachdem WiFi korrekt initialisiert ist.
    debug(F("Webserver-Routen registriert"));
}

// A-NIEDRIG: initWatchdog() entfernt — toter Code, kein Aufrufer
// (WatchdogManager::begin() in moodlight.cpp::setup() ist der tatsaechlich genutzte Pfad)

// === Regelmäßiger System-Gesundheitscheck ===
void runSystemHealthCheck() {
    unsigned long currentMillis = millis();
    debug(F("Führe regelmäßige Systemprüfung durch..."));

    // Memory-Analyse durchführen
    memMonitor.update();

    // A-MITTEL Flash/RAM: sysstat-Schreibblock entfernt — niemand liest die Dateien
    // (statsDoc/sysstat_*.json), unnoetiger Flash-Verschleiss.

    // Systemgesundheit aktualisieren
    sysHealth.update();

    if (sysHealth.isRestartRecommended()) {
        debug(F("System empfiehlt Neustart - plane Neustart für 3:00 Uhr..."));

        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_hour >= NIGHT_REBOOT_HOUR_START && timeinfo.tm_hour < NIGHT_REBOOT_HOUR_END) {
            debug(F("Nachtstunden, führe Neustart sofort durch..."));
            appState.rebootNeeded = true;
            appState.rebootTime = currentMillis + NIGHT_REBOOT_DELAY;
        } else {
            debug(F("Neustart verschoben auf Nachtstunden..."));
            Preferences prefs;
            prefs.begin("syshealth", false);
            prefs.putBool("restartPending", true);
            prefs.end();
        }
    } else {
        // Selbstheilung: restartPending löschen wenn kein Neustart mehr nötig
        Preferences prefs;
        prefs.begin("syshealth", false);
        if (prefs.getBool("restartPending", false)) {
            debug(F("Neustart-Empfehlung aufgehoben, lösche restartPending-Flag"));
            prefs.putBool("restartPending", false);
        }
        prefs.end();
    }

    // Geplanten Neustart prüfen
    {
        Preferences prefs;
        prefs.begin("syshealth", true);
        bool restartPending = prefs.getBool("restartPending", false);
        prefs.end();

        if (restartPending) {
            time_t now;
            time(&now);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);

            if (timeinfo.tm_hour >= SCHEDULED_REBOOT_HOUR_START && timeinfo.tm_hour < SCHEDULED_REBOOT_HOUR_END) {
                debug(F("Geplanter Neustart wird ausgeführt..."));
                appState.rebootNeeded = true;
                appState.rebootTime = currentMillis + SCHEDULED_REBOOT_DELAY;

                Preferences prefs2;
                prefs2.begin("syshealth", false);
                prefs2.putBool("restartPending", false);
                prefs2.end();
            }
        }
    }

    // Speicherplatz-Prüfung
    uint64_t total, used, free;
    getStorageInfo(total, used, free);
    float percentUsed = (total > 0) ? ((float)used * 100.0 / total) : 0;

    if (percentUsed > STORAGE_WARNING_PERCENT) {
        debug(F("Hohe Dateisystembelegung erkannt"));
    }

    appState.lastSystemHealthCheckTime = currentMillis;
}
