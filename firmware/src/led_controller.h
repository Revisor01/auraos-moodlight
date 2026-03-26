#pragma once

#include "app_state.h"
#include <Adafruit_NeoPixel.h>

// Hardware-Instanz — definiert in led_controller.cpp
extern Adafruit_NeoPixel pixels;

// === Farbdefinitionen ===
struct ColorDefinition
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// Farbnamen für UI (extern definiert in led_controller.cpp)
extern const char *colorNames[5];

// Hilfsfunktionen für Farbkonvertierung
ColorDefinition uint32ToColorDef(uint32_t color);
ColorDefinition getColorDefinition(int index);

// LED-Steuerungsfunktionen
void updateLEDs();
void setStatusLED(int mode);
void updateStatusLED();
void processLEDUpdates();
void pulseCurrentColor();
void updatePulse();
void initFirstLEDUpdate();
