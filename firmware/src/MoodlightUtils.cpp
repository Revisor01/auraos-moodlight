// MoodlightUtils.cpp - Implementierung der Moodlight-Utilities

#include "MoodlightUtils.h"
#include <time.h>

// ===== WATCHDOG-MANAGER IMPLEMENTIERUNG =====

WatchdogManager::WatchdogManager() : 
    _monitoredTask(NULL), 
    _lastFeedTime(0), 
    _feedInterval(15000), 
    _isEnabled(false) 
{
}

bool WatchdogManager::begin(uint32_t timeoutSeconds, bool panicOnTimeout) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms    = timeoutSeconds * 1000,
        .idle_core_mask = 0,
        .trigger_panic  = panicOnTimeout
    };
    // Versuche init — wenn schon initialisiert, reconfigure stattdessen
    esp_err_t err = esp_task_wdt_init(&wdt_config);
    if (err == ESP_ERR_INVALID_STATE) {
        err = esp_task_wdt_reconfigure(&wdt_config);
    }
#else
    esp_err_t err = esp_task_wdt_init(timeoutSeconds, panicOnTimeout);
#endif
    if (err != ESP_OK) {
        debug(F("Fehler bei der Initialisierung des Watchdogs"));
        return false;
    }

    _isEnabled = true;
    _lastFeedTime = millis();
    debug(String(F("Watchdog initialisiert: ")) + timeoutSeconds + "s Timeout");
    return true;
}

bool WatchdogManager::registerCurrentTask() {
    if (!_isEnabled) return false;
    
    // Erhalte den aktuellen Task-Handle (in der Regel der Loop-Task)
    _monitoredTask = xTaskGetCurrentTaskHandle();
    if (_monitoredTask == NULL) {
        debug(F("Konnte Task-Handle nicht erhalten"));
        return false;
    }
    
    // Registriere Task beim Watchdog
    esp_err_t err = esp_task_wdt_add(_monitoredTask);
    if (err != ESP_OK) {
        debug(F("Fehler beim Registrieren des Tasks beim Watchdog"));
        return false;
    }
    
    debug(F("Task erfolgreich beim Watchdog registriert"));
    return true;
}

void WatchdogManager::feed() {
    if (!_isEnabled) return;
    
    esp_task_wdt_reset();
    _lastFeedTime = millis();
}

bool WatchdogManager::autoFeed(unsigned long interval) {
    if (!_isEnabled) return false;
    
    _feedInterval = interval;
    unsigned long currentTime = millis();
    
    if (currentTime - _lastFeedTime >= _feedInterval) {
        feed();
        return true;
    }
    
    return false;
}

void WatchdogManager::analyzeStack() {
    if (_monitoredTask == NULL) {
        debug(F("Kein überwachter Task für Stack-Analyse"));
        return;
    }
    
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(_monitoredTask);
    debug(String(F("Task Stack High Water Mark: ")) + stackHighWaterMark + F(" Wörter"));
    
    if (stackHighWaterMark < 200) {
        debug(F("WARNUNG: Stack wird knapp! Stacküberlauf möglich"));
    }
}

void WatchdogManager::disable() {
    if (!_isEnabled) return;
    
    if (_monitoredTask != NULL) {
        esp_task_wdt_delete(_monitoredTask);
        _monitoredTask = NULL;
    }
    
    _isEnabled = false;
    debug(F("Watchdog deaktiviert"));
}

// ===== MEMORY-MONITOR IMPLEMENTIERUNG =====

MemoryMonitor::MemoryMonitor() :
    _lastFreeHeap(ESP.getFreeHeap()),
    _lowestHeap(ESP.getFreeHeap()),
    _lastPersistedLowestHeap(ESP.getFreeHeap()),
    _lastReportTime(0),
    _reportInterval(60000),
    _lastNvsWriteTime(0),
    _startTime(millis()),
    _isEnabled(false)
{
}

bool MemoryMonitor::begin(unsigned long reportInterval) {
    _reportInterval = reportInterval;
    _startTime = millis();
    _lastFreeHeap = ESP.getFreeHeap();
    _lowestHeap = _lastFreeHeap;
    _lastReportTime = 0;
    _lastNvsWriteTime = millis();
    _isEnabled = true;

    // Lade gespeicherten niedrigsten Heap-Wert, falls vorhanden
    _prefs.begin("memMonitor", false);
    size_t savedLowest = _prefs.getULong("lowestHeap", 0);
    if (savedLowest > 0 && savedLowest < _lowestHeap) {
        _lowestHeap = savedLowest;
    }
    _prefs.end();
    _lastPersistedLowestHeap = _lowestHeap;

    debug(String(F("Speicher-Monitor initialisiert. Initiale Heap-Größe: ")) + formatBytes(_lastFreeHeap));
    return true;
}

void MemoryMonitor::update() {
    if (!_isEnabled) return;

    unsigned long currentTime = millis();
    size_t currentFree = ESP.getFreeHeap();

    // Aktualisiere niedrigsten Wert (im RAM immer aktuell halten)
    if (currentFree < _lowestHeap) {
        _lowestHeap = currentFree;
    }

    // NVS-Write throttlen: max. alle 10 Minuten ODER wenn sich der Tiefstwert
    // um mehr als 1 KB seit dem letzten Schreiben verändert hat (A10)
    bool intervalElapsed = (currentTime - _lastNvsWriteTime) >= 600000UL; // 10 min
    bool significantDelta = (_lastPersistedLowestHeap > _lowestHeap) &&
                             ((_lastPersistedLowestHeap - _lowestHeap) > 1024);
    if (_lowestHeap < _lastPersistedLowestHeap && (intervalElapsed || significantDelta)) {
        _prefs.begin("memMonitor", false);
        _prefs.putULong("lowestHeap", _lowestHeap);
        _prefs.end();
        _lastPersistedLowestHeap = _lowestHeap;
        _lastNvsWriteTime = currentTime;
    }

    // Berichte periodisch oder bei signifikanten Änderungen
    if (currentTime - _lastReportTime >= _reportInterval || 
        abs((int)(currentFree - _lastFreeHeap)) > 10240) { // 10KB Änderung
        
        size_t maxBlock = ESP.getMaxAllocHeap();
        float fragmentationPercent = 100.0 - (maxBlock * 100.0 / currentFree);
        
        debug(String(F("Speicher: Frei=")) + formatBytes(currentFree) + 
              F(", Max Block=") + formatBytes(maxBlock) + 
              F(", Niedrigster=") + formatBytes(_lowestHeap) + 
              F(", Fragmentierung=") + fragmentationPercent + F("%"));
        
        _lastFreeHeap = currentFree;
        _lastReportTime = currentTime;
    }
}

bool MemoryMonitor::checkHeapBefore(const char* operation, size_t requiredFree) {
    size_t currentFree = ESP.getFreeHeap();
    if (currentFree < requiredFree) {
        debug(String(F("WARNUNG: Zu wenig Speicher für ")) + operation + 
              F(" (") + formatBytes(currentFree) + F(" verfügbar, ") + 
              formatBytes(requiredFree) + F(" benötigt)"));
        return false;
    }
    return true;
}

void MemoryMonitor::diagnose() {
    size_t currentFree = ESP.getFreeHeap();
    size_t maxBlock = ESP.getMaxAllocHeap();
    float fragmentationPercent = 100.0 - (maxBlock * 100.0 / currentFree);
    unsigned long uptime = (millis() - _startTime) / 1000;
    
    debug(F("===== SPEICHER-DIAGNOSE ====="));
    debug(String(F("Aktuell frei: ")) + formatBytes(currentFree));
    debug(String(F("Größter Block: ")) + formatBytes(maxBlock));
    debug(String(F("Niedrigster Heap: ")) + formatBytes(_lowestHeap));
    debug(String(F("Fragmentierung: ")) + fragmentationPercent + F("%"));
    debug(String(F("Programm läuft seit: ")) + MoodlightUtils::formatTime(uptime * 1000));
    
    // Heap-Histogramm für detailliertere Analyse
    size_t blocks[6] = {0}; // Anzahl der Blöcke in verschiedenen Größenkategorien
    size_t testSizes[6] = {64, 256, 1024, 4096, 16384, 65536};
    
    debug(F("Heap-Blockgrößen-Test:"));
    for (int i = 0; i < 6; i++) {
        // Versuche, Blöcke dieser Größe zu allozieren
        void* testBlock = malloc(testSizes[i]);
        if (testBlock) {
            blocks[i] = 1;
            free(testBlock);
            debug(String(F("  ")) + formatBytes(testSizes[i]) + F(": Verfügbar"));
        } else {
            debug(String(F("  ")) + formatBytes(testSizes[i]) + F(": Nicht verfügbar"));
        }
    }
    
    // Empfehlungen basierend auf der Analyse
    if (fragmentationPercent > 70) {
        debug(F("WARNUNG: Hohe Fragmentierung. Neustart könnte helfen."));
    }
    
    if (currentFree < 20000) {
        debug(F("WARNUNG: Wenig freier Speicher. Vermeide große Allokationen."));
    }
    
    debug(F("============================"));
}

size_t MemoryMonitor::getLowestHeap() const {
    return _lowestHeap;
}

String MemoryMonitor::formatBytes(size_t bytes) {
    if (bytes < 1024) {
        return String(bytes) + F(" B");
    } else if (bytes < (1024 * 1024)) {
        return String(bytes / 1024.0, 1) + F(" KB");
    } else {
        return String(bytes / 1024.0 / 1024.0, 2) + F(" MB");
    }
}

// ===== SAFE-FILE-OPS IMPLEMENTIERUNG =====

SafeFileOps::SafeFileOps(bool enableBackup) : 
    _backupEnabled(enableBackup), 
    _tempSuffix(".tmp"), 
    _backupSuffix(".bak") 
{
}

String SafeFileOps::readFile(const char* path, int maxRetries) {
    String result = "";
    int attempts = 0;
    
    while (attempts < maxRetries) {
        if (!LittleFS.exists(path)) {
            debug(String(F("Datei nicht gefunden: ")) + path);
            return "";
        }
        
        File file = LittleFS.open(path, "r");
        if (!file) {
            debug(String(F("Konnte Datei nicht öffnen: ")) + path + F(" (Versuch ") + 
                  (attempts+1) + F("/") + maxRetries + F(")"));
            attempts++;
            delay(50); // Kurze Pause vor erneutem Versuch
            continue;
        }
        
        // Prüfe Dateigröße und Speicherverfügbarkeit
        size_t fileSize = file.size();
        size_t freeHeap = ESP.getFreeHeap();
        
        if (fileSize > freeHeap / 2) {
            debug(String(F("WARNUNG: Datei zu groß für sicheres Laden: ")) + fileSize + 
                  F("B, Freier Heap: ") + freeHeap + F("B"));
            
            // Lese die Datei in Blöcken statt als Ganzes
            const size_t bufferSize = 512;
            char buffer[bufferSize];
            size_t bytesRead = 0;
            
            while (file.available()) {
                bytesRead = file.readBytes(buffer, bufferSize - 1);
                buffer[bytesRead] = '\0'; // Null-Terminierung
                result += buffer;
                yield(); // Gib anderen Tasks Zeit
            }
        } else {
            // Für kleine Dateien ist es sicher, sie komplett zu lesen
            result = file.readString();
        }
        
        file.close();
        return result;
    }
    
    return "";
}

bool SafeFileOps::writeFile(const char* path, const String& content) {
    // Backup erstellen, falls die Datei existiert und Backups aktiviert sind
    if (_backupEnabled && LittleFS.exists(path)) {
        String backupPath = String(path) + _backupSuffix;
        if (LittleFS.exists(backupPath)) {
            LittleFS.remove(backupPath);
        }
        
        // Kopiere aktuelle Datei als Backup
        if (!copyFile(path, backupPath)) {
            debug(String(F("WARNUNG: Konnte kein Backup von ")) + path + F(" erstellen"));
            // Fahre trotzdem fort, aber mit Vorsicht
        }
    }
    
    // Schreibe zunächst in eine temporäre Datei
    String tempPath = String(path) + _tempSuffix;
    if (LittleFS.exists(tempPath)) {
        LittleFS.remove(tempPath);
    }
    
    File tempFile = LittleFS.open(tempPath.c_str(), "w");
    if (!tempFile) {
        debug(String(F("Fehler: Konnte temporäre Datei nicht erstellen: ")) + tempPath);
        return false;
    }
    
    // Schreibe den Inhalt in Blöcken, um den Speicherverbrauch zu reduzieren
    const size_t chunkSize = 256;
    size_t totalLength = content.length();
    bool writeSuccess = true;
    
    for (size_t i = 0; i < totalLength; i += chunkSize) {
        size_t endPos = min(i + chunkSize, totalLength);
        String chunk = content.substring(i, endPos);
        
        if (!tempFile.print(chunk)) {
            writeSuccess = false;
            debug(String(F("Fehler beim Schreiben von Chunk ")) + i + F(" zu ") + endPos);
            break;
        }
        
        // Bei längeren Dateien regelmäßig yield() aufrufen
        if (i % 1024 == 0) {
            yield();
        }
    }
    
    tempFile.close();
    
    if (!writeSuccess) {
        LittleFS.remove(tempPath.c_str());
        debug(String(F("Schreiben in temporäre Datei fehlgeschlagen: ")) + tempPath);
        return false;
    }
    
    // Lösche Zieldatei, wenn sie existiert
    if (LittleFS.exists(path)) {
        if (!LittleFS.remove(path)) {
            debug(String(F("Konnte die alte Datei nicht entfernen: ")) + path);
            LittleFS.remove(tempPath.c_str());
            return false;
        }
    }
    
    // Benenne temp Datei zum finalen Namen um
    if (LittleFS.rename(tempPath.c_str(), path)) {
        return true;
    } else {
        debug(String(F("Umbenennen der temporären Datei fehlgeschlagen: ")) + tempPath + F(" -> ") + path);
        
        // Versuche Backup wiederherzustellen, wenn vorhanden
        if (_backupEnabled) {
            String backupPath = String(path) + _backupSuffix;
            if (LittleFS.exists(backupPath)) {
                if (LittleFS.rename(backupPath.c_str(), path)) {
                    debug(F("Backup wiederhergestellt nach Schreibfehler"));
                }
            }
        }
        
        return false;
    }
}

bool SafeFileOps::copyFile(const String& source, const String& destination) {
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

bool SafeFileOps::checkSpaceBefore(size_t requiredBytes) {
    size_t freeSpace = LittleFS.totalBytes() - LittleFS.usedBytes();
    if (freeSpace < requiredBytes) {
        debug(String(F("WARNUNG: Nicht genügend freier Speicherplatz: ")) + 
              freeSpace + F(" verfügbar, ") + requiredBytes + F(" benötigt"));
        return false;
    }
    return true;
}

void SafeFileOps::listDir(const char* dirname, uint8_t levels) {
    if (levels > 3) return; // Begrenzung der Rekursionstiefe
    
    File root = LittleFS.open(dirname);
    if (!root) {
        debug(String(F("Konnte Verzeichnis nicht öffnen: ")) + dirname);
        return;
    }
    
    if (!root.isDirectory()) {
        debug(String(F("Nicht ein Verzeichnis: ")) + dirname);
        root.close();
        return;
    }
    
    File file = root.openNextFile();
    while (file) {
        String indent = "";
        for (int i = 0; i < levels; i++) {
            indent += "  ";
        }
        
        String filePath = String(file.path());
        
        if (file.isDirectory()) {
            debug(indent + F("DIR: ") + filePath);
            file.close();
            listDir(filePath.c_str(), levels + 1);
        } else {
            String fileSize = "";
            if (file.size() < 1024) {
                fileSize = String(file.size()) + F(" B");
            } else if (file.size() < 1024 * 1024) {
                fileSize = String(file.size() / 1024.0, 1) + F(" KB");
            } else {
                fileSize = String(file.size() / 1024.0 / 1024.0, 1) + F(" MB");
            }
            
            debug(indent + filePath + F(" (") + fileSize + F(")"));
            file.close();
        }
        
        file = root.openNextFile();
    }
    
    root.close();
}

// ===== NETWORK-DIAGNOSTICS IMPLEMENTIERUNG =====

NetworkDiagnostics::NetworkDiagnostics() : 
    _lastFullAnalysis(0), 
    _analysisInterval(3600000), // 1 Stunde 
    _lastRssi(0) 
{
}

bool NetworkDiagnostics::begin(unsigned long intervalMs) {
    _analysisInterval = intervalMs;
    _lastFullAnalysis = 0;
    return true;
}

void NetworkDiagnostics::analyzeWiFiSignal() {
    if (WiFi.status() != WL_CONNECTED) {
        debug(F("WiFi nicht verbunden, keine Signalanalyse möglich"));
        return;
    }
    
    int rssi = WiFi.RSSI();
    _lastRssi = rssi;
    String quality;
    int qualityPercent;
    
    // RSSI zu Prozent und Qualitätslevel konvertieren
    if (rssi <= -100) {
        qualityPercent = 0;
        quality = F("Sehr schlecht");
    } else if (rssi >= -50) {
        qualityPercent = 100;
        quality = F("Ausgezeichnet");
    } else {
        qualityPercent = 2 * (rssi + 100);
        
        if (qualityPercent > 80) quality = F("Ausgezeichnet");
        else if (qualityPercent > 60) quality = F("Gut");
        else if (qualityPercent > 40) quality = F("Mittel");
        else if (qualityPercent > 20) quality = F("Schwach");
        else quality = F("Sehr schwach");
    }
    
    debug(String(F("WiFi-Signalanalyse: RSSI=")) + rssi + 
          F(" dBm, Qualität=") + quality + 
          F(" (") + qualityPercent + F("%)"));
    
    // Kanal-Informationen erfassen
    int channel = WiFi.channel();
    debug(String(F("WiFi-Kanal: ")) + channel);
    
    // Prüfen auf kritisch schwaches Signal
    if (qualityPercent < 30) {
        debug(F("WARNUNG: Sehr schwache WiFi-Signalstärke erkannt!"));
    }
}

void NetworkDiagnostics::quickCheck() {
    if (WiFi.status() != WL_CONNECTED) {
        debug(F("WiFi ist nicht verbunden"));
        return;
    }
    
    int rssi = WiFi.RSSI();
    if (abs(rssi - _lastRssi) > 5) {
        debug(String(F("WiFi-Signalstärke geändert: ")) + _lastRssi + F(" dBm -> ") + rssi + F(" dBm"));
        _lastRssi = rssi;
    }
}

void NetworkDiagnostics::fullAnalysis() {
    if (WiFi.status() != WL_CONNECTED) {
        debug(F("WiFi nicht verbunden, keine Netzwerkanalyse möglich"));
        return;
    }

    debug(F("Starte vollständige Netzwerkanalyse..."));

    // Grundlegende Netzwerkinfos
    analyzeWiFiSignal();

    // A-MITTEL: Stuendlicher aktiver WiFi-Scan entfernt (blockiert loop() bis zu mehrere
    // Sekunden). Nur RSSI/Kanal des aktuellen Netzwerks loggen — Scan bleibt on-demand
    // ueber den /wifiscan-Endpunkt verfuegbar.
    int channel = WiFi.channel();
    debug(String(F("Aktueller Kanal: ")) + channel);

    // Router-Verbindungsinformationen
    IPAddress gatewayIP = WiFi.gatewayIP();
    debug(String(F("Gateway-IP: ")) + gatewayIP.toString());

    // DNS-Informationen
    IPAddress dns1 = WiFi.dnsIP(0);
    IPAddress dns2 = WiFi.dnsIP(1);
    debug(String(F("DNS-Server: ")) + dns1.toString() + F(", ") + dns2.toString());

    _lastFullAnalysis = millis();
}

bool NetworkDiagnostics::updateCheck() {
    unsigned long currentTime = millis();
    
    // Vollständige Analyse beim Intervall
    if (currentTime - _lastFullAnalysis >= _analysisInterval) {
        fullAnalysis();
        return true;
    }
    
    // Schnelle Überprüfung alle 5 Minuten
    static unsigned long lastQuickCheck = 0;
    if (currentTime - lastQuickCheck >= 300000) {
        quickCheck();
        lastQuickCheck = currentTime;
        return true;
    }
    
    return false;
}

// ===== SYSTEM-HEALTH-CHECK IMPLEMENTIERUNG =====

SystemHealthCheck::SystemHealthCheck() :
    _lastCheckTime(0),
    _checkInterval(3600000), // 1 Stunde
    _uptimeHours(0),
    _bootUptimeHours(0),
    _restartCount(0),
    _memMonitor(nullptr),
    _netDiagnostics(nullptr),
    _initialized(false)
{
}

bool SystemHealthCheck::begin(MemoryMonitor* memMonitor, NetworkDiagnostics* netDiagnostics) {
    _lastCheckTime = 0;
    _bootUptimeHours = 0;
    _memMonitor = memMonitor;
    _netDiagnostics = netDiagnostics;

    // Lese gespeicherten Neustart-Zähler und Uptime (Uptime nur als Statistik, steuert keinen Neustart mehr — A6)
    _prefs.begin("syshealth", false);
    _restartCount = _prefs.getULong("restarts", 0);
    _uptimeHours = _prefs.getULong("uptime", 0);
    _prefs.putULong("restarts", _restartCount + 1);
    _prefs.end();

    _initialized = true;
    debug(String(F("System-Gesundheitscheck initialisiert, Neustarts: ")) + (_restartCount + 1));
    return true;
}

void SystemHealthCheck::performFullCheck() {
    if (!_initialized) {
        debug(F("SystemHealthCheck nicht initialisiert"));
        return;
    }
    
    debug(F("====== VOLLSTÄNDIGE SYSTEMPRÜFUNG ======"));
    
    // Speicher-Status prüfen
    if (_memMonitor != nullptr) {
        _memMonitor->diagnose();
    } else {
        size_t freeHeap = ESP.getFreeHeap();
        size_t maxBlock = ESP.getMaxAllocHeap();
        float fragmentationIndex = 1.0 - ((float)maxBlock / freeHeap);
        
        debug(String(F("Speicher: Frei=")) + freeHeap + 
              F(" Bytes, Max Block=") + maxBlock + 
              F(" Bytes, Fragmentierung=") + (fragmentationIndex * 100) + F("%"));
    }
    
    // Netzwerk prüfen
    if (_netDiagnostics != nullptr) {
        _netDiagnostics->fullAnalysis();
    } else if (WiFi.status() == WL_CONNECTED) {
        debug(String(F("WiFi: SSID=")) + WiFi.SSID() + 
              F(", IP=") + WiFi.localIP().toString() + 
              F(", RSSI=") + WiFi.RSSI() + F(" dBm"));
    }
    
    // Dateisystem prüfen
    uint64_t total = LittleFS.totalBytes();
    uint64_t used = LittleFS.usedBytes();
    float percentUsed = ((float)used / total) * 100;
    
    debug(String(F("Dateisystem: Genutzt=")) + used + 
          F(" Bytes (") + percentUsed + F("%) von ") + total + F(" Bytes"));
    
    if (percentUsed > 85) {
        debug(F("WARNUNG: Dateisystem ist fast voll. Bereinigung erforderlich."));
    }
    
    // CPU-Last und Temperatur
    float temperature = temperatureRead(); // ESP32-Funktion für die interne Temperatur
    debug(String(F("CPU-Temperatur: ")) + temperature + F("°C"));
    
    if (temperature > 70) {
        debug(F("WARNUNG: Hohe CPU-Temperatur erkannt."));
    }
    
    // Runtime-Statistiken
    unsigned long uptimeSeconds = millis() / 1000;
    debug(String(F("Systemlaufzeit: ")) + MoodlightUtils::formatTime(uptimeSeconds * 1000));
    debug(String(F("Systemneustarts: ")) + (_restartCount + 1));
    
    debug(F("========================================"));
    
    _lastCheckTime = millis();
}

void SystemHealthCheck::update() {
    if (!_initialized) return;

    unsigned long currentTime = millis();

    // Prüfe, ob es Zeit für eine vollständige Überprüfung ist
    if (currentTime - _lastCheckTime >= _checkInterval) {
        performFullCheck();
    }

    // Laufzeit des AKTUELLEN Boots aus millis() ableiten (steuert isRestartRecommended(), A6)
    unsigned long currentBootHours = currentTime / 3600000;

    if (currentBootHours > _bootUptimeHours) {
        _bootUptimeHours = currentBootHours;
        debug(String(F("Boot-Uptime: ")) + _bootUptimeHours + F(" Stunden"));

        // Bei 24h-Intervallen zusätzliche Prüfungen
        if (_bootUptimeHours % 24 == 0) {
            debug(String(F("24-Stunden-Meilenstein: ")) + _bootUptimeHours + F(" Stunden Laufzeit (dieser Boot)"));
            performFullCheck();
        }
    }

    // _uptimeHours bleibt reine NVS-Statistik (höchste je erreichte Boot-Laufzeit), nicht kumulativ über Boots hinweg
    if (currentBootHours > _uptimeHours) {
        _uptimeHours = currentBootHours;

        // Speichere aktuelle Uptime
        _prefs.begin("syshealth", false);
        _prefs.putULong("uptime", _uptimeHours);
        _prefs.end();
    }
}

String SystemHealthCheck::getMetricsJson() {
    String result = "{";
    
    // Systemlaufzeit
    unsigned long uptimeSeconds = millis() / 1000;
    result += "\"uptime\":" + String(uptimeSeconds) + ",";
    
    // Speichernutzung
    result += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    result += "\"maxBlock\":" + String(ESP.getMaxAllocHeap()) + ",";
    
    if (_memMonitor != nullptr) {
        result += "\"lowestHeap\":" + String(_memMonitor->getLowestHeap()) + ",";
    }
    
    // Netzwerk
    result += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    
    if (WiFi.status() == WL_CONNECTED) {
        result += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        result += "\"channel\":" + String(WiFi.channel()) + ",";
    }
    
    // Dateisystem
    uint64_t total = LittleFS.totalBytes();
    uint64_t used = LittleFS.usedBytes();
    float percentUsed = ((float)used / total) * 100;
    
    result += "\"fsTotal\":" + String((unsigned long)total) + ",";
    result += "\"fsUsed\":" + String((unsigned long)used) + ",";
    result += "\"fsPercent\":" + String(percentUsed) + ",";
    
    // Temperatur
    result += "\"temperature\":" + String(temperatureRead()) + ",";
    
    // Neustarts
    result += "\"restarts\":" + String(_restartCount + 1);
    
    result += "}";
    return result;
}

bool SystemHealthCheck::isRestartRecommended() {
    if (!_initialized) return false;

    // Alle Schwellwerte gegen die Laufzeit DES AKTUELLEN BOOTS prüfen (A6) —
    // _uptimeHours ist nur eine NVS-Statistik und darf hier nicht verwendet werden,
    // sonst empfiehlt nach 30 Tagen kumulierter Betriebszeit JEDER Boot einen Neustart.

    // Speicher-Fragmentierung prüfen
    size_t freeHeap = ESP.getFreeHeap();
    size_t maxBlock = ESP.getMaxAllocHeap();
    float fragmentationIndex = 1.0 - ((float)maxBlock / freeHeap);

    // Extrem hohe Fragmentierung
    if (fragmentationIndex > 0.85 && _bootUptimeHours > 48) {
        debug(F("Neustart empfohlen: Extreme Speicherfragmentierung"));
        return true;
    }

    // Sehr wenig freier Speicher
    if (freeHeap < 10000 && _bootUptimeHours > 24) {
        debug(F("Neustart empfohlen: Kritisch wenig freier Speicher"));
        return true;
    }

    // Dateisystem fast voll
    uint64_t total = LittleFS.totalBytes();
    uint64_t used = LittleFS.usedBytes();
    float percentUsed = (total > 0) ? (((float)used / total) * 100) : 0;

    if (percentUsed > 95 && _bootUptimeHours > 1) {
        debug(F("Neustart empfohlen: Dateisystem fast voll"));
        return true;
    }

    // Sehr lange Laufzeit dieses Boots
    if (_bootUptimeHours > 720) { // 30 Tage
        debug(F("Neustart empfohlen: Sehr lange Laufzeit (>30 Tage, dieser Boot)"));
        return true;
    }

    return false;
}

// ===== MOODLIGHTUTILS NAMESPACE IMPLEMENTIERUNG =====

namespace MoodlightUtils {

String formatTime(unsigned long ms) {
    unsigned long seconds = ms / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;
    
    seconds %= 60;
    minutes %= 60;
    hours %= 24;
    
    char timeStr[32];
    if (days > 0) {
        snprintf(timeStr, sizeof(timeStr), "%lud %02lu:%02lu:%02lu", days, hours, minutes, seconds);
    } else {
        snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", hours, minutes, seconds);
    }
    
    return String(timeStr);
}

} // namespace MoodlightUtils