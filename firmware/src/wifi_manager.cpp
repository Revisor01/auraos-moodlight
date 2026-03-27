#include "wifi_manager.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include "esp_wifi.h"
#include <ESPmDNS.h>
#include <time.h>
#include "MoodlightUtils.h"

// Captive Portal IP (192.168.4.1 — Standard-IP fuer den Access Point)
#ifndef CAPTIVE_PORTAL_IP
#define CAPTIVE_PORTAL_IP \
    {192, 168, 4, 1}
#endif

// Externe Globals aus moodlight.cpp
extern AppState appState;
extern Adafruit_NeoPixel pixels;
extern WatchdogManager watchdog;

#include "debug.h"
#include "web_server.h"

// Hardware-Instanz — definiert in diesem Modul
DNSServer dnsServer;

extern void setStatusLED(int mode);

// Captive Portal Redirect Handler — leitet alle unbekannten Anfragen zur Setup-Seite
class CaptiveRequestHandler : public RequestHandler
{
public:
    CaptiveRequestHandler() {}
    virtual ~CaptiveRequestHandler() {}

    bool canHandle(HTTPMethod method, String uri)
    {
        return true; // Faengt alles ab
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

// === NTP-Zeit initialisieren ===
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
        appState.timeInitialized = true;
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        char dateStr[64];
        strftime(dateStr, sizeof(dateStr), "%d.%m.%Y %H:%M:%S", &timeinfo);
        debug(String(F("NTP: Zeit synchronisiert - ")) + dateStr);
    } else {
        debug(F("NTP: Zeitsynchronisation fehlgeschlagen"));
    }
}

// === Sichere WiFi-Verbindung mit Timeout ===
bool safeWiFiConnect(const String &ssid, const String &password, unsigned long timeout)
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
        dotCounter++;
        if (dotCounter % 10 == 0)
        {
            debug(".");
            watchdog.feed(); // Watchdog füttern alle 500ms während Verbindungsaufbau
            yield();
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

    // LED Animation fuer AP-Modus
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

    // DNS-Server fuer Captive Portal starten
    dnsServer.start(DNS_PORT, "*", IP);
    appState.isInConfigMode = true;

    debug("AP gestartet mit IP " + WiFi.softAPIP().toString());
    debug(String(F("SSID: ")) + DEFAULT_AP_NAME);

    // Captive Portal Request Handler hinzufuegen
    server.addHandler(new CaptiveRequestHandler());

    // Status LED fuer AP-Modus
    setStatusLED(5);

    // Merke Zeit fuer Timeout
    appState.apModeStartTime = millis();
}

// === WiFi Station-Modus starten ===
bool startWiFiStation()
{
    if (!appState.wifiConfigured || appState.wifiSSID.isEmpty())
    {
        debug(F("Keine WiFi-Konfiguration vorhanden."));
        return false;
    }

    debug(String(F("Verbinde mit WiFi: ")) + appState.wifiSSID);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    // Use the safer WiFi connect method
    bool connected = safeWiFiConnect(appState.wifiSSID, appState.wifiPassword, 15000);

    if (connected)
    {
        appState.wifiWasConnected = true;
        appState.wifiConnectedSince = millis(); // Stabilitäts-Timer starten

        // Synchronize time via NTP after successful WiFi connection
        initTime();
    }
    else
    {
        debug(F("WiFi Verbindungs-Timeout! Fahre trotzdem fort."));
    }

    return connected;
}

// === WiFi-Netzwerke scannen fuer Setup-Seite ===
String scanWiFiNetworks()
{
    debug(F("Scanne WiFi-Netzwerke..."));

    int n = WiFi.scanNetworks();
    debug(String(F("Scan abgeschlossen: ")) + n + F(" Netzwerke gefunden"));

    JsonDocument doc; // Groesser fuer viele WiFi-Netzwerke
    JsonArray networks = doc["networks"].to<JsonArray>();

    for (int i = 0; i < n; i++)
    {
        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        network["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

        // Kurze Pause zwischen Eintraegen
        delay(10);
    }

    String jsonResult;
    serializeJson(doc, jsonResult);
    return jsonResult;
}

// === DNS-Anfragen fuer Captive Portal verarbeiten ===
void processDNS()
{
    if (appState.isInConfigMode)
    {
        dnsServer.processNextRequest();
    }
}

// === Periodischen WiFi-Reconnect pruefen und durchfuehren ===
void checkAndReconnectWifi()
{
    // Don't try to reconnect if we're in AP mode
    if (appState.isInConfigMode)
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
        if (appState.wifiWasConnected)
        {
            debug(F("WiFi Verbindung verloren! Starte Reconnect-Prozess..."));
            appState.wifiWasConnected = false;
            appState.wifiReconnectActive = true;  // LED-02: Unterdrueckt pixels.show() waehrend Reconnect
            appState.wifiReconnectAttempts = 0;
            appState.wifiReconnectDelay = 5000;
            appState.disconnectStartMs = millis();  // LED-03: Grace-Timer starten, KEIN setStatusLED(1) hier
        }

        // LED-03: Status-LED erst nach Grace Period aktivieren
        if (appState.disconnectStartMs > 0 && (currentMillis - appState.disconnectStartMs > STATUS_LED_GRACE_MS)) {
            setStatusLED(1);
            appState.disconnectStartMs = 0;  // Einmalig setzen
        }

        // Check if it's time for a new reconnect attempt
        if (currentMillis - appState.lastWifiCheck >= appState.wifiReconnectDelay)
        {
            appState.lastWifiCheck = currentMillis;
            appState.wifiReconnectAttempts++;

            // Only try to connect if we have SSID
            if (appState.wifiConfigured && !appState.wifiSSID.isEmpty())
            {
                debug(String(F("WiFi Reconnect Versuch #")) + appState.wifiReconnectAttempts + String(F(" mit Delay ")) + String(appState.wifiReconnectDelay / 1000) + "s");

                // Try reconnect
                WiFi.disconnect(true);
                delay(100);
                WiFi.begin(appState.wifiSSID.c_str(), appState.wifiPassword.c_str());

                // Short non-blocking wait time — feed watchdog to prevent WDT reset during reconnect
                unsigned long reconnectStart = millis();
                int attemptCount = 0;
                while (WiFi.status() != WL_CONNECTED && millis() - reconnectStart < 3000)
                {
                    delay(50);
                    attemptCount++;
                    if (attemptCount % 10 == 0)
                    {
                        watchdog.feed(); // Watchdog füttern alle 500ms während Reconnect
                        yield();
                    }
                }

                if (WiFi.status() == WL_CONNECTED)
                {
                    debug(F("WiFi erfolgreich wieder verbunden!"));
                    appState.wifiWasConnected = true;
                    appState.wifiReconnectActive = false;  // LED-02: Reconnect beendet, LEDs wieder freigeben
                    appState.disconnectStartMs = 0;         // LED-03: Grace-Timer zuruecksetzen
                    appState.wifiReconnectAttempts = 0;
                    appState.wifiReconnectDelay = 5000;
                    appState.wifiConnectedSince = millis(); // Stabilitäts-Timer starten

                    // Explicitly disable power save mode after connection
                    esp_wifi_set_ps(WIFI_PS_NONE);

                    // Initialize time after successful reconnection if not already done
                    if (!appState.timeInitialized) {
                        initTime();
                    }

                    setStatusLED(0);
                }
                else
                {
                    // Exponential backoff - with max limit
                    appState.wifiReconnectDelay = min(appState.wifiReconnectDelay * 2, (unsigned long)MAX_RECONNECT_DELAY);
                    debug(String(F("WiFi Reconnect fehlgeschlagen. Naechster Versuch in ")) + String(appState.wifiReconnectDelay / 1000) + "s");
                }
            }
        }
    }
    else if (!appState.wifiWasConnected)
    {
        // WiFi is now connected but was disconnected before
        debug(F("WiFi ist wieder verbunden."));
        appState.wifiWasConnected = true;
        appState.wifiReconnectActive = false;  // LED-02: Reconnect beendet, LEDs wieder freigeben
        appState.disconnectStartMs = 0;         // LED-03: Grace-Timer zuruecksetzen
        appState.wifiReconnectAttempts = 0;
        appState.wifiReconnectDelay = 5000;
        appState.wifiConnectedSince = millis(); // Stabilitäts-Timer starten

        // Explicitly disable power save mode after connection
        esp_wifi_set_ps(WIFI_PS_NONE);

        // Initialize time after successful reconnection if not already done
        if (!appState.timeInitialized) {
            initTime();
        }

        setStatusLED(0);
    }
    yield();
    delay(1);
}

// === AP-Modus mit Server-Start (fuer Setup-Phase) ===
void startAPModeWithServer()
{
    debug(F("Starte Access Point Modus..."));

    // Status-LED via Serial signalisieren, da pixels.show() vermieden wird
    debug(F("AP Mode activated (visual LED indicator skipped during init)."));
    // pixels.fill(pixels.Color(255, 255, 0));  // Gelb
    // pixels.show(); // <-- Vermeiden waehrend Setup-Phase

    // AP-Modus konfigurieren
    WiFi.mode(WIFI_AP);
    delay(300); // Laengere Verzoegerung fuer Modus-Wechsel

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
    appState.isInConfigMode = true; // Dies markiert, dass wir im AP-Modus sind

    debug("AP gestartet mit IP " + WiFi.softAPIP().toString());
    debug(String(F("SSID: ")) + DEFAULT_AP_NAME);

    // Captive Portal Request Handler hinzufuegen
    server.addHandler(new CaptiveRequestHandler());

    // Status LED fuer AP-Modus (wird in loop aktualisiert)
    setStatusLED(5);

    // Merke Zeit fuer Timeout
    appState.apModeStartTime = millis();
}

// === WiFi verbinden + NTP + mDNS + Server starten ===
bool connectWiFiAndStartServices() {
    if (!appState.wifiConfigured || appState.wifiSSID.isEmpty()) {
        debug(F("Keine WiFi-Konfiguration gefunden. Starte Access Point..."));
        startAPModeWithServer();
        return false;
    }

    debug(String(F("Vorhandene WiFi-Konfiguration gefunden: ")) + appState.wifiSSID);

    bool wifiConnected = startWiFiStation();

    if (!wifiConnected) {
        debug(F("WiFi-Verbindung fehlgeschlagen. Starte Access Point..."));
        startAPModeWithServer();
        return false;
    }

    debug(F("WiFi verbunden. Deaktiviere explizit Power Save..."));
    esp_wifi_set_ps(WIFI_PS_NONE);
    debug(F("ESP-IDF WiFi Power Save explicitly disabled (STA)."));

    initTime();

    debug(F("Starte Webserver..."));
    server.begin();
    debug(F("Webserver gestartet"));

    if (MDNS.begin("moodlight")) {
        MDNS.addService("http", "tcp", 80);
        debug(F("mDNS responder gestartet"));
    } else {
        debug(F("mDNS start fehlgeschlagen"));
    }

    return true;
}
