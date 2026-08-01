#pragma once

#include "app_state.h"
#include <Adafruit_NeoPixel.h>

// Hardware-Instanz — in led_controller.cpp zur Laufzeit erzeugt.
// Die Bibliothek konfiguriert den GPIO nur im Konstruktor zuverlaessig: ein
// parameterlos konstruiertes Objekt nachtraeglich per setPin()/begin()
// umzustellen funktioniert auf dem ESP32 nicht, weil begin() intern setPin()
// mit dem gespeicherten Wert (-1) aufruft und die Pin-Nummer damit verwirft.
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

// LED-Steuerungsfunktionen
void updateLEDs();
void setStatusLED(int mode);
void updateStatusLED();
void processLEDUpdates();
void initFirstLEDUpdate();
