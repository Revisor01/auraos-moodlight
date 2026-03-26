#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>

// Debug-Logging mit Ringpuffer und Serial-Ausgabe
void debug(const String &message);
void debug(const __FlashStringHelper *message);

// Hilfsfunktion fuer Float-zu-String-Konvertierung
String floatToString(float value, int decimalPlaces);

#endif // DEBUG_H
