#pragma once

#include "app_state.h"
#include <Preferences.h>

// Hardware-Instanz — definiert in settings_manager.cpp
extern Preferences preferences;

// Laden und Speichern der Geräteeinstellungen

bool saveSettingsToFile();
bool loadSettingsFromFile();
void saveSettings();
void loadSettings();
