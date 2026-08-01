---
status: resolved
trigger: "Weder Browser-Steuerung noch Home Assistant wirken. Die Lampe zeigt immer blau, auch im Auto-Modus. Sie geht auch nicht aus."
created: 2026-08-01T14:15:00Z
updated: 2026-08-01T16:40:00Z
---

## Ursache

`Adafruit_NeoPixel pixels;` wurde parameterlos konstruiert und erst später per
`updateType()` / `updateLength()` / `setPin()` / `begin()` konfiguriert.

Das funktioniert auf dem ESP32 nicht: **`begin()` ruft intern `setPin()` mit dem
im Objekt gespeicherten Pin auf.** Bei parameterloser Konstruktion ist dieser
Wert `-1`. Ein zuvor gesetzter Pin wird dadurch wieder verworfen — GPIO 26 wurde
nie als Ausgang konfiguriert, der Ring bekam nie ein Signal.

Die Firmware verhielt sich dabei völlig unauffällig: `pixels.show()` lief,
`showCount` zählte hoch, `showBlocked` blieb 0, keine Fehler, kein Crash.

## Fix

`initPixels()` in `led_controller.cpp` erzeugt die Instanz mit den echten
Parametern — genau so wie im minimalen Testsketch, der nachweislich funktioniert:

```cpp
pixelsPtr = new Adafruit_NeoPixel(appState.numLeds, appState.ledPin,
                                  NEO_GRB + NEO_KHZ800);
pixelsPtr->begin();
```

Ein Makro `#define pixels (*pixelsPtr)` im Header hält alle ~30 bestehenden
Aufrufstellen unverändert.

## Verifiziert am Gerät

- Rot, Grün, Blau, Weiß über `/api/led-test` — alle korrekt
- Farbwechsel über `/set-color` im Manuell-Modus — korrekt
- Moduswechsel Auto/Manuell — korrekt
- Licht aus/an — korrekt
- Auto-Modus zeigt die Sentiment-Farbe (#545DF0 bei Score -0.35)
- Kein Crash, stabile Uptime

## Der entscheidende Test

Ein minimaler Sketch (nur NeoPixel, kein WiFi/MQTT/Webserver) lief fehlerfrei:
alle Farben, Lauflicht über alle 12 LEDs. Damit war Hardware ausgeschlossen —
Ring, Datenleitung, GPIO 26, Stromversorgung, Farbreihenfolge GRB, LED-Anzahl.

Der einzige strukturelle Unterschied zur Hauptfirmware war die Konstruktion der
Instanz. Genau dort lag der Fehler.

Testprogramm: `.planning/debug/ledtest-minimal.cpp`

## Weitere behobene Fehler

1. **Kein Initial-Update:** `initFirstLEDUpdate()` löschte den Ring auf Schwarz;
   nichts gab danach den echten Zustand aus. Ohne Ereignis blieb der Ring bis zu
   30 Minuten dunkel. Fix: `updateLEDs()` am Ende von `initFirstLEDUpdate()`.
2. **Verlorene Updates:** Wurde `pixels.show()` durch ein Sicherheitsflag
   blockiert, war `ledUpdatePending` bereits false — das Update fiel weg.
   Fix: Flag wird zurückgesetzt, der nächste Durchlauf versucht es erneut.

## v9.11 stürzt ab — Rückkehr ist keine Option

Beim Gegentest mit v9.11 (Stand vor dem LED-Umbau):

```
Guru Meditation Error: Core 1 panic'ed (LoadProhibited)
EXCVADDR: 0x1e90ff3e
```

`0x1E90FF` = DodgerBlue = `customColors[2]`. Ein Farbwert wird als
Speicheradresse dereferenziert — der Use-after-free aus
`pixels = Adafruit_NeoPixel(...)` (Copy-Assignment), behoben in v9.12.

## Sackgassen (zur Warnung für die Zukunft)

Diese Ansätze wurden probiert und wieder entfernt — sie haben es verschlimmert:

- **`portDISABLE_INTERRUPTS()` um `pixels.show()`**: Die Bibliothek nutzt auf
  dem ESP32 den RMT-Peripheriebaustein, der interruptgesteuert arbeitet. Mit
  gesperrten Interrupts kann RMT die Übertragung nicht abschließen.
- **`updateLength()` / `begin()` zur Laufzeit** (im Diagnose-Endpoint): Das
  reallokiert den Pixel-Puffer und reinitialisiert RMT, während der Loop
  parallel `show()` ausführt. Nach einem Aufruf mit `count=4` blieb der Puffer
  dauerhaft auf 4 LEDs.
- **`setBrightness()` nach `setPixelColor()`**: Der Helligkeitsfaktor wird beim
  Schreiben jedes Pixels angewendet. Umgekehrte Reihenfolge skaliert den bereits
  gefüllten Puffer ein zweites Mal.
- **`server.arg("hex").c_str()`**: liefert einen Dangling Pointer (Temporary),
  der Hex-Parameter wurde ignoriert.

## Neue Diagnose-Endpoints (dauerhaft)

- `GET /api/led-diag` — `showCount`, `showBlocked`, Sperrflags, `ledClear`,
  `color0`, Pin, `numLeds`, Modus. Ohne diese Werte war von außen nicht
  unterscheidbar, ob die Firmware nicht ansteuert oder die Hardware nicht annimmt.
- `GET /api/led-test?hex=RRGGBB` — schreibt direkt auf die Hardware, ohne
  Zustandslogik. Keine Laufzeit-Rekonfiguration.

Hinweis: `/api/led-test` aktualisiert `appState.ledColors[]` nicht. Nach einem
Selbsttest zeigt der Ring die Testfarbe, bis das nächste reguläre Update kommt.
