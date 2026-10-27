#pragma once

// Lichtlogik für Home Assistant, ohne Arduino-Abhängigkeit, damit sie nativ testbar ist.

#include <stdint.h>

// Was die LEDs zeigen, wenn das Licht an ist
struct ShownLight {
    uint32_t color;      // 0xRRGGBB
    uint8_t brightness;
};

// Wie ein Farbbefehl (HA oder Web-UI) behandelt wird
enum class ColorCommandAction : uint8_t {
    Ignore,                  // nichts zu tun
    StoreOnly,               // nur als manualColor merken, Anzeige bleibt
    Apply,                   // manualColor setzen und anzeigen (Modus ist bereits Manual)
    SwitchToManualAndApply   // auf Manual umschalten, dann anzeigen
};

inline int clampLedIndex(int index) {
    return index < 0 ? 0 : (index > 4 ? 4 : index);
}

// Auto: Farbe der Weltlage mit fester Helligkeit; Manual: eigene Farbe und Helligkeit.
inline ShownLight computeShownLight(bool autoMode, int ledIndex, const uint32_t customColors[5],
                                    uint32_t manualColor, uint8_t manualBrightness,
                                    uint8_t autoBrightness) {
    ShownLight shown;
    if (autoMode) {
        shown.color = customColors[clampLedIndex(ledIndex)] & 0xFFFFFF;
        shown.brightness = autoBrightness;
    } else {
        shown.color = manualColor & 0xFFFFFF;
        shown.brightness = manualBrightness;
    }
    return shown;
}

// Farbwahl im Auto-Modus schaltet auf Manual, außer in der Startphase, damit ein veralteter Befehl Auto nicht verlässt.
inline ColorCommandAction decideColorCommand(bool autoMode, bool startupGrace,
                                             uint32_t newColor, uint32_t manualColor) {
    bool sameColor = (newColor & 0xFFFFFF) == (manualColor & 0xFFFFFF);
    if (autoMode) {
        if (!startupGrace) return ColorCommandAction::SwitchToManualAndApply;
        return sameColor ? ColorCommandAction::Ignore : ColorCommandAction::StoreOnly;
    }
    return sameColor ? ColorCommandAction::Ignore : ColorCommandAction::Apply;
}
