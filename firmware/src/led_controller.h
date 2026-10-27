#pragma once

#include "app_state.h"
#include "ha_light_logic.h"
#include <Adafruit_NeoPixel.h>

// Zur Laufzeit erzeugt: der GPIO wird auf dem ESP32 nur im Konstruktor zuverlässig gesetzt.
extern Adafruit_NeoPixel *pixelsPtr;

// Alle bestehenden Aufrufstellen nutzen weiterhin "pixels.foo()".
#define pixels (*pixelsPtr)

// Muss vor jeder LED-Nutzung genau einmal aufgerufen werden.
void initPixels();

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

// Bei eingeschaltetem Licht angezeigte Farbe und Helligkeit (Auto: Weltlage, Manual: eigene Werte).
ShownLight currentShownLight();

// LED-Steuerungsfunktionen
void updateLEDs();
void setStatusLED(int mode);
void updateStatusLED();
void processLEDUpdates();
void initFirstLEDUpdate();
