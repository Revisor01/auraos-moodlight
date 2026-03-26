#include "settings_manager.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include "MoodlightUtils.h"
#include "LittleFS.h"

// Externe Objekte — definiert in moodlight.cpp
extern AppState appState;
extern Preferences preferences;
extern Adafruit_NeoPixel pixels;
extern SafeFileOps fileOps;

extern void debug(const String &message);
extern void debug(const __FlashStringHelper *message);

bool saveSettingsToFile() {
    if (!LittleFS.exists("/data")) {
        if (!LittleFS.mkdir("/data")) {
            debug(F("Fehler beim Erstellen des /data Verzeichnisses"));
            return false;
        }
    }

    JsonDocument doc;

    // Allgemeine Einstellungen
    doc["moodInterval"] = appState.moodUpdateInterval;
    doc["dhtInterval"] = appState.dhtUpdateInterval;
    doc["autoMode"] = appState.autoMode;
    doc["lightOn"] = appState.lightOn;
    doc["manBright"] = appState.manualBrightness;
    doc["manColor"] = appState.manualColor;
    // v9.0: headlinesPS removed - only for legacy API endpoints

    // WiFi-Einstellungen
    doc["wifiSSID"] = appState.wifiSSID;
    doc["wifiPass"] = appState.wifiPassword;
    doc["wifiConfigured"] = appState.wifiConfigured;

    // Erweiterte Einstellungen
    doc["apiUrl"] = appState.apiUrl;
    doc["mqttServer"] = appState.mqttServer;
    doc["mqttUser"] = appState.mqttUser;
    doc["mqttPass"] = appState.mqttPassword;
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
    appState.moodUpdateInterval = doc["moodInterval"] | DEFAULT_MOOD_UPDATE_INTERVAL;
    appState.dhtUpdateInterval = doc["dhtInterval"] | DEFAULT_DHT_READ_INTERVAL;
    // v9.0: headlinesPS removed
    appState.autoMode = doc["autoMode"] | true;
    appState.lightOn = doc["lightOn"] | true;
    appState.manualBrightness = doc["manBright"] | DEFAULT_LED_BRIGHTNESS;
    appState.manualColor = doc["manColor"] | pixels.Color(255, 255, 255);

    // WiFi-Einstellungen
    appState.wifiSSID = doc["wifiSSID"] | "";
    {
        String loadedWifiPass = doc["wifiPass"] | "";
        if (loadedWifiPass != "****") {
            appState.wifiPassword = loadedWifiPass;
        }
    }
    appState.wifiConfigured = doc["wifiConfigured"] | false;

    // Erweiterte Einstellungen
    appState.apiUrl = doc["apiUrl"] | DEFAULT_NEWS_API_URL;
    appState.mqttServer = doc["mqttServer"] | "";
    appState.mqttUser = doc["mqttUser"] | "";
    {
        String loadedMqttPass = doc["mqttPass"] | "";
        if (loadedMqttPass != "****") {
            appState.mqttPassword = loadedMqttPass;
        }
    }
    appState.dhtPin = doc["dhtPin"] | DEFAULT_DHT_PIN;
    appState.dhtEnabled = doc["dhtEnabled"] | true;
    appState.ledPin = doc["ledPin"] | DEFAULT_LED_PIN;
    appState.numLeds = constrain(doc["numLeds"] | DEFAULT_NUM_LEDS, 1, MAX_LEDS);
    appState.statusLedIndex = appState.numLeds - 1;
    appState.mqttEnabled = doc["mqttEnabled"] | false;

    // Benutzerdefinierte Farben laden
    for (int i = 0; i < 5; i++) {
        String colorKey = "color" + String(i);
        if (doc[colorKey].is<uint32_t>()) {
            appState.customColors[i] = doc[colorKey].as<uint32_t>();
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
    preferences.putULong("moodInterval", appState.moodUpdateInterval);
    preferences.putULong("dhtInterval", appState.dhtUpdateInterval);
    preferences.putBool("autoMode", appState.autoMode);
    preferences.putBool("lightOn", appState.lightOn);
    preferences.putUChar("manBright", appState.manualBrightness);
    preferences.putUInt("manColor", appState.manualColor);
    // v9.0: headlinesPS removed

    // Speichere WiFi-Einstellungen
    preferences.putString("wifiSSID", appState.wifiSSID);
    preferences.putString("wifiPass", appState.wifiPassword);
    preferences.putBool("wifiConfigured", appState.wifiConfigured);

    // Speichere erweiterte Einstellungen
    preferences.putString("apiUrl", appState.apiUrl);
    preferences.putString("mqttServer", appState.mqttServer);
    preferences.putString("mqttUser", appState.mqttUser);
    preferences.putString("mqttPass", appState.mqttPassword);
    preferences.putInt("dhtPin", appState.dhtPin);
    preferences.putBool("dhtEnabled", appState.dhtEnabled);
    preferences.putInt("ledPin", appState.ledPin);
    preferences.putInt("numLeds", appState.numLeds);
    preferences.putBool("mqttEnabled", appState.mqttEnabled);

    // Speichere benutzerdefinierte Farben
    preferences.putUInt("color0", appState.customColors[0]);
    preferences.putUInt("color1", appState.customColors[1]);
    preferences.putUInt("color2", appState.customColors[2]);
    preferences.putUInt("color3", appState.customColors[3]);
    preferences.putUInt("color4", appState.customColors[4]);

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
        appState.moodUpdateInterval = DEFAULT_MOOD_UPDATE_INTERVAL;
        appState.dhtUpdateInterval = DEFAULT_DHT_READ_INTERVAL;
        // v9.0: headlines_per_source removed
        appState.autoMode = true;
        appState.lightOn = true;
        appState.manualBrightness = DEFAULT_LED_BRIGHTNESS;
        appState.manualColor = pixels.Color(255, 255, 255);

        // Lade WiFi-Einstellungen
        appState.wifiSSID = preferences.getString("wifiSSID", "");
        appState.wifiPassword = preferences.getString("wifiPass", "");
        appState.wifiConfigured = preferences.getBool("wifiConfigured", false);

        // Lade erweiterte Einstellungen
        appState.apiUrl = preferences.getString("apiUrl", DEFAULT_NEWS_API_URL);
        appState.mqttServer = preferences.getString("mqttServer", "");
        appState.mqttUser = preferences.getString("mqttUser", "");
        appState.mqttPassword = preferences.getString("mqttPass", "");
        appState.dhtPin = preferences.getInt("dhtPin", DEFAULT_DHT_PIN);
        appState.dhtEnabled = preferences.getBool("dhtEnabled", true);
        appState.ledPin = preferences.getInt("ledPin", DEFAULT_LED_PIN);
        appState.numLeds = constrain(preferences.getInt("numLeds", DEFAULT_NUM_LEDS), 1, MAX_LEDS);
        appState.statusLedIndex = appState.numLeds - 1;
        appState.mqttEnabled = preferences.getBool("mqttEnabled", false);

        // Benutzerdefinierte Farben laden
        appState.customColors[0] = preferences.getUInt("color0", 0xFF0000);
        appState.customColors[1] = preferences.getUInt("color1", 0xFFA500);
        appState.customColors[2] = preferences.getUInt("color2", 0x1E90FF);
        appState.customColors[3] = preferences.getUInt("color3", 0x545DF0);
        appState.customColors[4] = preferences.getUInt("color4", 0x8A2BE2);
        preferences.end();

        // Nach dem Laden aus Preferences, in Datei speichern für den nächsten Ladevorgang
        saveSettingsToFile();
        debug(F("Einstellungen aus Preferences in JSON-Datei migriert"));
    }

    // Log der geladenen Einstellungen
    debug(F("Einstellungen geladen:"));
    debug("  Mood Interval: " + String(appState.moodUpdateInterval / 1000) + "s");
    debug("  DHT Interval: " + String(appState.dhtUpdateInterval / 1000) + "s");
    debug(String(F("  DHT Enabled: ")) + (appState.dhtEnabled ? "ja" : "nein"));
    // v9.0: Headlines debugging removed
    debug(String(F("  AutoMode: ")) + (appState.autoMode ? "true" : "false"));
    debug(String(F("  LightOn: ")) + (appState.lightOn ? "true" : "false"));
    debug(String(F("  WiFi konfiguriert: ")) + (appState.wifiConfigured ? "ja" : "nein"));

    if (appState.wifiConfigured)
    {
        debug(String(F("  WiFi SSID: ")) + appState.wifiSSID);
    }

    debug(String(F("  API URL: ")) + appState.apiUrl);
    debug(String(F("  MQTT Enabled: ")) + (appState.mqttEnabled ? "ja" : "nein"));
    if (appState.mqttEnabled)
    {
        debug(String(F("  MQTT Server: ")) + appState.mqttServer);
    }
}
