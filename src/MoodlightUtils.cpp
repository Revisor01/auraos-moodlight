// MoodlightUtils.cpp - Implementierung der Moodlight-Utilities

#include "MoodlightUtils.h"
#include <sstream>
#include <iomanip>
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
    // Initialisiere Watchdog mit der angegebenen Timeout-Zeit
    esp_err_t err = esp_task_wdt_init(timeoutSeconds, panicOnTimeout);
    if (err != ESP_OK) {
        debug(F("Fehler bei der Initialisierung des Watchdogs"));
        return false;
    }
    
    _isEnabled = true;
    _lastFeedTime = millis();
    debug(String(F("Watchdog initialisiert mit Timeout: ")) + timeoutSeconds + " Sekunden, Panic: " + (panicOnTimeout ? "Ja" : "Nein"));
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
    _lastReportTime(0), 
    _reportInterval(60000), 
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
    _isEnabled = true;
    
    // Lade gespeicherten niedrigsten Heap-Wert, falls vorhanden
    _prefs.begin("memMonitor", false);
    size_t savedLowest = _prefs.getULong("lowestHeap", 0);
    if (savedLowest > 0 && savedLowest < _lowestHeap) {
        _lowestHeap = savedLowest;
    }
    _prefs.end();
    
    debug(String(F("Speicher-Monitor initialisiert. Initiale Heap-Größe: ")) + formatBytes(_lastFreeHeap));
    return true;
}

void MemoryMonitor::update() {
    if (!_isEnabled) return;
    
    unsigned long currentTime = millis();
    size_t currentFree = ESP.getFreeHeap();
    
    // Aktualisiere niedrigsten Wert
    if (currentFree < _lowestHeap) {
        _lowestHeap = currentFree;
        
        // Speichere neuen Tiefstwert
        _prefs.begin("memMonitor", false);
        _prefs.putULong("lowestHeap", _lowestHeap);
        _prefs.end();
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

bool SafeFileOps::moveFile(const String& source, const String& destination) {
    if (!LittleFS.exists(source)) {
        debug("Quelldatei existiert nicht: " + source);
        return false;
    }
    
    if (LittleFS.exists(destination)) {
        if (!LittleFS.remove(destination)) {
            debug("Konnte Zieldatei nicht entfernen: " + destination);
            return false;
        }
    }
    
    if (LittleFS.rename(source, destination)) {
        debug("Datei verschoben von " + source + " nach " + destination);
        return true;
    }
    
    // Wenn Umbenennen fehlschlägt, versuche Kopieren und Löschen
    if (copyFile(source, destination)) {
        if (LittleFS.remove(source)) {
            debug("Datei kopiert und gelöscht von " + source + " nach " + destination);
            return true;
        } else {
            debug("Warnung: Konnte Quelldatei nicht löschen nach Kopieren: " + source);
            return true; // Trotzdem erfolgreich, aber Quelldatei bleibt
        }
    }
    
    debug("Fehler beim Verschieben der Datei: " + source + " -> " + destination);
    return false;
}

bool SafeFileOps::removeDir(const String& dirPath) {
    File root = LittleFS.open(dirPath);
    if (!root) {
        debug("Verzeichnis existiert nicht: " + dirPath);
        return false;
    }
    
    if (!root.isDirectory()) {
        debug("Pfad ist kein Verzeichnis: " + dirPath);
        root.close();
        return false;
    }
    
    File file = root.openNextFile();
    while (file) {
        String filePath = String(file.path());
        if (file.isDirectory()) {
            file.close();
            removeDir(filePath);
        } else {
            file.close();
            if (!LittleFS.remove(filePath)) {
                debug("Fehler beim Löschen der Datei: " + filePath);
            }
        }
        
        file = root.openNextFile();
    }
    
    root.close();
    if (LittleFS.rmdir(dirPath)) {
        debug("Verzeichnis gelöscht: " + dirPath);
        return true;
    } else {
        debug("Fehler beim Löschen des Verzeichnisses: " + dirPath);
        return false;
    }
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

// ===== CSV-BUFFER IMPLEMENTIERUNG =====

CSVBuffer::CSVBuffer(const String& filename) : 
    _count(0), 
    _lastFlush(0), 
    _flushInterval(300000), // 5 Minuten 
    _filename(filename) 
{
    _mutex = xSemaphoreCreateMutex();
    
    // Initialisiere alle Einträge als ungültig
    for (int i = 0; i < BUFFER_SIZE; i++) {
        _entries[i].valid = false;
    }
}

CSVBuffer::~CSVBuffer() {
    flush(); // Sichere alle Daten beim Beenden
    
    if (_mutex != NULL) {
        vSemaphoreDelete(_mutex);
        _mutex = NULL;
    }
}

bool CSVBuffer::begin() {
    // Erstelle Verzeichnis, falls erforderlich
    int lastSlash = _filename.lastIndexOf('/');
    if (lastSlash > 0) {
        String directory = _filename.substring(0, lastSlash);
        if (!LittleFS.exists(directory)) {
            if (!LittleFS.mkdir(directory)) {
                debug(String(F("Fehler beim Erstellen des Verzeichnisses: ")) + directory);
                return false;
            }
        }
    }
    
    // Prüfe, ob Datei mit Header erstellt werden muss
    if (!LittleFS.exists(_filename)) {
        File file = LittleFS.open(_filename, "w");
        if (!file) {
            debug(String(F("Fehler beim Erstellen der CSV-Datei: ")) + _filename);
            return false;
        }
        
        // Schreibe Header
        file.println("timestamp,sentiment");
        file.close();
        
        debug(String(F("CSV-Datei erstellt mit Header: ")) + _filename);
    }
    
    debug(String(F("CSV-Puffer initialisiert für: ")) + _filename);
    _lastFlush = millis();
    return true;
}

bool CSVBuffer::add(time_t timestamp, float sentiment) {
    // Strikte Validierung der Eingabewerte
    if (timestamp < 1600000000 || isnan(sentiment) || isinf(sentiment) || sentiment < -1.0f || sentiment > 1.0f) {
        debug(String(F("CSV-Puffer: Ungültige Eingabewerte - timestamp=")) + 
              timestamp + F(", sentiment=") + sentiment);
        return false;
    }

    if (_mutex == NULL) {
        debug(F("CSV-Puffer: Mutex nicht initialisiert"));
        return false;
    }
    
    if (xSemaphoreTake(_mutex, 100 / portTICK_PERIOD_MS) != pdTRUE) {
        debug(F("CSV-Puffer: Konnte Mutex nicht sperren"));
        return false;
    }
    
    // Suche freien Platz im Puffer
    int slot = -1;
    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (!_entries[i].valid) {
            slot = i;
            break;
        }
    }
    
    // Wenn kein freier Platz, spüle Puffer
    if (slot == -1) {
        xSemaphoreGive(_mutex);
        if (!flush()) {
            return false;
        }
        
        if (xSemaphoreTake(_mutex, 100 / portTICK_PERIOD_MS) != pdTRUE) {
            return false;
        }
        slot = 0; // Nach Spülung sollte Platz 0 frei sein
    }
    
    // Speichere Wert im Puffer
    _entries[slot].timestamp = timestamp;
    _entries[slot].sentiment = sentiment;
    _entries[slot].valid = true;
    _count++;
    
    xSemaphoreGive(_mutex);
    
    // Automatisch spülen, wenn der Puffer voll ist oder Interval abgelaufen
    unsigned long currentTime = millis();
    if (_count >= BUFFER_SIZE || (currentTime - _lastFlush > _flushInterval && _count > 0)) {
        return flush();
    }
    
    return true;
}

bool CSVBuffer::flush() {
    if (_count == 0) return true; // Nichts zu tun
    
    if (_mutex == NULL) {
        debug(F("CSV-Puffer: Mutex nicht initialisiert"));
        return false;
    }
    
    if (xSemaphoreTake(_mutex, 200 / portTICK_PERIOD_MS) != pdTRUE) {
        debug(F("CSV-Puffer: Konnte Mutex zum Spülen nicht sperren"));
        return false;
    }
    
    debug(String(F("CSV-Puffer: Spüle ")) + _count + F(" Einträge in ") + _filename);
    
    // Öffne die Datei im Append-Modus mit Sicherheitsmaßnahmen
    bool isNewFile = !LittleFS.exists(_filename);
    
    // Wenn die Datei nicht existiert, erstellen wir sie mit Header
    if (isNewFile) {
        File newFile = LittleFS.open(_filename, "w");
        if (!newFile) {
            debug(String(F("CSV-Puffer: Fehler beim Erstellen von ")) + _filename);
            xSemaphoreGive(_mutex);
            return false;
        }
        newFile.println("timestamp,sentiment");
        newFile.close();
    }
    
    // Öffne die Datei zum Anhängen
    File file = LittleFS.open(_filename, "a");
    if (!file) {
        debug(String(F("CSV-Puffer: Fehler beim Öffnen von ")) + _filename);
        xSemaphoreGive(_mutex);
        return false;
    }
    
    // Zähle erfolgreiche Schreiboperationen
    int successCount = 0;
    int failCount = 0;
    
    // Schreibe alle gültigen Einträge mit strikter Validierung
    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (_entries[i].valid) {
            // Überprüfe Werte erneut
            if (_entries[i].timestamp < 1600000000 || 
                isnan(_entries[i].sentiment) || 
                isinf(_entries[i].sentiment) || 
                _entries[i].sentiment < -1.0f || 
                _entries[i].sentiment > 1.0f) {
                
                debug(F("CSV-Puffer: Ungültiger Eintrag - nicht in Datei geschrieben"));
                _entries[i].valid = false;  // Markiere als ungültig
                failCount++;
                continue;
            }
            
            // Bereite Zeile vor und validiere
            char line[64];
            int result = snprintf(line, sizeof(line), "%ld,%.2f\n", 
                               (long)_entries[i].timestamp, _entries[i].sentiment);
            
            if (result < 5 || result >= (int)sizeof(line)) {
                debug(F("CSV-Puffer: Formatierungsfehler bei CSV-Zeile"));
                failCount++;
                continue;
            }
            
            // Schreibe Zeile
            if (file.print(line) != strlen(line)) {
                debug(F("CSV-Puffer: Fehler beim Schreiben in Datei"));
                failCount++;
                continue;
            }
            
            // Markiere als ungültig nach erfolgreichem Schreiben
            _entries[i].valid = false;
            successCount++;
        }
    }
    
    file.close();
    
    // Aktualisiere Pufferstatus
    _count = 0;
    _lastFlush = millis();
    
    debug(String(F("CSV-Puffer: ")) + successCount + F(" Einträge erfolgreich geschrieben, ") + 
          failCount + F(" Einträge fehlgeschlagen"));
    
    xSemaphoreGive(_mutex);
    return successCount > 0 || failCount == 0;  // Erfolg, wenn entweder etwas geschrieben wurde oder keine Fehler auftraten
}

void CSVBuffer::setFlushInterval(unsigned long intervalMs) {
    _flushInterval = intervalMs;
}

bool CSVBuffer::isFull() const {
    return _count >= BUFFER_SIZE;
}

int CSVBuffer::count() const {
    return _count;
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
    
    // WLAN-Kanal und umgebende Netzwerke
    int channel = WiFi.channel();
    int networksFound = WiFi.scanNetworks(false, true, false, 300);
    
    if (networksFound > 0) {
        debug(String(F("Gefundene WiFi-Netzwerke: ")) + networksFound);
        
        int networksOnSameChannel = 0;
        int networksOnAdjacentChannels = 0;
        int strongNetworksOnSameChannel = 0;
        
        for (int i = 0; i < networksFound; i++) {
            int networkChannel = WiFi.channel(i);
            int networkRSSI = WiFi.RSSI(i);
            
            if (networkChannel == channel) {
                networksOnSameChannel++;
                if (networkRSSI > -70) {
                    strongNetworksOnSameChannel++;
                }
            } else if (abs(networkChannel - channel) <= 1) {
                networksOnAdjacentChannels++;
            }
            
            // Zeige nur die 5 stärksten Netzwerke
            if (i < 5) {
                debug(String(F("  Netzwerk: ")) + WiFi.SSID(i) + 
                      F(", Kanal: ") + networkChannel + 
                      F(", RSSI: ") + networkRSSI + F(" dBm"));
            }
        }
        
        // FEHLERKORREKTUR: Gesamte Verkettung innerhalb eines debug()-Aufrufs
        debug(String(F("Kanal ")) + channel + 
              F(": ") + networksOnSameChannel + F(" Netzwerke auf gleichem Kanal, ") +
              strongNetworksOnSameChannel + F(" stark, ") +
              networksOnAdjacentChannels + F(" auf benachbarten Kanälen"));
        
        // Interferenz-Bewertung
        if (strongNetworksOnSameChannel > 2) {
            debug(F("WARNUNG: Starke WiFi-Interferenz auf dem aktuellen Kanal"));
        }
    } else {
        debug(F("Keine anderen WiFi-Netzwerke gefunden"));
    }
    
    // Router-Verbindungsinformationen
    IPAddress gatewayIP = WiFi.gatewayIP();
    debug(String(F("Gateway-IP: ")) + gatewayIP.toString());
    
    // DNS-Informationen
    IPAddress dns1 = WiFi.dnsIP(0);
    IPAddress dns2 = WiFi.dnsIP(1);
    debug(String(F("DNS-Server: ")) + dns1.toString() + F(", ") + dns2.toString());
    
    _lastFullAnalysis = millis();
}

// Implementierung der TaskManager-Klasse
TaskManager::TaskManager(const char* taskName, uint32_t stackSize, 
    UBaseType_t priority, BaseType_t coreId) :
_taskHandle(nullptr),
_taskName(taskName),
_stackSize(stackSize),
_priority(priority),
_coreId(coreId),
_isRunning(false)
{
_taskMutex = xSemaphoreCreateMutex();
}

TaskManager::~TaskManager() {
end();

if (_taskMutex != nullptr) {
vSemaphoreDelete(_taskMutex);
_taskMutex = nullptr;
}
}

bool TaskManager::begin(TaskFunction_t taskFunction, void* taskParameters) {
if (_isRunning) {
debug(F("Task bereits gestartet"));
return false;
}

// Task auf spezifischem Core erstellen
BaseType_t result = xTaskCreatePinnedToCore(
taskFunction,
_taskName,
_stackSize,
taskParameters,
_priority,
&_taskHandle,
_coreId
);

if (result == pdPASS) {
_isRunning = true;
debug(String(F("Task '")) + _taskName + F("' erfolgreich erstellt auf Core ") + _coreId);
return true;
} else {
debug(String(F("Fehler beim Erstellen des Tasks '")) + _taskName + F("'"));
return false;
}
}

bool TaskManager::execute() {
if (!_isRunning || _taskHandle == nullptr) {
debug(String(F("Task '")) + _taskName + F("' nicht aktiv"));
return false;
}

// Task benachrichtigen
return xTaskNotifyGive(_taskHandle) == pdPASS;
}

void TaskManager::end() {
if (_isRunning && _taskHandle != nullptr) {
vTaskDelete(_taskHandle);
_taskHandle = nullptr;
_isRunning = false;
debug(String(F("Task '")) + _taskName + F("' beendet"));
}
}

bool TaskManager::isRunning() const {
return _isRunning;
}

UBaseType_t TaskManager::getStackHighWaterMark() const {
if (_taskHandle != nullptr) {
return uxTaskGetStackHighWaterMark(_taskHandle);
}
return 0;
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
    _restartCount(0), 
    _memMonitor(nullptr), 
    _netDiagnostics(nullptr), 
    _initialized(false) 
{
}

bool SystemHealthCheck::begin(MemoryMonitor* memMonitor, NetworkDiagnostics* netDiagnostics) {
    _lastCheckTime = 0;
    _memMonitor = memMonitor;
    _netDiagnostics = netDiagnostics;
    
    // Lese gespeicherten Neustart-Zähler
    _prefs.begin("syshealth", false);
    _restartCount = _prefs.getULong("restarts", 0);
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
    
    // Berechne stündliche Uptime
    unsigned long currentHours = currentTime / 3600000;
    
    if (currentHours > _uptimeHours) {
        _uptimeHours = currentHours;
        
        // Speichere aktuelle Uptime
        _prefs.begin("syshealth", false);
        _prefs.putULong("uptime", _uptimeHours);
        _prefs.end();
        
        debug(String(F("System-Uptime: ")) + _uptimeHours + F(" Stunden"));
        
        // Bei 24h-Intervallen zusätzliche Prüfungen
        if (_uptimeHours % 24 == 0) {
            debug(String(F("24-Stunden-Meilenstein: ")) + _uptimeHours + F(" Stunden Laufzeit"));
            performFullCheck();
        }
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
    
    // Speicher-Fragmentierung prüfen
    size_t freeHeap = ESP.getFreeHeap();
    size_t maxBlock = ESP.getMaxAllocHeap();
    float fragmentationIndex = 1.0 - ((float)maxBlock / freeHeap);
    
    // Extrem hohe Fragmentierung
    if (fragmentationIndex > 0.85 && _uptimeHours > 48) {
        debug(F("Neustart empfohlen: Extreme Speicherfragmentierung"));
        return true;
    }
    
    // Sehr wenig freier Speicher
    if (freeHeap < 10000 && _uptimeHours > 24) {
        debug(F("Neustart empfohlen: Kritisch wenig freier Speicher"));
        return true;
    }
    
    // Dateisystem fast voll
    uint64_t total = LittleFS.totalBytes();
    uint64_t used = LittleFS.usedBytes();
    float percentUsed = ((float)used / total) * 100;
    
    if (percentUsed > 95) {
        debug(F("Neustart empfohlen: Dateisystem fast voll"));
        return true;
    }
    
    // Sehr lange Laufzeit
    if (_uptimeHours > 720) { // 30 Tage
        debug(F("Neustart empfohlen: Sehr lange Laufzeit (>30 Tage)"));
        return true;
    }
    
    return false;
}

// ===== MOODLIGHTUTILS NAMESPACE IMPLEMENTIERUNG =====

namespace MoodlightUtils {

String formatString(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return String(buffer);
}

void safeDelay(unsigned long ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
        // Feed watchdog regularly during longer delays
        if (ms > 1000 && millis() - start > 1000) {
            esp_task_wdt_reset();
            yield(); // Give other tasks time to run
        }
        delay(10);
    }
}

String getTimestamp() {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    std::stringstream ss;
    ss << std::setfill('0') 
       << std::setw(4) << (timeinfo.tm_year + 1900) << "-"
       << std::setw(2) << (timeinfo.tm_mon + 1) << "-"
       << std::setw(2) << timeinfo.tm_mday << " "
       << std::setw(2) << timeinfo.tm_hour << ":"
       << std::setw(2) << timeinfo.tm_min << ":"
       << std::setw(2) << timeinfo.tm_sec;
    
    return String(ss.str().c_str());
}

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

String randomString(size_t length) {
    const char charset[] = "0123456789"
                          "abcdefghijklmnopqrstuvwxyz"
                          "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    String result = "";
    
    if (length > 0) {
        randomSeed(micros());
        for (size_t i = 0; i < length; i++) {
            int index = random(sizeof(charset) - 1);
            result += charset[index];
        }
    }
    
    return result;
}

} // namespace MoodlightUtils