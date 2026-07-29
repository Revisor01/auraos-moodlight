#pragma once

#include "app_state.h"
#include <Arduino.h>
#include <DNSServer.h>

// Hardware-Instanz — definiert in wifi_manager.cpp
extern DNSServer dnsServer;

// NTP-Zeit initialisieren (nicht-blockierend, startet SNTP-Sync)
void initTime();

// Prueft nicht-blockierend, ob die asynchrone NTP-Sync abgeschlossen ist
void checkNtpTimeSync();

// Sichere WiFi-Verbindung mit Timeout
bool safeWiFiConnect(const String &ssid, const String &password, unsigned long timeout = 15000);

// WiFi Station-Modus starten und verbinden
bool startWiFiStation();

// Verfuegbare WiFi-Netzwerke scannen (gibt JSON-String zurueck)
String scanWiFiNetworks();

// DNS-Anfragen fuer Captive Portal verarbeiten
void processDNS();

// Periodischen WiFi-Reconnect pruefen und durchfuehren
void checkAndReconnectWifi();

// AP-Modus mit Server-Start (fuer Setup-Phase in setup())
void startAPModeWithServer();

// WiFi verbinden + NTP + mDNS + Server starten (für setup())
// Gibt true zurück wenn WiFi verbunden, false wenn AP-Modus gestartet
bool connectWiFiAndStartServices();
