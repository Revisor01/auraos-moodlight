#pragma once

// Reine Entscheidungslogik fuer die Home-Assistant-Lichtentitaet.
// Bewusst ohne Arduino-, NeoPixel- oder ArduinoHA-Abhaengigkeit, damit sie
// nativ getestet werden kann (firmware/test/test_ha_light_logic, `pio test -e native`).

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

// Angezeigte Farbe/Helligkeit: Auto = Farbe der Weltlage bei fester Auto-Helligkeit,
// Manual = manuelle Farbe und Helligkeit.
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

// Wer im Auto-Modus eine Farbe waehlt, will sie sehen: Umschalten auf Manual —
// auch wenn die Farbe der gespeicherten manualColor gleicht.
// startupGrace: Startphase nach dem Boot. Dort schaltet ein Farbbefehl den Modus
// nicht um (wie onModeCommand), damit ein veralteter Befehl beim Verbinden
// den Auto-Modus nicht verlaesst.
inline ColorCommandAction decideColorCommand(bool autoMode, bool startupGrace,
                                             uint32_t newColor, uint32_t manualColor) {
    bool sameColor = (newColor & 0xFFFFFF) == (manualColor & 0xFFFFFF);
    if (autoMode) {
        if (!startupGrace) return ColorCommandAction::SwitchToManualAndApply;
        return sameColor ? ColorCommandAction::Ignore : ColorCommandAction::StoreOnly;
    }
    return sameColor ? ColorCommandAction::Ignore : ColorCommandAction::Apply;
}
