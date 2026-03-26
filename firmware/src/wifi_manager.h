#pragma once

#include "app_state.h"
#include <Arduino.h>
#include <DNSServer.h>

// Hardware-Instanz — definiert in wifi_manager.cpp
extern DNSServer dnsServer;

// NTP-Zeit initialisieren
void initTime();

// Sichere WiFi-Verbindung mit Timeout
bool safeWiFiConnect(const String &ssid, const String &password, unsigned long timeout = 15000);

// AP-Modus starten (ohne Server-Start, fuer loop()-Kontext)
void startAPMode();

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
