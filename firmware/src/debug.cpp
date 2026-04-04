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
// Verwendet fix-groesse char-Arrays im Ringpuffer statt String-Objekte.
// Verhindert Heap-Fragmentierung durch staendiges Allokieren/Freigeben.
void debug(const String &message) {
    unsigned long ms = millis();

    // Direkt in den Ringpuffer-Slot schreiben (kein String-Objekt, kein Heap)
    snprintf(appState.logBuffer[appState.logIndex], AppState::LOG_ENTRY_SIZE,
             "[%lus] %s", ms / 1000, message.c_str());

    // Print to Serial
    Serial.print(appState.logBuffer[appState.logIndex]);
    Serial.print(F(" (Mem: "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(")"));

    appState.logIndex = (appState.logIndex + 1) % LOG_BUFFER_SIZE;
}

void debug(const __FlashStringHelper *message) {
    unsigned long ms = millis();

    // Direkt in den Ringpuffer-Slot schreiben (kein String-Objekt, kein Heap)
    snprintf(appState.logBuffer[appState.logIndex], AppState::LOG_ENTRY_SIZE,
             "[%lus] %s", ms / 1000, (const char*)message);

    // Print to Serial
    Serial.print(appState.logBuffer[appState.logIndex]);
    Serial.print(F(" (Mem: "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(")"));

    appState.logIndex = (appState.logIndex + 1) % LOG_BUFFER_SIZE;
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
