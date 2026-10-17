#pragma once

// Reine Logik fuer den HA-Sensor "Weltlage relativ" (und "Weltlage Stufe").
// Die Lampe zeigt im Auto-Modus customColors[led_index]; led_index berechnet das
// Backend relativ zu den letzten 7 Tagen (Perzentile p20/p40/p60/p80). Der Sensor
// nennt diese Stufe in Worten — aber nur, wenn sie wirklich ein Wochenvergleich ist.
// Ohne Arduino-Abhaengigkeit, nativ getestet (firmware/test/test_relative_level_logic).

#include <stdio.h>
#include <stddef.h>
#include "ha_light_logic.h"  // clampLedIndex — dieselbe Begrenzung wie bei der LED-Farbe

static const int RELATIVE_LEVEL_UNKNOWN = -1;
// Vor dem ersten Abruf: unbekannt (nicht der AppState-Default currentLedIndex = 2)
static const int RELATIVE_LEVEL_INITIAL = RELATIVE_LEVEL_UNKNOWN;

// Stufe 0-4 aus der Backend-Antwort, sonst RELATIVE_LEVEL_UNKNOWN:
// - hasLedIndex = false: altes Backend, Farbe kommt aus dem lokalen, absoluten mapSentimentToLED()
// - thresholdFallback = true: Backend hat zu wenig Historie und nutzt feste Schwellwerte
// In beiden Faellen waere "als die Woche" eine falsche Behauptung.
inline int relativeLevelFromResponse(bool hasLedIndex, int ledIndex, bool thresholdFallback) {
    if (!hasLedIndex || thresholdFallback) return RELATIVE_LEVEL_UNKNOWN;
    return clampLedIndex(ledIndex);
}

inline const char *relativeLevelText(int level) {
    switch (level) {
        case 0: return "viel schlechter als die Woche";
        case 1: return "schlechter als die Woche";
        case 2: return "wie die Woche";
        case 3: return "besser als die Woche";
        case 4: return "viel besser als die Woche";
        default: return "unbekannt";
    }
}

// Zahl fuer den numerischen Sensor. false = unbekannt (Aufrufer sendet dann "None").
inline bool relativeLevelNumber(int level, char *buf, size_t bufSize) {
    if (level < 0 || level > 4 || bufSize < 2) return false;
    snprintf(buf, bufSize, "%d", level);
    return true;
}
