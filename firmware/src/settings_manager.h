#pragma once

#include "app_state.h"

// Settings-Manager: Laden und Speichern der Geraeteeinstellungen
// Implementierung in settings_manager.cpp

bool saveSettingsToFile();
bool loadSettingsFromFile();
void saveSettings();
void loadSettings();
