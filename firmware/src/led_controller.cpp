#include "led_controller.h"

extern AppState appState;
extern Adafruit_NeoPixel pixels;
extern void debug(const String &message);
extern void debug(const __FlashStringHelper *message);

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

// === Aktualisiere die LEDs ===
void updateLEDs() {
    if (!appState.lightOn) {
        // Just set the clear flag and return
        if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            appState.ledClear = true;
            appState.ledUpdatePending = true;
            xSemaphoreGive(appState.ledMutex);
        }
        return;
    }

    // Licht soll an sein
    uint32_t colorToShow;
    uint8_t brightnessToShow;

    if (appState.autoMode) {
        appState.currentLedIndex = constrain(appState.currentLedIndex, 0, 4);
        ColorDefinition color = getColorDefinition(appState.currentLedIndex);
        colorToShow = pixels.Color(color.r, color.g, color.b);
        brightnessToShow = DEFAULT_LED_BRIGHTNESS;
    } else {
        colorToShow = appState.manualColor;
        brightnessToShow = appState.manualBrightness;
    }

    // Instead of directly updating LEDs, store the values safely
    if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        // Fill LED buffer
        for (int i = 0; i < appState.numLeds; i++) {
            // Skip status LED if in status mode
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

    // Debug output optimized
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
void setStatusLED(int mode) {
    appState.statusLedMode = mode;
    appState.statusLedBlinkStart = millis();
    appState.statusLedState = true;

    // Store LED state but don't update directly
    if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        // LED-Farbe entsprechend dem Status setzen
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
                // Just update the whole LED strip
                appState.ledUpdatePending = true;
                xSemaphoreGive(appState.ledMutex);
                return;
        }

        // Only update the status LED
        appState.ledColors[appState.statusLedIndex] = statusColor;
        appState.ledUpdatePending = true;
        xSemaphoreGive(appState.ledMutex);
    }
}

void updateStatusLED() {
    // Kein Blinken im Normalmodus
    if (appState.statusLedMode == 0)
        return;

    // Zeit für Blink-Update
    unsigned long currentMillis = millis();

    // Unterschiedliche Blink-Frequenzen für verschiedene Modi
    unsigned long blinkInterval;
    switch (appState.statusLedMode) {
        case 1: blinkInterval = 500; break; // WiFi (schnell)
        case 2: blinkInterval = 300; break; // API-Fehler (sehr schnell)
        case 3: blinkInterval = 1000; break; // Update (langsam)
        case 4: blinkInterval = 700; break; // MQTT (mittel)
        case 5: blinkInterval = 200; break; // AP-Modus (sehr schnell)
        default: blinkInterval = 500;
    }

    // Zeit abgelaufen?
    if (currentMillis - appState.statusLedBlinkStart >= blinkInterval) {
        appState.statusLedState = !appState.statusLedState;
        appState.statusLedBlinkStart = currentMillis;

        if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            if (appState.statusLedState) {
                // LED einschalten mit entsprechender Farbe
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
                // LED ausschalten
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

    // Check if we have pending LED updates
    bool needsUpdate = false;

    if (xSemaphoreTake(appState.ledMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        needsUpdate = appState.ledUpdatePending;
        if (needsUpdate) {
            // Apply the stored LED values
            if (appState.ledClear) {
                pixels.clear();
            } else {
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
        // LED-02: LEDs nur aktualisieren wenn kein WiFi-Reconnect aktiv ist
        if (!appState.wifiReconnectActive) {
            // Wait for any ongoing interrupt operations
            yield();
            delay(1);

            // Now it should be safe to update LEDs
            pixels.show();
            lastLedUpdateTime = currentTime;

            static unsigned long lastShowDebug = 0;
            if (currentTime - lastShowDebug > 10000) {
                debug(F("LED show() executed safely"));
                lastShowDebug = currentTime;
            }
        }
    }
}

void pulseCurrentColor()
{
    uint8_t targetBrightness = appState.autoMode ? DEFAULT_LED_BRIGHTNESS : appState.manualBrightness;

    // Debug pulsing state periodically
    static unsigned long lastPulseDebug = 0;
    if (millis() - lastPulseDebug >= 10000)
    { // Every 10 seconds
        if (appState.isPulsing)
        {
            debug(String(F("Pulse Status: Aktiv - Laufzeit: ")) + String((millis() - appState.pulseStartTime) / 1000) + "s");
        }
        lastPulseDebug = millis();
    }

    if (!appState.isPulsing || !appState.lightOn)
    {
        // Stelle sicher, dass Helligkeit korrekt ist, wenn nicht gepulst wird
        static uint8_t lastSetBrightness = 0;
        if (pixels.getBrightness() != targetBrightness)
        {
            pixels.setBrightness(targetBrightness);
            pixels.show(); // Zeige korrekte Helligkeit
            debug(String(F("Pulse: Helligkeit zurückgesetzt auf ")) + targetBrightness);
        }
        return;
    }

    unsigned long currentTime = millis();
    unsigned long elapsedTime = currentTime - appState.pulseStartTime;

    // Auto-disable pulsing after timeout (3 cycles)
    if (elapsedTime > DEFAULT_WAVE_DURATION * 3)
    {
        debug(F("Pulse: Timeout - Auto-disable nach 3 Zyklen"));
        appState.isPulsing = false;
        pixels.setBrightness(targetBrightness);
        pixels.show();
        return;
    }

    float progress = fmod((float)elapsedTime, (float)DEFAULT_WAVE_DURATION) / (float)DEFAULT_WAVE_DURATION;
    float easedValue = (sin(progress * 2.0 * PI - PI / 2.0) + 1.0) / 2.0;

    // Use the config values instead of hardcoded ones
    // Explizite Typkonvertierung, um den Compilerfehler zu vermeiden
    uint8_t minPulseBright = (DEFAULT_WAVE_MIN_BRIGHTNESS < targetBrightness / 2) ? DEFAULT_WAVE_MIN_BRIGHTNESS : (targetBrightness / 2);
    uint8_t maxPulseBright = (DEFAULT_WAVE_MAX_BRIGHTNESS < targetBrightness) ? DEFAULT_WAVE_MAX_BRIGHTNESS : targetBrightness;

    int brightness = minPulseBright + (int)(easedValue * (maxPulseBright - minPulseBright));

    pixels.setBrightness(constrain(brightness, 0, 255));
    pixels.show();
}
