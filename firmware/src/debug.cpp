// ========================================================
// Debug-Modul
// ========================================================
// debug()-Funktionen mit Ringpuffer-Logging und Serial-Ausgabe.
// floatToString()-Hilfsfunktion fuer Float-zu-String-Konvertierung.

#define DEBUG_MODE  // AKTIVIERT fuer besseres Debugging
#include "debug.h"
#include "app_state.h"
#include "config.h"

// Zentrale AppState-Instanz (definiert in moodlight.cpp)
extern AppState appState;

// === Hilfsfunktion ===
String floatToString(float value, int decimalPlaces)
{
    char buffer[16];
    dtostrf(value, 0, decimalPlaces, buffer);
    return String(buffer);
}

#ifdef DEBUG_MODE
// === Debug-Funktion mit Logging ===
void debug(const String &message) {
    // Use static buffer for timestamps to avoid repeated allocations
    static char timeBuffer[16];
    static char messageBuffer[256];

    // Calculate timestamp
    unsigned long ms = millis();
    snprintf(timeBuffer, sizeof(timeBuffer), "[%lus] ", ms / 1000);

    // Copy message safely into buffer
    snprintf(messageBuffer, sizeof(messageBuffer), "%s%s", timeBuffer, message.c_str());

    // Store in ring buffer (use a local copy)
    String logEntry = messageBuffer; // Create the String locally
    appState.logBuffer[appState.logIndex] = logEntry;  // Copy assignment is safer
    appState.logIndex = (appState.logIndex + 1) % LOG_BUFFER_SIZE;

    // Print to Serial
    Serial.print(messageBuffer);
    Serial.print(F(" (Mem: "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(")"));
}

void debug(const __FlashStringHelper *message) {
    static char timeBuffer[16];
    static char messageBuffer[256];

    // Calculate timestamp
    unsigned long ms = millis();
    snprintf(timeBuffer, sizeof(timeBuffer), "[%lus] ", ms / 1000);

    // Create combined message (avoid String operations)
    snprintf(messageBuffer, sizeof(messageBuffer), "%s%s", timeBuffer, (const char*)message);

    // Store in ring buffer (using direct copy)
    String logEntry = messageBuffer;
    appState.logBuffer[appState.logIndex] = logEntry;
    appState.logIndex = (appState.logIndex + 1) % LOG_BUFFER_SIZE;

    // Print to Serial
    Serial.print(messageBuffer);
    Serial.print(F(" (Mem: "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(")"));
}
#else

void debug(const String &message) {
    if (message.startsWith("ERROR:") || message.startsWith("CRITICAL:")) {
        Serial.println(message);
    }
}

void debug(const __FlashStringHelper *message) {
    // Optional: Nur kritische Meldungen
    if (strstr_P((PGM_P)message, PSTR("ERROR:")) || strstr_P((PGM_P)message, PSTR("CRITICAL:"))) {
        Serial.println(message);
    }
}

#endif
