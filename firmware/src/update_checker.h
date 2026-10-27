// Firmware-Updates über den Backend-Spiegel; installiert wird nur auf Anforderung aus der WebUI.

#ifndef UPDATE_CHECKER_H
#define UPDATE_CHECKER_H

#include <Arduino.h>

// Fragt das Backend nach einer neueren Version; true, wenn die Abfrage durchlief.
bool checkForUpdate();

// Ruft checkForUpdate() im Stundentakt auf. Gehoert in die loop().
void handleUpdateCheck();

// Lädt und flasht die Firmware; bei Erfolg Neustart, bei Fehler steht der Grund in updateLastError.
bool downloadAndInstallUpdate();

#endif // UPDATE_CHECKER_H
