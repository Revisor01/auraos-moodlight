#pragma once

// Logik für "Weltlage relativ", ohne Arduino-Abhängigkeit, damit sie nativ testbar ist.

#include <stdio.h>
#include <stddef.h>
#include "ha_light_logic.h"  // clampLedIndex — dieselbe Begrenzung wie bei der LED-Farbe

static const int RELATIVE_LEVEL_UNKNOWN = -1;
// Vor dem ersten Abruf: unbekannt (nicht der AppState-Default currentLedIndex = 2)
static const int RELATIVE_LEVEL_INITIAL = RELATIVE_LEVEL_UNKNOWN;

// Stufe 0-4, sonst UNKNOWN: ohne led_index oder bei festen Schwellwerten wäre "als die Woche" falsch.
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
