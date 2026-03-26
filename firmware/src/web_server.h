#pragma once

#include "app_state.h"
#include <WebServer.h>

// Hardware-Instanz — definiert in web_server.cpp
extern WebServer server;

// === Web-Server-Funktionen ===

// Dateisystem
void initFS();
void getStorageInfo(uint64_t &totalBytes, uint64_t &usedBytes, uint64_t &freeBytes);

// Versions-Abfragen
String getCurrentUiVersion();
String getCurrentFirmwareVersion();

// Statische Dateien
void handleStaticFile(String path);

// API-Handler
void handleApiStatus();
void handleApiDeleteDataPoint();
void handleApiResetAllData();
void handleApiBackendStats();
void handleUiUpload();

// System-Logging
void logSystemStatus();

// JSON-Buffer-Pool initialisieren (muss vor setupWebServer() aufgerufen werden)
void initJsonPool();

// Web-Server initialisieren (registriert alle Routen)
void setupWebServer();

// JsonBufferPool bleibt intern in web_server.cpp
