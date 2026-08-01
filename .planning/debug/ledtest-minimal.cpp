// Minimaler LED-Test: KEIN WiFi, KEIN MQTT, KEIN Webserver.
// Nur NeoPixel. Wenn der Ring hiermit sauber laeuft, liegt das Problem
// in der Wechselwirkung mit WiFi/RMT der Hauptfirmware.
// Wenn er auch hier spinnt, ist es Hardware (Signalpegel, Strom, Leitung).
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN   26
#define LED_COUNT 12

Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void alle(uint8_t r, uint8_t g, uint8_t b, const char* name) {
  for (int i = 0; i < LED_COUNT; i++) pixels.setPixelColor(i, pixels.Color(r, g, b));
  pixels.show();
  Serial.printf("[%lus] %s -> RGB(%d,%d,%d)\n", millis()/1000, name, r, g, b);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=====================================");
  Serial.println(" MINIMALER LED-TEST (ohne WiFi)");
  Serial.printf("  Pin: %d, LEDs: %d, Typ: GRB 800kHz\n", LED_PIN, LED_COUNT);
  Serial.println("=====================================");

  pixels.begin();
  pixels.setBrightness(40);   // bewusst niedrig: schont die USB-Stromversorgung
  pixels.clear();
  pixels.show();
  Serial.println("Init fertig. Ring sollte jetzt AUS sein.");
  delay(2000);
}

void loop() {
  alle(255, 0, 0, "ROT");     delay(2500);
  alle(0, 255, 0, "GRUEN");   delay(2500);
  alle(0, 0, 255, "BLAU");    delay(2500);
  alle(255, 255, 255, "WEISS"); delay(2500);
  alle(0, 0, 0, "AUS");       delay(2500);

  // Lauflicht: zeigt, ob einzelne LEDs gezielt ansprechbar sind
  Serial.printf("[%lus] LAUFLICHT\n", millis()/1000);
  for (int i = 0; i < LED_COUNT; i++) {
    pixels.clear();
    pixels.setPixelColor(i, pixels.Color(255, 255, 255));
    pixels.show();
    delay(180);
  }
  pixels.clear(); pixels.show();
  delay(1500);
}
