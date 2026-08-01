// update_checker.h — Automatische Firmware-Updates ueber den Backend-Spiegel
//
// Das Geraet fragt stuendlich beim Backend nach, ob eine neuere Firmware
// freigegeben ist. Gefunden wird nur, was im Admin ausdruecklich freigegeben
// wurde — ein gespiegeltes Release allein reicht nicht.
//
// Installiert wird ausschliesslich auf Anforderung aus der WebUI. Ein Update,
// das sich von selbst einspielt, koennte das Licht mitten am Abend neu starten
// und im Fehlerfall ein Geraet zuruecklassen, an das niemand mehr herankommt.

#ifndef UPDATE_CHECKER_H
#define UPDATE_CHECKER_H

#include <Arduino.h>

// Fragt das Backend nach einer neueren Version.
// Ergebnis landet in appState.updateAvailable / updateVersion / updateReleaseUrl.
// Rueckgabe: true wenn die Abfrage durchlief (auch ohne neues Update).
bool checkForUpdate();

// Ruft checkForUpdate() im Stundentakt auf. Gehoert in die loop().
void handleUpdateCheck();

// Laedt die bereitstehende Firmware und flasht sie. Bei Erfolg startet das
// Geraet neu und kehrt nie zurueck. Bei Misserfolg bleibt die laufende
// Firmware unangetastet und updateLastError beschreibt den Grund.
bool downloadAndInstallUpdate();

#endif // UPDATE_CHECKER_H
