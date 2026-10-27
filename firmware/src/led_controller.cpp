#include "led_controller.h"
#include "debug.h"

extern AppState appState;

// Erst in initPixels() mit echten Parametern erzeugen: begin() verwirft einen später per setPin() gesetzten Pin.
#undef pixels
Adafruit_NeoPixel *pixelsPtr = nullptr;
#define pixels (*pixelsPtr)

void initPixels() {
    if (pixelsPtr) return;  // nur einmal
    pixelsPtr = new Adafruit_NeoPixel(appState.numLeds, appState.ledPin,
                                      NEO_GRB + NEO_KHZ800);
    pixelsPtr->begin();
    pixelsPtr->setBrightness(DEFAULT_LED_BRIGHTNESS);
    pixelsPtr->clear();
    pixelsPtr->show();
}

// Farbnamen für UI
const char *colorNames[5] = {
    "Sehr negativ",
    "Negativ",
    "Neutral",
    "Positiv",
    "Sehr positiv"};

// Hilfsfunktion, um uint32_t in ColorDefinition zu konvertieren
ColorDefinition uint32ToColorDef(uint32_t color)
{
    ColorDefinition def;
    def.r = (color >> 16) & 0xFF;
    def.g = (color >> 8) & 0xFF;
    def.b = color & 0xFF;
    return def;
}

// Hilfsfunktion zum dynamischen Abrufen von Farbdefinitionen
ColorDefinition getColorDefinition(int index)
{
    index = constrain(index, 0, 4);
    return uint32ToColorDef(appState.customColors[index]);
}

// Angezeigte Farbe/Helligkeit — eine Quelle fuer LEDs und Home Assistant
ShownLight currentShownLight() {
    return computeShownLight(appState.autoMode, appState.currentLedIndex, appState.customColors,
                             appState.manualColor, appState.manualBrightness,
                             DEFAULT_LED_BRIGHTNESS);
}

// === Aktualisiere die LEDs ===
void updateLEDs() {
    // Im MQTT-Callback nur das Flag setzen, gesendet wird in loop().
    appState.haLightStateDirty = true;

    if (!appState.lightOn) {
        if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            appState.ledClear = true;
            appState.ledUpdatePending = true;
            xSemaphoreGive(appState.ledMutex);
        }
        return;
    }

    appState.currentLedIndex = constrain(appState.currentLedIndex, 0, 4);
    ShownLight shown = currentShownLight();
    uint32_t colorToShow = shown.color;
    uint8_t brightnessToShow = shown.brightness;

    // Instead of directly updating LEDs, store the values safely
    if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        for (int i = 0; i < appState.numLeds; i++) {
            if (i == (appState.numLeds - 1) && appState.statusLedMode != 0) {
                continue;
            }
            appState.ledColors[i] = colorToShow;
        }
        appState.ledBrightness = brightnessToShow;
        appState.ledClear = false;
        appState.ledUpdatePending = true;
        xSemaphoreGive(appState.ledMutex);
    }

    uint8_t r = (colorToShow >> 16) & 0xFF;
    uint8_t g = (colorToShow >> 8) & 0xFF;
    uint8_t b = colorToShow & 0xFF;

    char debugBuffer[100];
    snprintf(debugBuffer, sizeof(debugBuffer),
             "LEDs update requested: %s B=%d RGB(%d,%d,%d)",
             (appState.autoMode ? "Auto" : "Manual"),
             brightnessToShow, r, g, b);
    debug(debugBuffer);
}

// === Status-LED Funktionen ===
static const char* statusLedModeName(int mode) {
    switch (mode) {
        case 1: return "WLAN-Verbindung (blau)";
        case 2: return "API-Fehler (rot)";
        case 3: return "Update (grün)";
        case 4: return "MQTT-Verbindung (cyan)";
        case 5: return "AP-Modus (gelb)";
        default: return "Normal";
    }
}

void setStatusLED(int mode) {
    int prevMode = appState.statusLedMode;
    if (mode != prevMode) {
        char logBuffer[112];
        unsigned long activeSecs = (millis() - appState.statusLedModeSince) / 1000UL;
        if (prevMode == 0) {
            snprintf(logBuffer, sizeof(logBuffer),
                     "Status-LED: Blinken AN — %s", statusLedModeName(mode));
        } else if (mode == 0) {
            snprintf(logBuffer, sizeof(logBuffer),
                     "Status-LED: Blinken AUS (war: %s, Dauer %lus)",
                     statusLedModeName(prevMode), activeSecs);
        } else {
            snprintf(logBuffer, sizeof(logBuffer),
                     "Status-LED: Wechsel %s → %s (nach %lus)",
                     statusLedModeName(prevMode), statusLedModeName(mode), activeSecs);
        }
        debug(logBuffer);
        appState.statusLedModeSince = millis();
    }

    appState.statusLedMode = mode;
    appState.statusLedBlinkStart = millis();
    appState.statusLedState = true;

    // Store LED state but don't update directly
    if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        uint32_t statusColor = pixels.Color(0, 0, 0); // Default black

        switch (mode) {
            case 1: // WiFi-Verbindung (blau blinkend)
                statusColor = pixels.Color(0, 0, 255);
                break;
            case 2: // API-Fehler (rot blinkend)
                statusColor = pixels.Color(255, 0, 0);
                break;
            case 3: // Update (grün blinkend)
                statusColor = pixels.Color(0, 255, 0);
                break;
            case 4: // MQTT-Verbindung (cyan blinkend)
                statusColor = pixels.Color(0, 255, 255);
                break;
            case 5: // AP-Modus (gelb blinkend)
                statusColor = pixels.Color(255, 255, 0);
                break;
            default: // Normal (LED aus oder normaler Betrieb)
                appState.ledUpdatePending = true;
                xSemaphoreGive(appState.ledMutex);
                return;
        }

        appState.ledColors[appState.statusLedIndex] = statusColor;
        appState.ledUpdatePending = true;
        xSemaphoreGive(appState.ledMutex);
    }
}

void updateStatusLED() {
    if (appState.statusLedMode == 0)
        return;

    unsigned long currentMillis = millis();

    unsigned long blinkInterval;
    switch (appState.statusLedMode) {
        case 1: blinkInterval = 500; break; // WiFi (schnell)
        case 2: blinkInterval = 300; break; // API-Fehler (sehr schnell)
        case 3: blinkInterval = 1000; break; // Update (langsam)
        case 4: blinkInterval = 700; break; // MQTT (mittel)
        case 5: blinkInterval = 200; break; // AP-Modus (sehr schnell)
        default: blinkInterval = 500;
    }

    if (currentMillis - appState.statusLedBlinkStart >= blinkInterval) {
        appState.statusLedState = !appState.statusLedState;
        appState.statusLedBlinkStart = currentMillis;

        if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            if (appState.statusLedState) {
                uint32_t statusColor = pixels.Color(0, 0, 0);

                switch (appState.statusLedMode) {
                    case 1: statusColor = pixels.Color(0, 0, 255); break; // Blau
                    case 2: statusColor = pixels.Color(255, 0, 0); break; // Rot
                    case 3: statusColor = pixels.Color(0, 255, 0); break; // Grün
                    case 4: statusColor = pixels.Color(0, 255, 255); break; // Cyan
                    case 5: statusColor = pixels.Color(255, 255, 0); break; // Gelb
                }

                appState.ledColors[appState.statusLedIndex] = statusColor;
            } else {
                appState.ledColors[appState.statusLedIndex] = pixels.Color(0, 0, 0);
            }

            appState.ledUpdatePending = true;
            xSemaphoreGive(appState.ledMutex);
        }
    }
}

void processLEDUpdates() {
    static unsigned long lastLedUpdateTime = 0;
    unsigned long currentTime = millis();

    // Only process LED updates every 50ms maximum to avoid conflicts with WiFi
    if (currentTime - lastLedUpdateTime < 50) {
        return;
    }

    bool needsUpdate = false;

    if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        needsUpdate = appState.ledUpdatePending;
        if (needsUpdate) {
            if (appState.ledClear) {
                pixels.clear();
            } else {
                // setBrightness() vor setPixelColor(), sonst dunkelt jeder Durchlauf den Puffer weiter ab.
                pixels.setBrightness(appState.ledBrightness);
                for (int i = 0; i < appState.numLeds; i++) {
                    pixels.setPixelColor(i, appState.ledColors[i]);
                }
            }
            appState.ledUpdatePending = false;
        }
        xSemaphoreGive(appState.ledMutex);
    }

    // Only call show() if we have updates and not in a critical section
    if (needsUpdate) {
        // LEDs nur aktualisieren wenn safe UND kein WiFi-Reconnect aktiv
        if (appState.ledSafeToShow && !appState.wifiReconnectActive) {
            yield();
            delay(1);

            // WiFi Power Save nicht umschalten, das destabilisiert den WiFi-Stack bei schwachem Signal.
            // Keine Interrupt-Sperre um show(): RMT gibt interruptgesteuert aus, sonst bleibt der Ring dunkel.
            pixels.show();
            lastLedUpdateTime = currentTime;
        } else {
            // Flag zurücksetzen, damit der nächste Durchlauf die Ausgabe erneut versucht.
            if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
                appState.ledUpdatePending = true;
                xSemaphoreGive(appState.ledMutex);
            }
        }
    }
}

// === Erste LED-Initialisierung nach Setup ===
void initFirstLEDUpdate() {
    if (appState.firstLedShowDone) return;
    if (xSemaphoreTake(appState.ledMutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
        for (int i = 0; i < appState.numLeds; i++) {
            appState.ledColors[i] = 0;
        }
        appState.ledClear = true;
        appState.ledUpdatePending = true;
        xSemaphoreGive(appState.ledMutex);
    }
    appState.firstLedShowDone = true;
    debug(F("First LED update scheduled"));

    // Sonst bleibt der Ring bis zum nächsten Ereignis dunkel, beim 30-Minuten-Poll bis zu einer halben Stunde.
    updateLEDs();
    debug(F("Initialer LED-Zustand ausgegeben"));
}

