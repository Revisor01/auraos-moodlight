// update_checker.cpp — Automatische Firmware-Updates ueber den Backend-Spiegel
//
// Ablauf: stuendlich /api/firmware/latest fragen, bei einer neueren freigegebenen
// Version das Binary per HTTP laden und in die freie OTA-Partition schreiben.
//
// Dieser Pfad ist der einzige, der ein Geraet unbrauchbar machen kann, deshalb
// wird vor jedem Schritt geprueft: genug Heap, plausible Groesse, ESP32-Magic-Byte
// als erstes Byte, vollstaendig empfangen. Schlaegt irgendetwas fehl, wird
// abgebrochen — die laufende Partition bleibt unberuehrt und das Geraet startet
// im Zweifel einfach wieder mit der alten Firmware.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "config.h"
#include "app_state.h"
#include "debug.h"
#include "update_checker.h"
#include "led_controller.h"
#include "sensor_manager.h"   // wifiClientHTTP — dieselbe Client-Instanz wie der Sentiment-Abruf
#include "MoodlightUtils.h"

extern WatchdogManager watchdog;

// Basis-URL des Backends. Getrennt von apiUrl, weil dort der komplette
// Sentiment-Pfad drinsteht — hier wird nur der Host gebraucht.
static String updateApiBase()
{
    return String(DEFAULT_UPDATE_API_BASE);
}

// Nur die nackte Version ohne Namenszusatz: MOODLIGHT_FULL_VERSION ist
// "9.16 - AuraOS", das Backend vergleicht aber gegen "9.16".
static String currentVersionForCompare()
{
    return String(MOODLIGHT_VERSION);
}

bool checkForUpdate()
{
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    // Waehrend eines laufenden Updates nicht dazwischenfunken
    if (appState.updateInProgress) {
        return false;
    }

    String url = updateApiBase() + "/api/firmware/latest?current=" + currentVersionForCompare();
    debug(String(F("Update-Pruefung: ")) + url);

    HTTPClient http;
    http.setReuse(false);
    http.setUserAgent("MoodlightClient/1.0");

    if (wifiClientHTTP.connected()) {
        wifiClientHTTP.stop();
        delay(10);
    }

    if (!http.begin(wifiClientHTTP, url)) {
        debug(F("Update-Pruefung: HTTP Begin fehlgeschlagen"));
        return false;
    }

    http.setTimeout(UPDATE_HTTP_TIMEOUT);
    int httpCode = http.GET();
    watchdog.feed();  // Blockierender Call — WDT sofort fuettern

    if (httpCode != HTTP_CODE_OK) {
        debug(String(F("Update-Pruefung: HTTP ")) + String(httpCode));
        http.end();
        return false;
    }

    // Antwort ist klein (wenige hundert Byte) — direkt vom Stream parsen
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream());
    http.end();

    if (error) {
        debug(String(F("Update-Pruefung: JSON-Fehler ")) + error.c_str());
        return false;
    }

    bool available = doc["update_available"] | false;

    if (!available) {
        appState.updateAvailable = false;
        appState.updateVersion = "";
        appState.updateReleaseUrl = "";
        appState.updateFirmwarePath = "";
        appState.updateFirmwareSize = 0;
        debug(F("Update-Pruefung: aktuelle Version ist die neueste"));
        return true;
    }

    const char *version = doc["latest"] | "";
    const char *fwPath = doc["firmware_url"] | "";
    const char *relUrl = doc["release_url"] | "";
    size_t fwSize = doc["firmware_size"] | 0;

    // Ohne Version oder Pfad ist die Antwort unbrauchbar
    if (strlen(version) == 0 || strlen(fwPath) == 0) {
        debug(F("Update-Pruefung: Antwort ohne Version oder Pfad"));
        return false;
    }

    // Groessenplausibilitaet schon hier pruefen — spart einen sinnlosen Download
    if (fwSize > 0 && fwSize < UPDATE_MIN_FIRMWARE_SIZE) {
        debug(String(F("Update-Pruefung: gemeldete Groesse zu klein (")) +
              String(fwSize) + F(" Bytes) — ignoriert"));
        return false;
    }

    appState.updateAvailable = true;
    appState.updateVersion = String(version);
    appState.updateReleaseUrl = String(relUrl);
    appState.updateFirmwarePath = String(fwPath);
    appState.updateFirmwareSize = fwSize;

    debug(String(F("Update verfuegbar: ")) + appState.updateVersion +
          F(" (") + String(fwSize) + F(" Bytes)"));
    return true;
}

void handleUpdateCheck()
{
    if (!appState.updateCheckEnabled) {
        return;
    }

    if (WiFi.status() != WL_CONNECTED || appState.updateInProgress) {
        return;
    }

    // Nach dem Boot erst zur Ruhe kommen lassen: WLAN, NTP und der erste
    // Sentiment-Abruf sind wichtiger als die Update-Frage.
    if (millis() < UPDATE_CHECK_INITIAL_DELAY) {
        return;
    }

    // Erster Durchlauf (lastUpdateCheck == 0) sofort, danach im Stundentakt
    if (appState.lastUpdateCheck != 0 &&
        (millis() - appState.lastUpdateCheck) < UPDATE_CHECK_INTERVAL) {
        return;
    }

    appState.lastUpdateCheck = millis();
    checkForUpdate();
}

bool downloadAndInstallUpdate()
{
    if (appState.updateInProgress) {
        appState.updateLastError = F("Es laeuft bereits ein Update");
        return false;
    }

    if (!appState.updateAvailable || appState.updateFirmwarePath.length() == 0) {
        appState.updateLastError = F("Kein Update vorgemerkt");
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        appState.updateLastError = F("Keine WLAN-Verbindung");
        return false;
    }

    // Ohne ausreichend Heap gar nicht erst anfangen: mitten im Flash-Vorgang
    // auszugehen ist der schlimmste Zeitpunkt.
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < UPDATE_MIN_FREE_HEAP) {
        appState.updateLastError = String(F("Zu wenig freier Speicher (")) +
                                   String(freeHeap) + F(" Bytes)");
        debug(appState.updateLastError);
        return false;
    }

    appState.updateInProgress = true;
    appState.updateLastError = "";
    setStatusLED(3);  // Update-Modus

    String url = updateApiBase() + appState.updateFirmwarePath;
    debug(String(F("Firmware-Download: ")) + url);

    HTTPClient http;
    http.setReuse(false);
    http.setUserAgent("MoodlightClient/1.0");

    if (wifiClientHTTP.connected()) {
        wifiClientHTTP.stop();
        delay(10);
    }

    if (!http.begin(wifiClientHTTP, url)) {
        appState.updateLastError = F("Verbindung zum Backend fehlgeschlagen");
        appState.updateInProgress = false;
        setStatusLED(0);
        return false;
    }

    http.setTimeout(UPDATE_DOWNLOAD_TIMEOUT);
    int httpCode = http.GET();
    watchdog.feed();

    if (httpCode != HTTP_CODE_OK) {
        appState.updateLastError = String(F("Backend antwortete mit HTTP ")) + String(httpCode);
        debug(appState.updateLastError);
        http.end();
        appState.updateInProgress = false;
        setStatusLED(0);
        return false;
    }

    int contentLength = http.getSize();

    // Ohne bekannte Laenge kein Update: Update.begin() braucht die Groesse, um
    // die Zielpartition zu pruefen, und ohne sie faellt die
    // Vollstaendigkeitskontrolle am Ende weg.
    if (contentLength <= 0) {
        appState.updateLastError = F("Backend liefert keine Dateigroesse");
        debug(appState.updateLastError);
        http.end();
        appState.updateInProgress = false;
        setStatusLED(0);
        return false;
    }

    if ((size_t)contentLength < UPDATE_MIN_FIRMWARE_SIZE) {
        appState.updateLastError = String(F("Datei zu klein (")) +
                                   String(contentLength) + F(" Bytes)");
        debug(appState.updateLastError);
        http.end();
        appState.updateInProgress = false;
        setStatusLED(0);
        return false;
    }

    // Update.begin() prueft selbst, ob die Datei in die freie OTA-Partition passt
    if (!Update.begin(contentLength)) {
        appState.updateLastError = String(F("Update.begin fehlgeschlagen: ")) +
                                   String(Update.errorString());
        debug(appState.updateLastError);
        http.end();
        appState.updateInProgress = false;
        setStatusLED(0);
        return false;
    }

    debug(String(F("Schreibe ")) + String(contentLength) + F(" Bytes in die OTA-Partition"));

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[1024];
    size_t written = 0;
    bool magicChecked = false;
    unsigned long lastData = millis();

    while (http.connected() && written < (size_t)contentLength) {
        size_t available = stream->available();

        if (available == 0) {
            // Stillstand erkennen: haengt die Verbindung, nicht ewig warten
            if (millis() - lastData > UPDATE_DOWNLOAD_TIMEOUT) {
                appState.updateLastError = F("Download abgebrochen (Zeitueberschreitung)");
                debug(appState.updateLastError);
                Update.abort();
                http.end();
                appState.updateInProgress = false;
                setStatusLED(0);
                return false;
            }
            watchdog.feed();
            delay(10);
            continue;
        }

        size_t toRead = available > sizeof(buffer) ? sizeof(buffer) : available;
        size_t read = stream->readBytes(buffer, toRead);
        if (read == 0) {
            continue;
        }
        lastData = millis();

        // Dieselbe Bricking-Vorsorge wie beim Upload ueber die WebUI: eine
        // ESP32-Firmware beginnt mit 0xE9. Was damit nicht anfaengt, wird nicht
        // geflasht — egal was das Backend behauptet.
        if (!magicChecked) {
            magicChecked = true;
            if (buffer[0] != 0xE9) {
                appState.updateLastError = F("Datei ist keine ESP32-Firmware (Magic-Byte fehlt)");
                debug(appState.updateLastError);
                Update.abort();
                http.end();
                appState.updateInProgress = false;
                setStatusLED(0);
                return false;
            }
        }

        if (Update.write(buffer, read) != read) {
            appState.updateLastError = String(F("Schreibfehler: ")) +
                                       String(Update.errorString());
            debug(appState.updateLastError);
            Update.abort();
            http.end();
            appState.updateInProgress = false;
            setStatusLED(0);
            return false;
        }

        written += read;
        watchdog.feed();  // Der Download dauert; WDT bei jedem Block fuettern
    }

    http.end();

    // Abgebrochene Verbindung erkennen: Update.end() wuerde eine halbe Firmware
    // sonst als gueltig durchwinken, das Geraet startet neu und kommt nicht wieder.
    if (written != (size_t)contentLength) {
        appState.updateLastError = String(F("Unvollstaendig: ")) + String(written) +
                                   F(" von ") + String(contentLength) + F(" Bytes");
        debug(appState.updateLastError);
        Update.abort();
        appState.updateInProgress = false;
        setStatusLED(0);
        return false;
    }

    if (!Update.end(true)) {
        appState.updateLastError = String(F("Abschluss fehlgeschlagen: ")) +
                                   String(Update.errorString());
        debug(appState.updateLastError);
        appState.updateInProgress = false;
        setStatusLED(0);
        return false;
    }

    if (!Update.isFinished()) {
        appState.updateLastError = F("Update wurde nicht abgeschlossen");
        debug(appState.updateLastError);
        appState.updateInProgress = false;
        setStatusLED(0);
        return false;
    }

    // Version festhalten, damit /api/firmware-version nach dem Neustart stimmt —
    // dieselbe Datei nutzt der Upload-Weg ueber die WebUI.
    File versionFile = LittleFS.open("/firmware-version.txt", "w");
    if (versionFile) {
        versionFile.print(appState.updateVersion);
        versionFile.close();
    }

    debug(String(F("Update auf ")) + appState.updateVersion +
          F(" geschrieben — Neustart"));

    appState.updateAvailable = false;
    appState.updateInProgress = false;

    // Neustart der loop() ueberlassen: so geht die HTTP-Antwort an die WebUI
    // noch raus, bevor das Geraet weg ist.
    appState.rebootNeeded = true;
    appState.rebootTime = millis() + 1500;

    return true;
}
