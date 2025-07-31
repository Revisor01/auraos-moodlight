// MoodlightUtils.h - Utility-Bibliothek für Moodlight Projekt
// Version 1.0.0

#ifndef MOODLIGHT_UTILS_H
#define MOODLIGHT_UTILS_H

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>

// Forward-Deklaration für die Debug-Funktion, die vom Hauptprojekt bereitgestellt wird
extern void debug(const String &message);
extern void debug(const __FlashStringHelper *message);

// ===== WATCHDOG-MANAGEMENT =====
class WatchdogManager {
private:
    TaskHandle_t _monitoredTask;
    unsigned long _lastFeedTime;
    unsigned long _feedInterval;
    bool _isEnabled;
    
public:
    WatchdogManager();
    
    // Initialisiere den Watchdog mit einer bestimmten Timeout-Zeit
    bool begin(uint32_t timeoutSeconds = 30, bool panicOnTimeout = false);
    
    // Registriere den aktuellen Task (normalerweise Loop-Task)
    bool registerCurrentTask();
    
    // Füttere den Watchdog (sollte regelmäßig aufgerufen werden)
    void feed();
    
    // Automatisches Füttern, wenn die angegebene Zeit abgelaufen ist
    bool autoFeed(unsigned long interval = 15000);
    
    // Führe Stack-Analyse für den überwachten Task durch
    void analyzeStack();
    
    // Watchdog deaktivieren
    void disable();
};

// In MoodlightUtils.h - diese Deklaration ergänzen
class TaskManager {
    private:
        TaskHandle_t _taskHandle;
        const char* _taskName;
        uint32_t _stackSize;
        UBaseType_t _priority;
        BaseType_t _coreId;
        bool _isRunning;
        SemaphoreHandle_t _taskMutex;
        
    public:
        TaskManager(const char* taskName, uint32_t stackSize = 4096, 
                    UBaseType_t priority = 1, BaseType_t coreId = 0);
        ~TaskManager();
        
        // Task erstellen und starten
        bool begin(TaskFunction_t taskFunction, void* taskParameters = nullptr);
        
        // Task ausführen lassen (Signal senden)
        bool execute();
        
        // Task beenden
        void end();
        
        // Status-Informationen
        bool isRunning() const;
        UBaseType_t getStackHighWaterMark() const;
    };
    
// ===== SPEICHER-ÜBERWACHUNG =====
class MemoryMonitor {
private:
    size_t _lastFreeHeap;
    size_t _lowestHeap;
    unsigned long _lastReportTime;
    unsigned long _reportInterval;
    unsigned long _startTime;
    bool _isEnabled;
    Preferences _prefs;
    
public:
    MemoryMonitor();
    
    // Initialisiere den Monitor
    bool begin(unsigned long reportInterval = 60000);
    
    // Aktualisiere und überprüfe Speicherstatus
    void update();
    
    // Überprüfe, ob genügend freier Speicher für eine Operation vorhanden ist
    bool checkHeapBefore(const char* operation, size_t requiredFree = 10000);
    
    // Führe vollständige Speicherdiagnose durch
    void diagnose();
    
    // Erhalte den niedrigsten gesehenen Heap-Wert
    size_t getLowestHeap() const;
    
    // Formatiere Bytes in lesbare Größe (KB, MB)
    static String formatBytes(size_t bytes);
};

// ===== SICHERE DATEIOPERATIONEN =====
class SafeFileOps {
private:
    bool _backupEnabled;
    String _tempSuffix;
    String _backupSuffix;
    
public:
    SafeFileOps(bool enableBackup = true);
    
    // Sichere Leseoperation mit Wiederholversuchen
    String readFile(const char* path, int maxRetries = 3);
    
    // Sichere Schreiboperation mit temporärer Datei und Backups
    bool writeFile(const char* path, const String& content);
    
    // Sicheres Kopieren von Dateien
    bool copyFile(const String& source, const String& destination);
    
    // Sicheres Verschieben/Umbenennen von Dateien
    bool moveFile(const String& source, const String& destination);
    
    // Rekursives Löschen von Verzeichnissen
    bool removeDir(const String& dirPath);
    
    // Prüfe, ob genügend freier Speicherplatz für eine Operation vorhanden ist
    bool checkSpaceBefore(size_t requiredBytes);
    
    // Verzeichnisliste rekursiv abrufen (für Debugging)
    void listDir(const char* dirname, uint8_t levels = 0);
};

// ===== CSV-PUFFER-SYSTEM =====
class CSVBuffer {
private:
    static const int BUFFER_SIZE = 10; // Anzahl der Einträge im Puffer
    struct CSVEntry {
        time_t timestamp;
        float sentiment;
        bool valid;
    };
    
    CSVEntry _entries[BUFFER_SIZE];
    int _count;
    unsigned long _lastFlush;
    unsigned long _flushInterval;
    String _filename;
    SemaphoreHandle_t _mutex;
    
public:
    CSVBuffer(const String& filename);
    ~CSVBuffer();
    
    // Initialisiere den Puffer
    bool begin();
    
    // Füge einen neuen Datenpunkt hinzu
    bool add(time_t timestamp, float sentiment);
    
    // Spüle den Puffer zur Datei
    bool flush();
    
    // Setze das Flush-Intervall
    void setFlushInterval(unsigned long intervalMs);
    
    // Überprüfe, ob Puffer voll ist
    bool isFull() const;
    
    // Anzahl der Einträge im Puffer
    int count() const;
};

// ===== NETZWERK-DIAGNOSTIK =====
class NetworkDiagnostics {
private:
    unsigned long _lastFullAnalysis;
    unsigned long _analysisInterval;
    int _lastRssi;
    
public:
    NetworkDiagnostics();
    
    // Initialisiere die Diagnostik
    bool begin(unsigned long intervalMs = 3600000);
    
    // Analysiere das WiFi-Signal
    void analyzeWiFiSignal();
    
    // Führe eine kurze Überprüfung durch
    void quickCheck();
    
    // Führe eine vollständige Netzwerkanalyse durch
    void fullAnalysis();
    
    // Überprüfe und aktualisiere Intervall-basiert
    bool updateCheck();
};

// ===== SYSTEM-GESUNDHEITSCHECK =====
class SystemHealthCheck {
private:
    unsigned long _lastCheckTime;
    unsigned long _checkInterval;
    unsigned long _uptimeHours;
    unsigned long _restartCount;
    Preferences _prefs;
    MemoryMonitor* _memMonitor;
    NetworkDiagnostics* _netDiagnostics;
    bool _initialized;
    
public:
    SystemHealthCheck();
    
    // Initialisiere den Gesundheitscheck
    bool begin(MemoryMonitor* memMonitor = nullptr, NetworkDiagnostics* netDiagnostics = nullptr);
    
    // Führe eine vollständige Systemprüfung durch
    void performFullCheck();
    
    // Aktualisiere Systemmetriken und führe regelmäßige Prüfungen durch
    void update();
    
    // Exportiere Systemmetriken als JSON
    String getMetricsJson();
    
    // Prüfe, ob ein Neustart notwendig ist
    bool isRestartRecommended();
};

// Namespace für globale Funktionen
namespace MoodlightUtils {
    // Formatiert einen String mit variadischen Argumenten
    String formatString(const char* format, ...);
    
    // Sichere Delay-Funktion, die den Watchdog füttert
    void safeDelay(unsigned long ms);
    
    // Gibt den aktuellen Zeitstempel als String zurück
    String getTimestamp();
    
    // Formatiert Millisekunden in lesbare Zeit (dd:hh:mm:ss)
    String formatTime(unsigned long ms);
    
    // Gibt einen zufälligen String (für temporäre Dateinamen) zurück
    String randomString(size_t length);
}

#endif // MOODLIGHT_UTILS_H