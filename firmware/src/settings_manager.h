#pragma once

#include "app_state.h"
#include <Preferences.h>

// Hardware-Instanz — definiert in settings_manager.cpp
extern Preferences preferences;

// Settings-Manager: Laden und Speichern der Geraeteeinstellungen
// Implementierung in settings_manager.cpp

bool saveSettingsToFile();
bool loadSettingsFromFile();
void saveSettings();
void loadSettings();
